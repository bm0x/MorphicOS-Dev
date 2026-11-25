#!/usr/bin/env python3
"""
Morphic OS Multi-Language SDK Application Generator
Creates standalone Ring-3 .mpk app projects in C++, Rust, Lua, or Python (AOT).

Usage:
    python tools/new_app.py <AppName> [--lang cpp|rust|lua|python]
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

CPP_TEMPLATE = """#include "../../sdk/morphic_api.h"
#include "../../sdk/morphic_ui.h"
#include "../../sdk/morphic_hw.h"

class {class_name} : public MorphicAPI::Window {{
public:
    {class_name}() : MorphicAPI::Window(560, 340), clicks(0) {{}}

    bool Init() override {{
        if (!MorphicAPI::Window::Init()) return false;
        MorphicHW::GPU::QueryDevice(&gpuInfo);
        MorphicHW::Network::GetInfo(&netInfo);
        Invalidate();
        return true;
    }}

    void OnUpdate() override {{}}
    void OnMouseMove(int x, int y) override {{ (void)x; (void)y; Invalidate(); }}

    void OnRender(MorphicAPI::Graphics& g) override {{
        using namespace MorphicUI;
        DrawWindowBackdrop(g);
        DrawHeaderBar(g, "{display_name}", "Built with MorphicAPI UI v2 + MorphicHW SDK");

        Rect card = {{20, 72, (int)width - 40, 176}};
        DrawCard(g, card);

        int x = card.x + 18, w = card.w - 36, y = card.y + 18;
        g.DrawText(x, y, "HARDWARE CONTEXT", Theme::ACCENT_CYAN, 1);
        y += 28;
        DrawKeyValue(g, x, y, w, "GPU Backend", gpuInfo.vendor_id ? gpuInfo.vendor_name : "Software", Theme::SUCCESS);
        y += 26;
        DrawKeyValue(g, x, y, w, "Network NIC", netInfo.present ? netInfo.driver_name : "None", netInfo.link_up ? Theme::SUCCESS : Theme::TEXT_SECONDARY);

        Rect actionBtn = {{20, (int)height - 56, 180, 36}};
        DrawButton(g, actionBtn, "Trigger Action", ButtonStyle::PRIMARY, actionBtn.Contains(mouseX, mouseY));
    }}

    void OnMouseDown(int x, int y, int btn) override {{
        (void)btn;
        MorphicUI::Rect actionBtn = {{20, (int)height - 56, 180, 36}};
        if (actionBtn.Contains(x, y)) {{ clicks++; Invalidate(); }}
    }}

private:
    MorphicVkPhysicalDeviceInfo gpuInfo = {{}};
    MorphicNetInfo netInfo = {{}};
    uint64_t clicks;
}};

extern "C" int main(void* args) {{
    (void)args;
    {class_name} app;
    app.Run();
    return 0;
}}
"""


def main():
    if len(sys.argv) < 2:
        print("Usage: python tools/new_app.py <AppName> [--lang cpp|rust|lua|python]")
        sys.exit(1)

    lang = "cpp"
    args = sys.argv[1:]
    raw_name = args[0].strip()
    if "--lang" in args:
        idx = args.index("--lang")
        if idx + 1 < len(args):
            lang = args[idx + 1].strip().lower()

    slug = re.sub(r"[^a-z0-9_]+", "_", raw_name.lower()).strip("_")
    if not slug:
        print("[ERROR] Invalid application name.")
        sys.exit(1)

    class_name = "".join(part.capitalize() for part in slug.split("_")) + "App"
    display_name = " ".join(part.capitalize() for part in slug.split("_"))
    app_dir = ROOT / "userspace" / "apps" / slug

    if app_dir.exists():
        print(f"[ERROR] App directory already exists: {app_dir}")
        sys.exit(1)

    app_dir.mkdir(parents=True, exist_ok=True)
    (app_dir / "manifest.txt").write_text(
        f"name={display_name}\nversion=1.0\nentry=main\nlang={lang}\ncategory=User\n",
        encoding="utf-8",
    )

    if lang == "rust":
        (app_dir / "main.rs").write_text(
            f'//! {display_name} - Morphic OS Rust App (#![no_std])\n'
            f'#![no_std]\n#![no_main]\n\n'
            f'#[path = "../../sdk/rust/morphic_sdk.rs"]\nmod morphic_sdk;\n'
            f'use core::panic::PanicInfo;\n'
            f'use morphic_sdk::{{Surface, theme}};\n\n'
            f'#[panic_handler]\nfn panic(_info: &PanicInfo) -> ! {{ loop {{}} }}\n\n'
            f'#[no_mangle]\npub extern "C" fn main(_args: *mut core::ffi::c_void) -> i32 {{\n'
            f'    if let Some(mut s) = Surface::create(520, 300) {{\n'
            f'        s.clear(theme::BG_CANVAS);\n'
            f'        s.fill_rect(20, 20, 480, 44, theme::SURFACE_CARD);\n'
            f'        s.fill_rect(20, 20, 4, 44, theme::ACCENT_CYAN);\n'
            f'        s.present();\n'
            f'        loop {{ morphic_sdk::hw::sleep_ms(50); }}\n'
            f'    }}\n'
            f'    0\n}}\n',
            encoding="utf-8",
        )
        print(f"[SDK][RUST] Created Rust .mpk project: {app_dir}")
        return

    if lang == "lua":
        (app_dir / "main.lua").write_text(
            f'-- {display_name} (MorphicLua Script)\n'
            f'ui.backdrop()\n'
            f'ui.header("{display_name}")\n'
            f'ui.card(20, 70, 500, 190)\n'
            f'ui.text(36, 92, "Running natively inside MorphicLua SDK!")\n',
            encoding="utf-8",
        )
        (app_dir / "main.cpp").write_text(
            f'#include "../../sdk/lua/morphic_lua.h"\n'
            f'static const char* kScript =\n'
            f'    "ui.backdrop()\\n"\n'
            f'    "ui.header(\\"{display_name}\\")\\n"\n'
            f'    "ui.card(20, 70, 500, 190)\\n"\n'
            f'    "ui.text(36, 92, \\"Running natively inside MorphicLua SDK!\\")\\n";\n\n'
            f'class {class_name} : public MorphicAPI::Window {{\npublic:\n'
            f'    {class_name}() : MorphicAPI::Window(540, 300) {{}}\n'
            f'    void OnUpdate() override {{}}\n'
            f'    void OnRender(MorphicAPI::Graphics& g) override {{\n'
            f'        MorphicLua::EvalContext ctx = {{&g, nullptr, nullptr}};\n'
            f'        MorphicLua::RunScript(kScript, ctx);\n'
            f'    }}\n}};\n'
            f'extern "C" int main(void* args) {{ (void)args; {class_name} app; app.Run(); return 0; }}\n',
            encoding="utf-8",
        )
    elif lang == "python":
        py_file = app_dir / "app.py"
        py_file.write_text(
            f'# {display_name} - MorphicPy AOT Application\n'
            f'TITLE = "{display_name}"\n'
            f'SUBTITLE = "Compiled from Python via MorphicPy AOT SDK"\n'
            f'WIDTH = 540\n'
            f'HEIGHT = 300\n\n'
            f'def on_render(ui):\n'
            f'    ui.card(20, 70, 500, 190)\n'
            f'    ui.text(36, 92, "Hello from MorphicPy native x86_64 .mpk!")\n'
            f'    ui.badge(36, 130, "PYTHON AOT")\n',
            encoding="utf-8",
        )
        subprocess.run([sys.executable, str(ROOT / "tools" / "morphic_py2mpk.py"), str(py_file)], check=True)
    else:
        (app_dir / "main.cpp").write_text(
            CPP_TEMPLATE.format(class_name=class_name, display_name=display_name),
            encoding="utf-8",
        )

    (app_dir / "Makefile").write_text(
        f"APP_NAME = {slug}\nSRCS = main.cpp\ninclude ../../sdk/app.mk\n",
        encoding="utf-8",
    )
    print(f"[SDK][{lang.upper()}] Created Morphic OS application: {app_dir}")


if __name__ == "__main__":
    main()
