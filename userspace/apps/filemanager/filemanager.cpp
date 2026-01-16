// Morphic File Manager - Professional File Browser
// Based on modern dark UI design

#include "filemanager.h"

// Colors - Dark Modern Theme
#define COL_BG           0xFF1A1A2E   // Dark blue-gray background
#define COL_SIDEBAR      0xFF16213E   // Darker sidebar
#define COL_HEADER       0xFF0F3460   // Header/accent
#define COL_SELECTED     0xFF3282B8   // Selection blue
#define COL_HOVER        0xFF252542   // Hover state
#define COL_TEXT         0xFFE8E8E8   // Light text
#define COL_TEXT_DIM     0xFF888888   // Dimmed text
#define COL_BORDER       0xFF2D2D44   // Borders
#define COL_FOLDER       0xFFF9A825   // Folder yellow/orange
#define COL_FILE         0xFF64B5F6   // File blue
#define COL_BTN_PRIMARY  0xFF00B894   // Green button
#define COL_BTN_SECONDARY 0xFF2D3436  // Secondary button
#define COL_STORAGE_BAR  0xFF00CEC9   // Storage bar teal

// Utility functions
static void fm_memset(void* ptr, int val, int size) {
    char* p = (char*)ptr;
    for (int i = 0; i < size; i++) p[i] = (char)val;
}

static void fm_memcpy(void* dst, const void* src, int size) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (int i = 0; i < size; i++) d[i] = s[i];
}

FileManagerApp::FileManagerApp() : MorphicAPI::Window(900, 600) {
    // Initialize
    fm_memset(currentPath, 0, FM_MAX_PATH);
    fm_memset(files, 0, sizeof(files));
    fm_memset(navItems, 0, sizeof(navItems));
    
    currentPath[0] = '/';
    currentPath[1] = 'i';
    currentPath[2] = 'n';
    currentPath[3] = 'i';
    currentPath[4] = 't';
    currentPath[5] = 'r';
    currentPath[6] = 'd';
    currentPath[7] = 0;
    
    fileCount = 0;
    selectedIndex = -1;
    scrollOffset = 0;
    scrollOffset = 0;
    showListView = true;
    showTextViewer = false;
    textScrollY = 0;
    fm_memset(textViewerContent, 0, 4096);
    
    // Setup navigation items
    navCount = 0;
    
    // Quick Access
    StrCopy(navItems[navCount].label, "Quick Access", 32);
    StrCopy(navItems[navCount].path, "", 64);
    navItems[navCount].icon_color = COL_SELECTED;
    navItems[navCount].is_drive = false;
    navCount++;
    
    // InitRD
    StrCopy(navItems[navCount].label, "InitRD", 32);
    StrCopy(navItems[navCount].path, "/initrd", 64);
    navItems[navCount].icon_color = COL_FOLDER;
    navItems[navCount].is_drive = true;
    navCount++;
    
    // Data Drive (FAT32)
    StrCopy(navItems[navCount].label, "Data", 32);
    StrCopy(navItems[navCount].path, "/data", 64);
    navItems[navCount].icon_color = COL_STORAGE_BAR;
    navItems[navCount].is_drive = true;
    navCount++;
    
    // Load initial directory
    RefreshFileList();
}

void FileManagerApp::StrCopy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

int FileManagerApp::StrLen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

void FileManagerApp::FormatSize(uint32_t bytes, char* out) {
    if (bytes < 1024) {
        // Format as bytes
        int i = 0;
        uint32_t n = bytes;
        char tmp[16];
        if (n == 0) {
            out[0] = '0';
            out[1] = ' ';
            out[2] = 'B';
            out[3] = 0;
            return;
        }
        while (n > 0) {
            tmp[i++] = '0' + (n % 10);
            n /= 10;
        }
        int j = 0;
        while (i > 0) out[j++] = tmp[--i];
        out[j++] = ' ';
        out[j++] = 'B';
        out[j] = 0;
    } else if (bytes < 1024 * 1024) {
        uint32_t kb = bytes / 1024;
        int i = 0;
        char tmp[16];
        while (kb > 0) {
            tmp[i++] = '0' + (kb % 10);
            kb /= 10;
        }
        int j = 0;
        while (i > 0) out[j++] = tmp[--i];
        out[j++] = ' ';
        out[j++] = 'K';
        out[j++] = 'B';
        out[j] = 0;
    } else {
        uint32_t mb = bytes / (1024 * 1024);
        int i = 0;
        char tmp[16];
        while (mb > 0) {
            tmp[i++] = '0' + (mb % 10);
            mb /= 10;
        }
        int j = 0;
        while (i > 0) out[j++] = tmp[--i];
        out[j++] = ' ';
        out[j++] = 'M';
        out[j++] = 'B';
        out[j] = 0;
    }
}

void FileManagerApp::RefreshFileList() {
    fileCount = 0;
    fm_memset(files, 0, sizeof(files));
    
    DirEntry entries[FM_MAX_FILES];
    int count = sys_readdir(currentPath, entries, FM_MAX_FILES);
    
    if (count > 0) {
        // First add directories
        for (int i = 0; i < count && fileCount < FM_MAX_FILES; i++) {
            if (entries[i].type == 1) {  // Directory
                StrCopy(files[fileCount].name, entries[i].name, 64);
                files[fileCount].size = 0;
                files[fileCount].is_directory = true;
                files[fileCount].selected = false;
                fileCount++;
            }
        }
        // Then add files
        for (int i = 0; i < count && fileCount < FM_MAX_FILES; i++) {
            if (entries[i].type == 0) {  // File
                StrCopy(files[fileCount].name, entries[i].name, 64);
                files[fileCount].size = entries[i].size;
                files[fileCount].is_directory = false;
                files[fileCount].selected = false;
                fileCount++;
            }
        }
    }
    
    selectedIndex = -1;
    scrollOffset = 0;
}

void FileManagerApp::NavigateTo(const char* path) {
    StrCopy(currentPath, path, FM_MAX_PATH);
    RefreshFileList();
}

void FileManagerApp::GoUp() {
    // Find last slash
    int len = StrLen(currentPath);
    if (len <= 1) return;  // Already at root
    
    int lastSlash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (currentPath[i] == '/') {
            lastSlash = i;
            break;
        }
    }
    
    if (lastSlash > 0) {
        currentPath[lastSlash] = 0;
    } else if (lastSlash == 0) {
        currentPath[1] = 0;  // Root "/"
    }
    
    RefreshFileList();
}

void FileManagerApp::OpenSelected() {
    if (selectedIndex < 0 || selectedIndex >= fileCount) return;
    
    FileItem* item = &files[selectedIndex];
    if (item->is_directory) {
        // Navigate into directory
        int pathLen = StrLen(currentPath);
        int nameLen = StrLen(item->name);
        
        if (pathLen + nameLen + 2 < FM_MAX_PATH) {
            if (pathLen > 1) {
                currentPath[pathLen++] = '/';
            }
            for (int i = 0; i < nameLen; i++) {
                currentPath[pathLen + i] = item->name[i];
            }
            currentPath[pathLen + nameLen] = 0;
            RefreshFileList();
        }
    }

    // Files: could open viewer in future
    else {
        // Build full path
        int pathLen = StrLen(currentPath);
        int nameLen = StrLen(item->name);
        char fullPath[FM_MAX_PATH];
        
        int idx = 0;
        // Copy current path
        const char* p = currentPath;
        while(*p && idx < FM_MAX_PATH-1) fullPath[idx++] = *p++;
        
        if (idx > 1 && fullPath[idx-1] != '/') fullPath[idx++] = '/';
        
        // Copy filename
        const char* n = item->name;
        while(*n && idx < FM_MAX_PATH-1) fullPath[idx++] = *n++;
        fullPath[idx] = 0;
        
        // Read File
        int bytesRead = sys_read_file(fullPath, textViewerContent, 4095);
        if (bytesRead >= 0) {
            textViewerContent[bytesRead] = 0;
            StrCopy(textViewerTitle, item->name, 64);
            showTextViewer = true;
            textScrollY = 0;
        }
    }
}

void FileManagerApp::CloseTextViewer() {
    showTextViewer = false;
}

void FileManagerApp::DrawTextViewer(MorphicAPI::Graphics& g) {
    // Dim background
    // g.FillRect(0, 0, width, height, 0x80000000); // Alpha not supported? Check colors. 
    // Just draw a box
    
    int boxW = 600;
    int boxH = 400;
    int boxX = (width - boxW) / 2;
    int boxY = (height - boxH) / 2;
    
    // Shadow
    g.FillRect(boxX + 10, boxY + 10, boxW, boxH, 0xFF101010);
    
    // Main Box
    g.FillRect(boxX, boxY, boxW, boxH, COL_SIDEBAR);
    g.FillRect(boxX, boxY, boxW, 1, COL_BORDER);
    g.FillRect(boxX, boxY, 1, boxH, COL_BORDER);
    g.FillRect(boxX + boxW - 1, boxY, 1, boxH, COL_BORDER);
    g.FillRect(boxX, boxY + boxH - 1, boxW, 1, COL_BORDER);
    
    // Header
    g.FillRect(boxX, boxY, boxW, 30, COL_HEADER);
    g.DrawText(boxX + 10, boxY + 8, textViewerTitle, COL_TEXT, 1);
    
    // Close Button [X]
    g.FillRect(boxX + boxW - 30, boxY, 30, 30, 0xFFE53935); // Red
    g.DrawText(boxX + boxW - 20, boxY + 8, "X", COL_TEXT, 1);
    
    // Content Area
    int contentX = boxX + 10;
    int contentY = boxY + 40;
    int maxWidth = boxW - 20;
    int charsPerLine = maxWidth / 8;  // 8px per char
    if (charsPerLine > 70) charsPerLine = 70;  // Limit line length
    
    // Render line-by-line (MUCH faster than char-by-char)
    const char* p = textViewerContent;
    int cy = contentY;
    char lineBuffer[72];  // Max 70 chars + null
    int lineIdx = 0;
    
    while (*p && cy < boxY + boxH - 20) {
        if (*p == '\n' || lineIdx >= charsPerLine) {
            // Render current line
            lineBuffer[lineIdx] = 0;
            if (lineIdx > 0) {
                g.DrawText(contentX, cy, lineBuffer, COL_TEXT, 1);
            }
            cy += 16;
            lineIdx = 0;
            if (*p == '\n') p++;
        } else {
            lineBuffer[lineIdx++] = *p;
            p++;
        }
    }
    // Render remaining line
    if (lineIdx > 0) {
        lineBuffer[lineIdx] = 0;
        g.DrawText(contentX, cy, lineBuffer, COL_TEXT, 1);
    }
}

void FileManagerApp::OnRender(MorphicAPI::Graphics& g) {
    // Clear background
    g.Clear(COL_BG);
    
    // Draw components
    DrawSidebar(g);
    DrawToolbar(g);
    DrawBreadcrumb(g);
    DrawFileList(g);
    DrawFooter(g);
    
    if (showTextViewer) {
        DrawTextViewer(g);
    }
}

void FileManagerApp::DrawSidebar(MorphicAPI::Graphics& g) {
    // Sidebar background
    g.FillRect(0, 0, FM_SIDEBAR_WIDTH, height, COL_SIDEBAR);
    
    // Border
    g.FillRect(FM_SIDEBAR_WIDTH - 1, 0, 1, height, COL_BORDER);
    
    // Navigation title
    g.DrawText(12, 12, "NAVIGATION", COL_TEXT_DIM, 1);
    
    int y = 35;
    
    for (int i = 0; i < navCount; i++) {
        // Check if this is the current path
        bool isCurrent = false;
        if (navItems[i].path[0]) {
            const char* p1 = currentPath;
            const char* p2 = navItems[i].path;
            isCurrent = true;
            while (*p1 && *p2) {
                if (*p1 != *p2) { isCurrent = false; break; }
                p1++; p2++;
            }
            if (*p2) isCurrent = false;
        }
        
        // Background for selected
        if (isCurrent) {
            g.FillRect(4, y - 2, FM_SIDEBAR_WIDTH - 8, 22, COL_SELECTED);
        }
        
        // Icon (simple box)
        g.FillRect(16, y + 3, 12, 12, navItems[i].icon_color);
        
        // Label
        g.DrawText(34, y + 3, navItems[i].label, COL_TEXT, 1);
        
        y += 28;
        
        // Add separator before drives
        if (i == 0) {
            g.DrawText(12, y, "DRIVES", COL_TEXT_DIM, 1);
            y += 22;
        }
    }
    
    // Storage indicator at bottom
    int storageY = height - 60;
    g.FillRect(1, storageY, FM_SIDEBAR_WIDTH - 2, 1, COL_BORDER);
    g.DrawText(12, storageY + 10, "STORAGE", COL_TEXT_DIM, 1);
    
    // Progress bar
    int barY = storageY + 30;
    int barW = FM_SIDEBAR_WIDTH - 24;
    g.FillRect(12, barY, barW, 8, COL_BORDER);
    g.FillRect(12, barY, barW * 3 / 4, 8, COL_STORAGE_BAR);  // 75% used
    
    g.DrawText(12, barY + 14, "75% used", COL_TEXT_DIM, 1);
}

void FileManagerApp::DrawToolbar(MorphicAPI::Graphics& g) {
    int x = FM_SIDEBAR_WIDTH + 10;
    int y = 40;
    
    // New Folder button (green)
    DrawButton(g, x, y, 80, 28, "New Folder", COL_BTN_PRIMARY);
    x += 90;
    
    // Upload button
    DrawButton(g, x, y, 60, 28, "Upload", COL_BTN_SECONDARY);
    x += 70;
    
    // Share button
    DrawButton(g, x, y, 50, 28, "Share", COL_BTN_SECONDARY);
    x += 60;
    
    // Move to button
    DrawButton(g, x, y, 60, 28, "Move to", COL_BTN_SECONDARY);
    x += 70;
    
    // Delete button
    DrawButton(g, x, y, 55, 28, "Delete", COL_BTN_SECONDARY);
    
    // View toggle (list/grid) - right side
    int toggleX = width - 60;
    g.FillRect(toggleX, y, 24, 24, showListView ? COL_SELECTED : COL_BTN_SECONDARY);
    g.DrawText(toggleX + 6, y + 6, "=", COL_TEXT, 1);
    
    g.FillRect(toggleX + 28, y, 24, 24, !showListView ? COL_SELECTED : COL_BTN_SECONDARY);
    g.DrawText(toggleX + 34, y + 6, "#", COL_TEXT, 1);
}

void FileManagerApp::DrawBreadcrumb(MorphicAPI::Graphics& g) {
    int x = FM_SIDEBAR_WIDTH + 10;
    int y = 10;
    
    // Home icon (simple house shape)
    g.FillRect(x, y + 2, 12, 10, COL_TEXT);
    g.FillRect(x + 2, y, 8, 4, COL_TEXT);
    
    x += 20;
    
    // Path segments
    const char* path = currentPath;
    if (*path == '/') path++;
    
    char segment[32];
    int segIdx = 0;
    
    while (*path) {
        if (*path == '/') {
            if (segIdx > 0) {
                segment[segIdx] = 0;
                g.DrawText(x, y + 2, ">", COL_TEXT_DIM, 1);
                x += 12;
                g.DrawText(x, y + 2, segment, COL_TEXT, 1);
                x += segIdx * 6 + 8;
                segIdx = 0;
            }
        } else {
            if (segIdx < 31) segment[segIdx++] = *path;
        }
        path++;
    }
    
    // Last segment
    if (segIdx > 0) {
        segment[segIdx] = 0;
        g.DrawText(x, y + 2, ">", COL_TEXT_DIM, 1);
        x += 12;
        
        // Draw in box (current folder)
        g.FillRect(x - 4, y - 2, segIdx * 6 + 12, 18, COL_HEADER);
        g.DrawText(x, y + 2, segment, COL_TEXT, 1);
    }
}

void FileManagerApp::DrawButton(MorphicAPI::Graphics& g, int x, int y, int w, int h, const char* label, uint32_t color) {
    // Button background with rounded corners effect
    g.FillRect(x, y, w, h, color);
    
    // Text centered
    int textLen = StrLen(label);
    int textX = x + (w - textLen * 6) / 2;
    int textY = y + (h - 7) / 2;
    g.DrawText(textX, textY, label, COL_TEXT, 1);
}

void FileManagerApp::DrawIcon(MorphicAPI::Graphics& g, int x, int y, bool is_folder, uint32_t color) {
    if (is_folder) {
        // Folder icon - simple folder shape
        g.FillRect(x, y + 3, 16, 12, color);
        g.FillRect(x, y, 8, 4, color);
    } else {
        // File icon - document shape
        g.FillRect(x + 2, y, 12, 16, color);
        g.FillRect(x + 10, y, 4, 4, COL_BG);  // Corner fold
    }
}

void FileManagerApp::DrawFileList(MorphicAPI::Graphics& g) {
    int listX = FM_SIDEBAR_WIDTH;
    int listY = FM_TOOLBAR_HEIGHT + FM_HEADER_HEIGHT + 15;
    int listW = width - FM_SIDEBAR_WIDTH;
    int listH = height - listY - FM_FOOTER_HEIGHT;
    
    // Column headers
    int headerY = listY - 20;
    g.FillRect(listX, headerY, listW, 20, COL_SIDEBAR);
    
    g.DrawText(listX + 40, headerY + 5, "NAME", COL_TEXT_DIM, 1);
    g.DrawText(listX + 350, headerY + 5, "DATE MODIFIED", COL_TEXT_DIM, 1);
    g.DrawText(listX + 500, headerY + 5, "TYPE", COL_TEXT_DIM, 1);
    g.DrawText(listX + 620, headerY + 5, "SIZE", COL_TEXT_DIM, 1);
    
    // Files
    int rowHeight = 32;
    int visibleRows = listH / rowHeight;
    
    for (int i = 0; i < fileCount && i < visibleRows; i++) {
        int idx = i + scrollOffset;
        if (idx >= fileCount) break;
        
        FileItem* item = &files[idx];
        int y = listY + i * rowHeight;
        
        // Selection highlight
        if (idx == selectedIndex) {
            g.FillRect(listX, y, listW, rowHeight - 2, COL_SELECTED);
        } else if (i % 2 == 0) {
            // Alternating row background
            g.FillRect(listX, y, listW, rowHeight - 2, COL_HOVER);
        }
        
        // Checkbox area
        g.FillRect(listX + 12, y + 8, 14, 14, COL_BORDER);
        if (item->selected) {
            g.FillRect(listX + 15, y + 11, 8, 8, COL_BTN_PRIMARY);
        }
        
        // Icon
        DrawIcon(g, listX + 36, y + 6, item->is_directory, 
                 item->is_directory ? COL_FOLDER : COL_FILE);
        
        // Name
        g.DrawText(listX + 60, y + 10, item->name, COL_TEXT, 1);
        
        // Type
        const char* typeStr = item->is_directory ? "Folder" : "File";
        g.DrawText(listX + 500, y + 10, typeStr, COL_TEXT_DIM, 1);
        
        // Size
        if (!item->is_directory && item->size > 0) {
            char sizeStr[16];
            FormatSize(item->size, sizeStr);
            g.DrawText(listX + 620, y + 10, sizeStr, COL_TEXT_DIM, 1);
        }
    }
    
    // Empty state
    if (fileCount == 0) {
        g.DrawText(listX + listW/2 - 60, listY + 50, "(empty folder)", COL_TEXT_DIM, 1);
    }
}

void FileManagerApp::DrawFooter(MorphicAPI::Graphics& g) {
    int footerY = height - FM_FOOTER_HEIGHT;
    
    // Footer background
    g.FillRect(FM_SIDEBAR_WIDTH, footerY, width - FM_SIDEBAR_WIDTH, FM_FOOTER_HEIGHT, COL_SIDEBAR);
    
    // Border
    g.FillRect(FM_SIDEBAR_WIDTH, footerY, width - FM_SIDEBAR_WIDTH, 1, COL_BORDER);
    
    // Item count
    char countStr[32];
    int idx = 0;
    uint32_t n = fileCount;
    char tmp[16];
    int tmpIdx = 0;
    
    if (n == 0) {
        countStr[idx++] = '0';
    } else {
        while (n > 0) {
            tmp[tmpIdx++] = '0' + (n % 10);
            n /= 10;
        }
        while (tmpIdx > 0) countStr[idx++] = tmp[--tmpIdx];
    }
    countStr[idx++] = ' ';
    countStr[idx++] = 'i';
    countStr[idx++] = 't';
    countStr[idx++] = 'e';
    countStr[idx++] = 'm';
    countStr[idx++] = 's';
    countStr[idx] = 0;
    
    g.DrawText(FM_SIDEBAR_WIDTH + 15, footerY + 10, countStr, COL_TEXT_DIM, 1);
    
    // Selected count
    if (selectedIndex >= 0) {
        g.DrawText(FM_SIDEBAR_WIDTH + 100, footerY + 10, "| 1 selected", COL_TEXT_DIM, 1);
    }
}



void FileManagerApp::OnKeyDown(char c) {
    if (showTextViewer) {
        if (c == 27) CloseTextViewer(); // ESC
        return;
    }

    if (c == 27) return;  // ESC handled by base
    
    // Arrow keys for navigation
    if (c == 'w' || c == 'W') {  // Up
        if (selectedIndex > 0) selectedIndex--;
        else if (fileCount > 0) selectedIndex = 0;
    }
    else if (c == 's' || c == 'S') {  // Down
        if (selectedIndex < fileCount - 1) selectedIndex++;
    }
    else if (c == '\n' || c == '\r') {  // Enter - open
        OpenSelected();
    }
    else if (c == '\b') {  // Backspace - go up
        GoUp();
    }
}

void FileManagerApp::OnMouseDown(int x, int y, int btn) {
    if (showTextViewer) {
        // Check Close Button
        int boxW = 600;
        int boxH = 400;
        int boxX = (width - boxW) / 2;
        int boxY = (height - boxH) / 2;
        
        // Close button rect: boxX + boxW - 30, boxY, 30, 30
        if (x >= boxX + boxW - 30 && x < boxX + boxW && y >= boxY && y < boxY + 30) {
            CloseTextViewer();
        }
        return;
    }

    // Sidebar navigation clicks
    if (x < FM_SIDEBAR_WIDTH) {
        int itemY = 35;
        for (int i = 0; i < navCount; i++) {
            if (i == 1) itemY += 22;  // After "Quick Access"
            
            if (y >= itemY && y < itemY + 24 && navItems[i].path[0]) {
                NavigateTo(navItems[i].path);
                return;
            }
            itemY += 28;
        }
        return;
    }
    
    // File list clicks
    int listY = FM_TOOLBAR_HEIGHT + FM_HEADER_HEIGHT + 15;
    int rowHeight = 32;
    
    if (y >= listY && x > FM_SIDEBAR_WIDTH) {
        int clickedRow = (y - listY) / rowHeight + scrollOffset;
        if (clickedRow >= 0 && clickedRow < fileCount) {
            if (clickedRow == selectedIndex && btn == 1) {
                // Double-click simulation (same item)
                OpenSelected();
            } else {
                selectedIndex = clickedRow;
            }
        }
    }
    
    // View toggle clicks
    int toggleX = width - 60;
    int toggleY = 40;
    if (y >= toggleY && y < toggleY + 24) {
        if (x >= toggleX && x < toggleX + 24) {
            showListView = true;
        } else if (x >= toggleX + 28 && x < toggleX + 52) {
            showListView = false;
        }
    }
}
