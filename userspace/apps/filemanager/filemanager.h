#pragma once

#include "../../sdk/morphic_api.h"
#include "../../sdk/morphic_syscalls.h"

// File Manager Constants
#define FM_SIDEBAR_WIDTH    200
#define FM_TOOLBAR_HEIGHT   50
#define FM_HEADER_HEIGHT    30
#define FM_FOOTER_HEIGHT    30
#define FM_MAX_FILES        64
#define FM_MAX_PATH         256

// File item structure for display
struct FileItem {
    char name[64];
    uint32_t size;
    bool is_directory;
    bool selected;
};

// Navigation item for sidebar
struct NavItem {
    char label[32];
    char path[64];
    uint32_t icon_color;
    bool is_drive;
};

// File Manager Application
class FileManagerApp : public MorphicAPI::Window {
public:
    FileManagerApp();
    
    void OnUpdate() override {}
    void OnRender(MorphicAPI::Graphics& g) override;
    void OnKeyDown(char c) override;
    void OnMouseDown(int x, int y, int btn) override;

private:
    // Current state
    char currentPath[FM_MAX_PATH];
    FileItem files[FM_MAX_FILES];
    int fileCount;
    int selectedIndex;
    int scrollOffset;
    
    // Navigation
    NavItem navItems[16];
    int navCount;
    
    // UI state
    bool showListView;  // true = list, false = grid
    
    // Text Viewer State
    bool showTextViewer;
    char textViewerContent[4096];
    char textViewerTitle[64];
    int textScrollY;
    
    // Methods
    void RefreshFileList();
    void NavigateTo(const char* path);
    void GoUp();
    void OpenSelected();
    
    // Drawing helpers
    void DrawSidebar(MorphicAPI::Graphics& g);
    void DrawToolbar(MorphicAPI::Graphics& g);
    void DrawBreadcrumb(MorphicAPI::Graphics& g);
    void DrawFileList(MorphicAPI::Graphics& g);
    void DrawTextViewer(MorphicAPI::Graphics& g);
    void CloseTextViewer();
    void DrawFooter(MorphicAPI::Graphics& g);
    void DrawButton(MorphicAPI::Graphics& g, int x, int y, int w, int h, const char* label, uint32_t color);
    void DrawIcon(MorphicAPI::Graphics& g, int x, int y, bool is_folder, uint32_t color);
    
    // Helpers
    void StrCopy(char* dst, const char* src, int max);
    int StrLen(const char* s);
    void FormatSize(uint32_t bytes, char* out);
};
