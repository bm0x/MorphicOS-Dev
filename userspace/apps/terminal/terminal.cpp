#include "terminal.h"

static void t_memset(void* ptr, int val, int size) {
    char* p = (char*)ptr;
    for (int i = 0; i < size; i++) p[i] = (char)val;
}

TerminalApp::TerminalApp() : MorphicAPI::Window(660, 420) {
    for (int i = 0; i < ROWS; i++) {
        t_memset(lines[i], 0, COLS + 1);
    }
    currentLine = 0;
    currentCol = 0;
    inputLen = 0;
    t_memset(inputBuffer, 0, sizeof(inputBuffer));
    
    Print("Morphic OS Shell (Independent App)\n");
    Print("Type 'help' for commands.\n");
    Prompt();
}

void TerminalApp::Prompt() {
    Print("> ");
    inputLen = 0;
    t_memset(inputBuffer, 0, sizeof(inputBuffer));
}

void TerminalApp::Scroll() {
    for (int i = 0; i < ROWS - 1; i++) {
        for (int j = 0; j < COLS + 1; j++) {
            lines[i][j] = lines[i+1][j];
        }
    }
    t_memset(lines[ROWS - 1], 0, COLS + 1);
    currentLine = ROWS - 1;
}

void TerminalApp::NewLine() {
    currentCol = 0;
    currentLine++;
    if (currentLine >= ROWS) {
        Scroll();
    }
}

void TerminalApp::PutChar(char c) {
    if (c == '\n') {
        NewLine();
        return;
    }
    
    if (currentCol >= COLS) {
        NewLine();
    }
    
    lines[currentLine][currentCol] = c;
    lines[currentLine][currentCol + 1] = 0;
    currentCol++;
}

void TerminalApp::Print(const char* text) {
    while (*text) {
        PutChar(*text++);
    }
}

void TerminalApp::OnKeyDown(char c) {
    if (c == 0) return;
    
    // Basic Key Handling
    if (c == 27) return; // ESC handled by Window loop (closes app)

    if (c == '\n' || c == '\r') {
        Print("\n");
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

void TerminalApp::OnRender(MorphicAPI::Graphics& g) {
    g.Clear(0xFF000000); // Black background

    int charH = 16;
    int startY = 4;
    int startX = 4;

    for (int i = 0; i < ROWS; i++) {
        if (lines[i][0] != 0) {
            // Draw String logic
            int x = startX;
            int y = startY + (i * charH);
            char* p = lines[i];
            while(*p) {
                DrawChar(g, x, y, *p, 0xFF00FF00); // Green Text
                x += 8;
                p++;
            }
        }
    }

    // Cursor
    int cursorX = startX + (currentCol * 8);
    int cursorY = startY + (currentLine * charH);
    
    // Simple Blink simulation (frame count based, if we had it, but static is fine for MPK refresh)
    g.FillRect(cursorX, cursorY + 12, 8, 2, 0xFF00FF00);
}

void TerminalApp::DrawChar(MorphicAPI::Graphics& g, int x, int y, char c, uint32_t color) {
    // Very Basic Debug Font (Rectangles)
    // In real app, we need a font. 
    // Just drawing a box for availability.
    if (c == ' ') return;
    g.FillRect(x + 2, y + 2, 4, 10, color);
}
