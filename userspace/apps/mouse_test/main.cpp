#include <stdint.h>
#include "../../sdk/morphic_syscalls.h"

extern "C" int main(void* arg) {
    (void)arg;
    while (1) {
        uint64_t st = sys_get_mouse_state();
        uint8_t btn = (st >> 56) & 0xFF;
        uint32_t x = (st >> 32) & 0xFFFFFFFF;
        uint32_t y = st & 0xFFFFFFFF;
        // Print minimal: use sys_debug_print (string) + simple number encoding
        char buf[64];
        int len = 0;
        // Format: MOUSE x,y btn=0xXX\n
        const char* p = "MOUSE ";
        for (; *p; p++) buf[len++] = *p;
        // simple dec for x
        int tx = 0; char sx[16];
        if (x == 0) { sx[tx++] = '0'; }
        else {
            uint32_t vx = x; char tmp[16]; int ti=0;
            while (vx) { tmp[ti++] = '0' + (vx % 10); vx /= 10; }
            while (ti--) sx[tx++] = tmp[ti];
        }
        for (int i=0;i<tx;i++) buf[len++]=sx[i];
        buf[len++]=',';
        int ty = 0; char sy[16];
        if (y == 0) { sy[ty++] = '0'; }
        else {
            uint32_t vy = y; char tmp[16]; int ti=0;
            while (vy) { tmp[ti++] = '0' + (vy % 10); vy /= 10; }
            while (ti--) sy[ty++] = tmp[ti];
        }
        for (int i=0;i<ty;i++) buf[len++]=sy[i];
        buf[len++]=' ';
        const char* bpre = "btn=0x"; for (const char* q=bpre; *q; q++) buf[len++]=*q;
        // hex nibble
        const char* hex = "0123456789ABCDEF";
        buf[len++]=hex[(btn>>4)&0xF]; buf[len++]=hex[btn&0xF];
        buf[len++]='\n';
        buf[len]=0;
        sys_debug_print(buf);
        // Sleep a bit to avoid saturating serial
        sys_sleep(100);
    }
    return 0;
}
