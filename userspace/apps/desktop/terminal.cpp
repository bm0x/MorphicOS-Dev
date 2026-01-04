#include "terminal.h"
#include "morphic_syscalls.h"

// Helper for memset
static void t_memset(void* ptr, int val, int size) {
    char* p = (char*)ptr;
    for (int i = 0; i < size; i++) p[i] = (char)val;
}

Terminal::Terminal() {
    for (int i = 0; i < ROWS; i++) {
        t_memset(lines[i], 0, COLS + 1);
    }
    currentLine = 0;
    currentCol = 0;
    inputLen = 0;
    t_memset(inputBuffer, 0, sizeof(inputBuffer));
    
    Print("Morphic OS Shell (Userspace)\n");
    Print("Type 'help' for commands.\n");
    Prompt();
}

void Terminal::Prompt() {
    Print("> ");
    inputLen = 0;
    t_memset(inputBuffer, 0, sizeof(inputBuffer));
}

void Terminal::Scroll() {
    // Move lines up
    for (int i = 0; i < ROWS - 1; i++) {
        for (int j = 0; j < COLS + 1; j++) {
            lines[i][j] = lines[i+1][j];
        }
    }
    // Clear last line
    t_memset(lines[ROWS - 1], 0, COLS + 1);
    currentLine = ROWS - 1;
}

void Terminal::NewLine() {
    currentCol = 0;
    currentLine++;
    if (currentLine >= ROWS) {
        Scroll();
    }
}

void Terminal::PutChar(char c) {
    if (c == '\n') {
        NewLine();
        return;
    }
    
    if (currentCol >= COLS) {
        NewLine();
    }
    
    lines[currentLine][currentCol] = c;
    lines[currentLine][currentCol + 1] = 0; // Null terminate
    currentCol++;
}

void Terminal::Print(const char* text) {
    while (*text) {
        PutChar(*text++);
    }
}

void Terminal::OnChar(char c) {
    if (c == 0) return;
    
    if (c == '\n' || c == '\r') {
        Print("\n");
        // Execute command
        if (inputLen > 0) {
            MCL::ProcessCommand(inputBuffer, this);
        }
        Prompt();
    }
    else if (c == '\b') {
        if (inputLen > 0) {
            inputLen--;
            inputBuffer[inputLen] = 0;
            
            // Visual backspace
            if (currentCol > 0) {
                currentCol--;
                lines[currentLine][currentCol] = 0;
                // If we wrapped? (Simple implementation assumes no wrap for input)
                // If prompt is "> ", we shouldn't delete it.
                // Check bounds relative to prompt?
                // For now, just simple backspace.
            }
        }
    }
    else {
        if (inputLen < 127) {
            inputBuffer[inputLen++] = c;
            inputBuffer[inputLen] = 0;
            char str[2] = {c, 0};
            Print(str);
        }
    }
}

void Terminal::Draw(int x, int y, int w, int h) {
    // Background
    Compositor::DrawRect(x, y, w, h, 0xFF000000);
    
    int charH = 16;
    int startY = y + 4;
    int startX = x + 4;
    
    for (int i = 0; i < ROWS; i++) {
        if (lines[i][0] != 0) {
            Compositor::DrawText(startX, startY + (i * charH), lines[i], 0xFF00FF00, 1);
        }
    }
    
    // Cursor
    int cursorX = startX + (currentCol * 8); // 8px font width
    int cursorY = startY + (currentLine * charH);
    
    // Blink cursor?
    static int blink = 0;
    blink++;
    if ((blink / 20) % 2 == 0) {
        Compositor::DrawRect(cursorX, cursorY + 12, 8, 2, 0xFF00FF00);
    }
}
