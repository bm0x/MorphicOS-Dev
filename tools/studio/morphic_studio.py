#!/usr/bin/env python3
import os
import sys
import subprocess
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import shutil
import re

# Configuration
MORPHIC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../"))
USERSPACE_DIR = os.path.join(MORPHIC_ROOT, "userspace")
APPS_DIR = os.path.join(USERSPACE_DIR, "apps")
SDK_MK = os.path.join(USERSPACE_DIR, "sdk/app.mk")

# Theme Colors (Dark "Professional" Theme)
BG_COLOR = "#1e1e1e"
SIDEBAR_COLOR = "#252526"
ACCENT_COLOR = "#007acc"
TEXT_COLOR = "#cccccc"
ENTRY_BG = "#3c3c3c"
SUCCESS_COLOR = "#4ec9b0"
ERROR_COLOR = "#f44747"

class MorphicStudio(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Morphic Studio - MPK Builder")
        self.geometry("1100x750")
        self.configure(bg=BG_COLOR)
        
        self.current_project_path = None
        self.project_sources = []
        
        self._setup_styles()
        self._create_layout()
        self._init_welcome_screen()

    def _setup_styles(self):
        style = ttk.Style()
        style.theme_use('clam')
        
        style.configure("TFrame", background=BG_COLOR)
        style.configure("Sidebar.TFrame", background=SIDEBAR_COLOR)
        style.configure("TLabel", background=BG_COLOR, foreground=TEXT_COLOR, font=("Segoe UI", 10))
        style.configure("Header.TLabel", font=("Segoe UI", 16, "bold"), foreground="#ffffff")
        style.configure("TButton", background=ACCENT_COLOR, foreground="white", borderwidth=0, font=("Segoe UI", 10))
        style.map("TButton", background=[('active', '#005f9e')])
        
        style.configure("TEntry", fieldbackground=ENTRY_BG, foreground="white", borderwidth=0)
        style.configure("TNotebook", background=BG_COLOR, borderwidth=0)
        style.configure("TNotebook.Tab", background=SIDEBAR_COLOR, foreground=TEXT_COLOR, padding=[10, 5])
        style.map("TNotebook.Tab", background=[('selected', BG_COLOR)], foreground=[('selected', 'white')])

    def _create_layout(self):
        # Sidebar
        self.sidebar = ttk.Frame(self, style="Sidebar.TFrame", width=250)
        self.sidebar.pack(side="left", fill="y")
        self.sidebar.pack_propagate(False)
        
        # Title in Sidebar
        lbl_title = ttk.Label(self.sidebar, text="MORPHIC\nSTUDIO", font=("Segoe UI", 20, "bold"), background=SIDEBAR_COLOR, foreground="white")
        lbl_title.pack(pady=20, padx=20, anchor="w")
        
        # Sidebar Buttons
        self._create_sidebar_btn("New Project", self.new_project)
        self._create_sidebar_btn("Open Project", self.open_project_dialog)
        ttk.Separator(self.sidebar, orient="horizontal").pack(fill="x", pady=10, padx=10)
        self.btn_build = self._create_sidebar_btn("Build MPK", self.build_project, state="disabled")
        self.btn_clean = self._create_sidebar_btn("Clean", self.clean_project, state="disabled")
        
        # Main Content Area
        self.main_area = ttk.Frame(self)
        self.main_area.pack(side="right", fill="both", expand=True, padx=20, pady=20)

    def _create_sidebar_btn(self, text, command, state="normal"):
        btn = tk.Button(self.sidebar, text=text, command=command, bg=SIDEBAR_COLOR, fg="white", 
                        activebackground=ACCENT_COLOR, activeforeground="white", bd=0, 
                        font=("Segoe UI", 11), anchor="w", padx=20, pady=10, state=state)
        btn.pack(fill="x")
        return btn

    def _init_welcome_screen(self):
        for widget in self.main_area.winfo_children():
            widget.destroy()
            
        lbl = ttk.Label(self.main_area, text="Welcome to Morphic Studio", style="Header.TLabel")
        lbl.pack(pady=20)
        
        lbl_sub = ttk.Label(self.main_area, text="Select a project to begin managing your MPK application.")
        lbl_sub.pack()

    def _parse_makefile(self):
        """Parses Makefile to find APP_SRCS"""
        self.project_sources = []
        mk_path = os.path.join(self.current_project_path, "Makefile")
        if not os.path.exists(mk_path):
            return

        try:
            with open(mk_path, "r") as f:
                content = f.read()
                # Simple regex to find APP_SRCS = file1.cpp file2.cpp ...
                # Handles multiline with backslash
                match = re.search(r'APP_SRCS\s*=\s*(.*)', content, re.MULTILINE)
                if match:
                    raw_srcs = match.group(1)
                    # Clean up backslashes and newlines if any (basic handling)
                    raw_srcs = raw_srcs.replace('\\', ' ')
                    self.project_sources = [s.strip() for s in raw_srcs.split() if s.strip()]
        except Exception as e:
            print(f"Error parsing Makefile: {e}")

    def _load_project_ui(self):
        for widget in self.main_area.winfo_children():
            widget.destroy()
            
        # Parse Makefile to get sources
        self._parse_makefile()

        # Enable buttons
        self.btn_build.config(state="normal", bg=ACCENT_COLOR)
        self.btn_clean.config(state="normal")

        # Project Header
        header_frame = ttk.Frame(self.main_area)
        header_frame.pack(fill="x", pady=(0, 20))
        
        project_name = os.path.basename(self.current_project_path)
        ttk.Label(header_frame, text=project_name, style="Header.TLabel").pack(side="left")
        ttk.Label(header_frame, text=f"Path: {self.current_project_path}", foreground="#888888").pack(side="left", padx=10, anchor="sw")

        # Tabs
        self.notebook = ttk.Notebook(self.main_area)
        self.notebook.pack(fill="both", expand=True)
        
        self.tab_code = ttk.Frame(self.notebook)
        self.tab_manifest = ttk.Frame(self.notebook)
        self.tab_assets = ttk.Frame(self.notebook)
        self.tab_console = ttk.Frame(self.notebook)
        
        self.notebook.add(self.tab_code, text="Source Code")
        self.notebook.add(self.tab_manifest, text="Manifest & Config")
        self.notebook.add(self.tab_assets, text="Assets Manager")
        self.notebook.add(self.tab_console, text="Build Output")
        
        self._init_code_tab()
        self._init_manifest_tab()
        self._init_assets_tab()
        self._init_console_tab()

    def _init_code_tab(self):
        frame = ttk.Frame(self.tab_code)
        frame.pack(fill="both", expand=True, padx=10, pady=10)
        
        # Split: List of files (left) vs Editor (right)
        paned = tk.PanedWindow(frame, orient=tk.HORIZONTAL, bg=BG_COLOR, sashwidth=4)
        paned.pack(fill="both", expand=True)
        
        # File List
        list_frame = ttk.Frame(paned)
        lbl = ttk.Label(list_frame, text="Source Files (APP_SRCS)")
        lbl.pack(anchor="w", pady=(0, 5))
        
        self.src_list = tk.Listbox(list_frame, bg=ENTRY_BG, fg="white", borderwidth=0, font=("Consolas", 10))
        self.src_list.pack(fill="both", expand=True)
        self.src_list.bind('<<ListboxSelect>>', self._on_src_select)
        
        for src in self.project_sources:
            self.src_list.insert("end", src)
            
        paned.add(list_frame, width=200)
        
        # Editor Area
        edit_frame = ttk.Frame(paned)
        self.lbl_current_file = ttk.Label(edit_frame, text="Select a file to view/edit")
        self.lbl_current_file.pack(anchor="w", pady=(0, 5))
        
        self.code_editor = tk.Text(edit_frame, bg="#1e1e1e", fg="#d4d4d4", insertbackground="white", 
                                   font=("Consolas", 11), borderwidth=0)
        self.code_editor.pack(fill="both", expand=True)
        
        # Simple Save Button
        btn_save_code = ttk.Button(edit_frame, text="Save File", command=self._save_current_file)
        btn_save_code.pack(anchor="e", pady=5)
        
        paned.add(edit_frame)
        
        # Select first file if available
        if self.project_sources:
            self.src_list.selection_set(0)
            self._on_src_select(None)

    def _on_src_select(self, event):
        sel = self.src_list.curselection()
        if not sel: return
        filename = self.src_list.get(sel[0])
        filepath = os.path.join(self.current_project_path, filename)
        
        self.lbl_current_file.config(text=f"Editing: {filename}")
        self.current_editing_file = filepath
        
        if os.path.exists(filepath):
            with open(filepath, "r") as f:
                self.code_editor.delete(1.0, "end")
                self.code_editor.insert(1.0, f.read())
        else:
            self.code_editor.delete(1.0, "end")
            self.code_editor.insert(1.0, "// File not found or not created yet.")

    def _save_current_file(self):
        if hasattr(self, 'current_editing_file') and self.current_editing_file:
            content = self.code_editor.get(1.0, "end-1c")
            try:
                with open(self.current_editing_file, "w") as f:
                    f.write(content)
                self.log(f"Saved {os.path.basename(self.current_editing_file)}", SUCCESS_COLOR)
            except Exception as e:
                messagebox.showerror("Error", f"Could not save file: {e}")

    def _init_manifest_tab(self):
        frame = ttk.Frame(self.tab_manifest)
        frame.pack(fill="both", expand=True, padx=20, pady=20)
        
        # Form
        self.manifest_entries = {}
        fields = ["name", "version", "permissions"]
        
        # Read current manifest
        manifest_path = os.path.join(self.current_project_path, "manifest.txt")
        current_data = {}
        if os.path.exists(manifest_path):
            with open(manifest_path, "r") as f:
                for line in f:
                    if "=" in line:
                        k, v = line.strip().split("=", 1)
                        current_data[k.strip()] = v.strip()

        for i, field in enumerate(fields):
            lbl = ttk.Label(frame, text=field.capitalize() + ":")
            lbl.grid(row=i, column=0, sticky="w", pady=10)
            
            entry = tk.Entry(frame, bg=ENTRY_BG, fg="white", insertbackground="white", font=("Consolas", 11))
            entry.grid(row=i, column=1, sticky="ew", padx=10, pady=10)
            entry.insert(0, current_data.get(field, ""))
            self.manifest_entries[field] = entry
            
        frame.columnconfigure(1, weight=1)
        
        btn_save = ttk.Button(frame, text="Save Manifest", command=self.save_manifest)
        btn_save.grid(row=len(fields), column=1, sticky="e", pady=20)

    def _init_assets_tab(self):
        frame = ttk.Frame(self.tab_assets)
        frame.pack(fill="both", expand=True, padx=20, pady=20)
        
        # Toolbar
        toolbar = ttk.Frame(frame)
        toolbar.pack(fill="x", pady=(0, 10))
        ttk.Button(toolbar, text="Refresh List", command=self.refresh_assets).pack(side="left")
        ttk.Button(toolbar, text="Open Folder", command=lambda: subprocess.Popen(["xdg-open", os.path.join(self.current_project_path, "assets")])).pack(side="left", padx=10)
        
        # List
        self.assets_list = tk.Listbox(frame, bg=ENTRY_BG, fg="white", borderwidth=0, highlightthickness=0, font=("Consolas", 10))
        self.assets_list.pack(fill="both", expand=True)
        
        self.refresh_assets()

    def _init_console_tab(self):
        self.console = tk.Text(self.tab_console, bg="#101010", fg=TEXT_COLOR, font=("Consolas", 10), state="disabled")
        self.console.pack(fill="both", expand=True, padx=10, pady=10)

    # --- Logic ---

    def log(self, message, color=TEXT_COLOR):
        self.console.config(state="normal")
        self.console.insert("end", message + "\n")
        self.console.see("end")
        self.console.config(state="disabled")
        self.update_idletasks()

    def new_project(self):
        name = tk.simpledialog.askstring("New Project", "Enter App Name (no spaces):")
        if not name: return
        
        path = os.path.join(APPS_DIR, name)
        if os.path.exists(path):
            messagebox.showerror("Error", "Project already exists!")
            return
            
        os.makedirs(path)
        os.makedirs(os.path.join(path, "assets"))
        
        # Create Manifest
        with open(os.path.join(path, "manifest.txt"), "w") as f:
            f.write(f"name={name}\nversion=0.1.0\npermissions=VIDEO_ACCESS")
            
        # Create Main.cpp
        with open(os.path.join(path, "main.cpp"), "w") as f:
            f.write(f"""#include "mpk.h"

extern "C" int main(void* assets_ptr) {{
    // Entry point for {name}
    // Use mpk_find_asset(assets_ptr, "name", &size) to load resources.
    return 0;
}}
""")
            
        # Create Makefile
        with open(os.path.join(path, "Makefile"), "w") as f:
            f.write(f"""APP_NAME = {name}
APP_SRCS = main.cpp
# Add assets here, e.g.: assets/icon.png
APP_ASSETS = 

MORPHIC_ROOT = ../../..
include $(MORPHIC_ROOT)/userspace/sdk/app.mk
""")
        
        self.current_project_path = path
        self._load_project_ui()
        messagebox.showinfo("Success", f"Project {name} created!")

    def open_project_dialog(self):
        path = filedialog.askdirectory(initialdir=APPS_DIR, title="Select App Folder")
        if path:
            self.current_project_path = path
            self._load_project_ui()

    def save_manifest(self):
        path = os.path.join(self.current_project_path, "manifest.txt")
        with open(path, "w") as f:
            for k, entry in self.manifest_entries.items():
                f.write(f"{k}={entry.get()}\n")
        messagebox.showinfo("Saved", "Manifest updated.")

    def refresh_assets(self):
        self.assets_list.delete(0, "end")
        assets_dir = os.path.join(self.current_project_path, "assets")
        if os.path.exists(assets_dir):
            for f in os.listdir(assets_dir):
                self.assets_list.insert("end", f)

    def build_project(self):
        self.notebook.select(self.tab_console)
        self.console.config(state="normal")
        self.console.delete(1.0, "end")
        self.log(f"Building {os.path.basename(self.current_project_path)}...", ACCENT_COLOR)
        
        try:
            # Run Make
            proc = subprocess.Popen(["make"], cwd=self.current_project_path, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            stdout, stderr = proc.communicate()
            
            if stdout: self.log(stdout)
            if stderr: self.log(stderr, ERROR_COLOR)
            
            if proc.returncode == 0:
                self.log("BUILD SUCCESSFUL", SUCCESS_COLOR)
                messagebox.showinfo("Build", "MPK Package created successfully!")
            else:
                self.log("BUILD FAILED", ERROR_COLOR)
                
        except Exception as e:
            self.log(f"Error: {str(e)}", ERROR_COLOR)

    def clean_project(self):
        if messagebox.askyesno("Clean", "Remove build artifacts?"):
            subprocess.run(["make", "clean"], cwd=self.current_project_path)
            self.log("Project cleaned.")

if __name__ == "__main__":
    app = MorphicStudio()
    app.mainloop()
