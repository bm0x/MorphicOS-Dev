#!/usr/bin/env python3
"""
Morphic OS SDK App Generator
Creates a standalone Glass Dark .mpk application under userspace/apps/<name>/
wired with MorphicAPI, MorphicUI v2, MorphicHW, manifest.txt, and autonomous Makefile.

Usage:
  python tools/new_app.py <app_name> ["Display Title"]
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    if len(sys.argv) < 2:
        print("Usage: python tools/new_app.py <app_name> [\"Display Title\"]")
        sys.exit(1)

    raw_name = sys.argv[1].strip().lower()
    if not re.match(r"^[a-z][a-z0-9_]{1,24}$", raw_name):
        print("[ERROR] app_name must match ^[a-z][a-z0-9_]{1,24}$ (e.g. 'hw_monitor')")
        sys.exit(1)

    display_title = sys.argv[2].strip() if len(sys.argv) > 2 else raw_name.replace("_", " ").title()
    class_name = "".join(part.capitalize() for part in raw_name.split("_")) + "App"

    app_dir = ROOT / "userspace" / "apps" / raw_name
    if app_dir.exists():
        print(f"[ERROR] App directory already exists: {app_dir}")
        sys.exit(1)

    app_dir.mkdir(parents=True, exist_ok=True)

    (app_dir / "Makefile").write_text(
        f"APP_NAME := {raw_name}\n"
        f"SRCS := main.cpp\n"
        f"include ../../sdk/app.mk\n",
        encoding="utf-8",
    )

    (app_dir / "manifest.txt").write_text(
        f"NAME={display_title}\n"
        f"VERSION=1.0\n"
        f"DESCRIPTION=Native Morphic OS Application\n",
        encoding="utf-8",
    )

    main_cpp = f'''#include "../../sdk/morphic_api.h"
#include "../../sdk/morphic_ui.h"
#include "../../sdk/morphic_hw.h"

class {class_name} : public MorphicAPI::Window {{
public:
    {class_name}() : MorphicAPI::Window(520, 320), clicks(0) {{}}

    void OnUpdate() override {{}}

    void OnRender(MorphicAPI::Graphics& g) override {{
        using namespace MorphicUI;
        DrawWindowBackdrop(g);
        DrawHeaderBar(g, "{display_title}", "Built with MorphicAPI v2 + MorphicHW SDK", 54);

        Rect card = {{18, 70, (int)width - 36, 170}};
        DrawCard(g, card);

        MorphicVkPhysicalDeviceInfo gpu = {{}};
        bool hasGpu = MorphicHW::GPU::QueryDevice(&gpu);
        DrawKeyValue(g, card.x + 16, card.y + 18, card.w - 32, "GPU Layer:",
                     hasGpu ? gpu.vendor_name : "Software", Theme::ACCENT_CYAN);

        MorphicSystemInfo sys = {{}};
        MorphicHW::System::GetInfo(&sys);
        DrawKeyValue(g, card.x + 16, card.y + 44, card.w - 32, "CPU Vendor:",
                     sys.cpu_vendor, Theme::TEXT_PRIMARY);

        DrawProgressBar(g, {{card.x + 16, card.y + 84, card.w - 32, 10}}, (clicks * 10) % 101);

        btnAction = {{card.x + 16, card.y + 114, 180, 36}};
        DrawButton(g, btnAction, "Interact (+10%)", ButtonStyle::PRIMARY,
                   btnAction.Contains(GetMouseX(), GetMouseY()));
    }}

    void OnMouseMove(int x, int y) override {{
        (void)x; (void)y;
        Invalidate();
    }}

    void OnMouseDown(int x, int y, int btn) override {{
        (void)btn;
        if (btnAction.Contains(x, y)) {{
            clicks++;
            Invalidate();
        }}
    }}

private:
    int clicks;
    MorphicUI::Rect btnAction;
}};

extern "C" int main(void* args) {{
    (void)args;
    {class_name} app;
    app.Run();
    return 0;
}}
'''
    (app_dir / "main.cpp").write_text(main_cpp, encoding="utf-8")

    print(f"[SDK] Created Morphic OS app '{raw_name}' at: {app_dir}")
    print(f"      Build on Linux/macOS: cd userspace/apps/{raw_name} && make")
    print(f"      Or add to USERSPACE_APPS in tools/build_win.py and run .\\build_windows.bat")


if __name__ == "__main__":
    main()
