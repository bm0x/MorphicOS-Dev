#include "terminal.h"

static void t_memset(void* ptr, int val, int size) {
    char* p = (char*)ptr;
    for (int i = 0; i < size; i++) p[i] = (char)val;
}

static void t_memcpy(void* dst, const void* src, int size) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (int i = 0; i < size; i++) d[i] = s[i];
}

TerminalApp::TerminalApp() : MorphicAPI::Window(660, 420) {
    // Clear all lines
    for (int i = 0; i < ROWS; i++) {
        t_memset(lines[i], 0, COLS + 1);
    }
    currentLine = 0;
    currentCol = 0;
    inputLen = 0;
    t_memset(inputBuffer, 0, sizeof(inputBuffer));
    
    PrintLine("Morphic OS Shell v1.0");
    PrintLine("Type 'help' for available commands.");
    PrintLine("");
    Prompt();
}

void TerminalApp::Prompt() {
    // Start a new prompt on current line
    currentCol = 0;
    t_memset(lines[currentLine], 0, COLS + 1);
    lines[currentLine][0] = '>';
    lines[currentLine][1] = ' ';
    currentCol = 2;
    inputLen = 0;
    t_memset(inputBuffer, 0, sizeof(inputBuffer));
}

void TerminalApp::Scroll() {
    // Move all lines up by one
    for (int i = 0; i < ROWS - 1; i++) {
        t_memcpy(lines[i], lines[i + 1], COLS + 1);
    }
    // Clear the last line
    t_memset(lines[ROWS - 1], 0, COLS + 1);
    currentLine = ROWS - 1;
    currentCol = 0;
}

void TerminalApp::NewLine() {
    // Move to next line
    currentCol = 0;
    currentLine++;
    
    // Scroll if we exceed screen
    if (currentLine >= ROWS) {
        Scroll();
    }
    
    // Clear the new line
    t_memset(lines[currentLine], 0, COLS + 1);
}

void TerminalApp::PrintLine(const char* text) {
    // Print text and advance to new line
    Print(text);
    NewLine();
}

void TerminalApp::PutChar(char c) {
    // Handle newline
    if (c == '\n' || c == '\r') {
        NewLine();
        return;
    }
    
    // Ignore non-printable (except basic controls)
    if (c < 32 && c != '\t') {
        return;
    }
    
    // Handle tab as spaces
    if (c == '\t') {
        int spaces = 4 - (currentCol % 4);
        for (int i = 0; i < spaces && currentCol < COLS; i++) {
            lines[currentLine][currentCol++] = ' ';
        }
        return;
    }
    
    // Word wrap at column limit
    if (currentCol >= COLS) {
        NewLine();
    }
    
    // Write character to current position
    lines[currentLine][currentCol] = c;
    currentCol++;
    
    // Ensure null termination
    if (currentCol < COLS) {
        lines[currentLine][currentCol] = '\0';
    }
}

void TerminalApp::Print(const char* text) {
    if (!text) return;
    
    while (*text) {
        PutChar(*text);
        text++;
    }
}

void TerminalApp::OnKeyDown(char c) {
    if (c == 0) return;
    
    // ESC closes the terminal (handled by Window base class)
    if (c == 27) return;

    // Enter - execute command
    if (c == '\n' || c == '\r') {
        NewLine();
        
        if (inputLen > 0) {
            // Execute command
            MCL::ProcessCommand(inputBuffer, this);
        }
        
        Prompt();
        return;
    }
    
    // Backspace
    if (c == '\b' || c == 127) {
        if (inputLen > 0) {
            inputLen--;
            inputBuffer[inputLen] = '\0';
            
            // Visual backspace: move cursor back and clear character
            if (currentCol > 2) {  // Don't erase the "> " prompt
                currentCol--;
                lines[currentLine][currentCol] = '\0';
            }
        }
        return;
    }
    
    // Regular character input
    if (inputLen < 126 && currentCol < COLS - 1) {
        // Add to input buffer
        inputBuffer[inputLen++] = c;
        inputBuffer[inputLen] = '\0';
        
        // Display character
        lines[currentLine][currentCol] = c;
        currentCol++;
        lines[currentLine][currentCol] = '\0';
    }
}

void TerminalApp::OnRender(MorphicAPI::Graphics& g) {
    // Professional dark terminal background
    g.Clear(0xFF0A0A0A);
    
    // Draw a subtle border
    g.FillRect(0, 0, width, 2, 0xFF1A1A1A);
    g.FillRect(0, height - 2, width, 2, 0xFF1A1A1A);
    
    const int CHAR_WIDTH = 6;   // 5 pixels + 1 spacing
    const int CHAR_HEIGHT = 10; // 7 pixels + 3 spacing
    const int PADDING_X = 8;
    const int PADDING_Y = 6;

    // Render each line
    for (int row = 0; row < ROWS; row++) {
        if (lines[row][0] != '\0') {
            int y = PADDING_Y + (row * CHAR_HEIGHT);
            g.DrawText(PADDING_X, y, lines[row], 0xFF00DD00, 1);
        }
    }

    // Draw cursor (blinking effect via solid underline)
    int cursorX = PADDING_X + (currentCol * CHAR_WIDTH);
    int cursorY = PADDING_Y + (currentLine * CHAR_HEIGHT) + 8;
    g.FillRect(cursorX, cursorY, CHAR_WIDTH, 2, 0xFF00FF00);
}

void TerminalApp::DrawChar(MorphicAPI::Graphics& g, int x, int y, char c, uint32_t color) {
    g.DrawChar(x, y, c, color, 1);
}
