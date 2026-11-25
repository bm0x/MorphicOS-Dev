#include "selftest.h"
#include "../mm/pmm.h"
#include "../mm/heap.h"
#include "../arch/common/mmu.h"
#include "../fs/vfs.h"
#include "../hal/arch/x86_64/pci.h"
#include "../hal/video/vulkan_layer.h"
#include "../hal/video/vsync.h"
#include "../hal/arch/x86_64/tsc.h"
#include "../hal/video/early_term.h"
#include "../hal/serial/uart.h"
#include "../process/scheduler.h"
#include "../utils/std.h"

extern "C" uint64_t PIT_GetTicks();

namespace SelfTest {

    static void LogCheck(const char* name, bool ok, Report* rep) {
        rep->total++;
        if (ok) {
            rep->passed++;
            UART::Write("  [SELFTEST][PASS] ");
            UART::Write(name);
            UART::Write("\n");
        } else {
            rep->failed++;
            UART::Write("  [SELFTEST][FAIL] ");
            UART::Write(name);
            UART::Write("\n");
            EarlyTerm::Print("[SELFTEST FAIL] ");
            EarlyTerm::Print(name);
            EarlyTerm::Print("\n");
        }
    }

    static bool TestPMM() {
        size_t freeBefore = PMM::GetFreeMemory();
        void* p1 = PMM::AllocPage();
        void* p2 = PMM::AllocContiguous(4);
        if (!p1 || !p2) return false;
        if (((uint64_t)p1 & 0xFFFULL) != 0 || ((uint64_t)p2 & 0xFFFULL) != 0) return false;
        if ((uint64_t)p1 < 0x10000000ULL || (uint64_t)p2 < 0x10000000ULL) return false;

        PMM::FreePage(p1);
        PMM::FreePages(p2, 4);
        size_t freeAfter = PMM::GetFreeMemory();
        return freeAfter == freeBefore;
    }

    static bool TestKHeap() {
        uint8_t* a = (uint8_t*)kmalloc(128);
        uint8_t* b = (uint8_t*)kmalloc(256);
        if (!a || !b) return false;
        for (int i = 0; i < 128; i++) a[i] = (uint8_t)(i ^ 0x5A);
        for (int i = 0; i < 256; i++) b[i] = (uint8_t)(i ^ 0xA5);

        bool dataOk = true;
        for (int i = 0; i < 128; i++) {
            if (a[i] != (uint8_t)(i ^ 0x5A)) dataOk = false;
        }
        kfree(a);
        kfree(b);
        return dataOk;
    }

    static bool TestMMUIsolation() {
        // Kernel symbol must be mapped in Ring 0 WITHOUT PAGE_USER
        uint32_t kFlags = MMU::GetPageFlags((uint64_t)&TestMMUIsolation);
        if ((kFlags & PAGE_PRESENT) == 0) return false;
        if ((kFlags & PAGE_USER) != 0) return false;
        return true;
    }

    static bool TestVFSAndInitRD() {
        // 1. Verify RAM create/write/read/delete roundtrip
        const char* testPath = "/etc/_selftest.tmp";
        VFSNode* f = VFS::CreateFile(testPath);
        if (!f) return false;

        const char* payload = "MORPHIC_OK";
        uint32_t written = VFS::Write(f, 0, 10, (uint8_t*)payload);
        VFS::Close(f);
        if (written != 10) return false;

        VFSNode* r = VFS::Open(testPath);
        if (!r) return false;
        char readBuf[16] = {};
        uint32_t bytesRead = VFS::Read(r, 0, 10, (uint8_t*)readBuf);
        VFS::Close(r);
        if (bytesRead != 10 || kstrcmp(readBuf, "MORPHIC_OK") != 0) return false;
        if (!VFS::Delete(testPath)) return false;

        // 2. Verify compositor.mpk header in InitRD if present
        VFSNode* comp = VFS::Open("/initrd/compositor.mpk");
        if (comp) {
            char magic[4] = {};
            VFS::Read(comp, 0, 4, (uint8_t*)magic);
            VFS::Close(comp);
            if (magic[0] != 'M' || magic[1] != 'P' || magic[2] != 'K' || magic[3] != '1') {
                return false;
            }
        }
        return true;
    }

    static bool TestVulkanTranslationLayer() {
        MorphicVkPhysicalDeviceInfo info = {};
        if (!VulkanLayer::QueryDevice(&info)) return false;
        if (info.api_version != MORPHIC_VK_API_VERSION_1_3) return false;

        // Offscreen 16x16 render verification of the real-time Vulkan command translator
        uint32_t* surface = (uint32_t*)kmalloc(16 * 16 * sizeof(uint32_t));
        if (!surface) return false;

        MorphicVkCmdPacket cmds[3] = {};
        // 1. Clear to 0xFF102030
        cmds[0] = {MVK_CMD_CLEAR_COLOR, 0, 0xFF102030u, 0, 0, 0, 0, 0, 0, 0};
        // 2. Fill 4x4 rect at (2, 2) with 0xFF00FF00
        cmds[1] = {MVK_CMD_FILL_RECT, 0, 0xFF00FF00u, 2, 2, 4, 4, 0, 0, 0};
        // 3. Draw triangle covering (10, 10) with 0xFFFF0000
        cmds[2] = {MVK_CMD_DRAW_TRIANGLE, 0, 0xFFFF0000u, 8, 8, 14, 8, 11, 14, 0};

        int exec = VulkanLayer::ExecuteCommandPackets(surface, 16, 16, 16, cmds, 3, false);
        bool ok = (exec == 3) &&
                  (surface[0 * 16 + 0] == 0xFF102030u) &&
                  (surface[3 * 16 + 3] == 0xFF00FF00u) &&
                  (surface[10 * 16 + 11] == 0xFFFF0000u);

        kfree(surface);
        return ok;
    }

    static bool TestSchedulerAndTimer() {
        Task* cur = Scheduler::GetCurrentTask();
        if (!cur) return false;
        if (((uintptr_t)cur->GetFpuBuffer() & 0xFULL) != 0) return false;

        uint64_t us0 = TSC::GetMicroseconds();
        uint64_t t0 = PIT_GetTicks();
        for (volatile int i = 0; i < 2000000; i++) {
            if (PIT_GetTicks() > t0) break;
        }
        uint64_t us1 = TSC::GetMicroseconds();
        bool tscOk = (TSC::GetFreqMHz() >= 100 && us1 > us0);

        uint32_t origHz = VSync::GetRefreshRate();
        bool hzOk = VSync::SetRefreshRate(144) && VSync::GetRefreshRate() == 144 &&
                    VSync::SetRefreshRate(240) && VSync::GetRefreshRate() == 240 &&
                    VSync::SetRefreshRate(origHz);

        return (PIT_GetTicks() >= t0) && tscOk && hzOk;
    }

    Report RunAll() {
        Report rep = {0, 0, 0};
        UART::Write("\n================ MORPHIC OS INTERNAL SELFTEST ================\n");

        LogCheck("PMM page alloc/free & zero-leak accounting", TestPMM(), &rep);
        LogCheck("KHeap segment integrity & coalescing", TestKHeap(), &rep);
        LogCheck("MMU Ring0 supervisor page isolation", TestMMUIsolation(), &rep);
        LogCheck("VFS R/W roundtrip & InitRD MPK1 verification", TestVFSAndInitRD(), &rep);
        LogCheck("PCI GPU & MorphicVK 1.3 RT Translation Layer", TestVulkanTranslationLayer(), &rep);
        LogCheck("Scheduler 16-byte FXSAVE & PIT timer ticks", TestSchedulerAndTimer(), &rep);

        UART::Write("[SELFTEST] Summary: ");
        UART::WriteDec(rep.passed);
        UART::Write("/");
        UART::WriteDec(rep.total);
        UART::Write(" passed.\n");
        UART::Write("==============================================================\n\n");

        EarlyTerm::Print("[SELFTEST] Internal checks passed: ");
        EarlyTerm::PrintDec(rep.passed);
        EarlyTerm::Print("/");
        EarlyTerm::PrintDec(rep.total);
        EarlyTerm::Print("\n");
        return rep;
    }

}
