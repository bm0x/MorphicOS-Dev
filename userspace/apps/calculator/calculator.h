#pragma once
#include "../../sdk/morphic_api.h"

class CalculatorApp : public MorphicAPI::Window {
public:
    CalculatorApp() : MorphicAPI::Window(300, 400) {
        value = 0;
        currentOp = 0;
        inputBuffer[0] = '0';
        inputBuffer[1] = 0;
        inputLen = 1;
        newNumber = true;
    }

    void OnUpdate() override {
        // Animation logic if needed
    }

    void OnRender(MorphicAPI::Graphics& g) override {
        // Background
        g.Clear(0xFF202020);

        // Header Title
        g.DrawText(10, 5, "Morphic Calc", 0xFFAAAAAA, 1);
        
        // Display Background
        g.FillRect(20, 30, width - 40, 60, 0xFF000000);
        g.FillRect(20, 30 + 60, width - 40, 2, 0xFF404040); // underline
        
        // Display Text (Right aligned simulation)
        // Calculate text width to right align
        int charW = 6 * 2; // 5+1 * scale 2
        int textW = inputLen * charW;
        int textX = (20 + width - 40) - textW - 10; // Right margin 10
        if (textX < 25) textX = 25; // Clip left

        g.DrawText(textX, 45, inputBuffer, 0xFF00FF00, 2);

        // Grid Layout
        const char* labels[] = {
            "7", "8", "9", "/",
            "4", "5", "6", "*",
            "1", "2", "3", "-",
            "C", "0", "=", "+"
        };

        int startY = 110;
        int gap = 10;
        int btnW = (width - 40 - (3 * gap)) / 4;
        int btnH = (height - startY - 20) / 4;

        for (int i = 0; i < 16; i++) {
            int col = i % 4;
            int row = i / 4;
            int bx = 20 + col * (btnW + gap);
            int by = startY + row * (btnH + gap);
            
            // Highlight press? (Requires state, skip for now)
            g.FillRect(bx, by, btnW, btnH, 0xFF404040);
            
            // Draw Label Centered
            // We use simple centering
            const char* lbl = labels[i];
            int tw = 1 * 6 * 2; // len 1
            int th = 7 * 2;
            g.DrawText(bx + (btnW - tw)/2, by + (btnH - th)/2, lbl, 0xFFFFFFFF, 2);
        }
    }

    void OnMouseDown(int x, int y, int btn) override {
        sys_debug_print("[Calc] MouseDown\n");
        // Grid Layout Recalculation
        int startY = 110;
        int gap = 10;
        int btnW = (width - 40 - (3 * gap)) / 4;
        int btnH = (height - startY - 20) / 4;

        const char* labels[] = {
            "7", "8", "9", "/",
            "4", "5", "6", "*",
            "1", "2", "3", "-",
            "C", "0", "=", "+"
        };

        for (int i = 0; i < 16; i++) {
            int col = i % 4;
            int row = i / 4;
            int bx = 20 + col * (btnW + gap);
            int by = startY + row * (btnH + gap);

            if (x >= bx && x < bx + btnW && y >= by && y < by + btnH) {
                ProcessInput(labels[i][0]);
                return;
            }
        }
    }

    void OnKeyDown(char c) override {
        // Map functionality for Enter/Esc
        if (c == 13) c = '='; // Enter -> =
        if (c == 27) c = 'C'; // Esc -> Clear

        // Allow digits and ops
        if ((c >= '0' && c <= '9') || 
            c == '+' || c == '-' || c == '*' || c == '/' || 
            c == '=' || c == 'C') {
            ProcessInput(c);
        }
    }

private:
    char inputBuffer[32];
    int inputLen;
    int64_t value;
    int currentOp;
    bool newNumber;

    void ProcessInput(char c) {
        if (c >= '0' && c <= '9') {
            if (newNumber) {
                inputBuffer[0] = c;
                inputBuffer[1] = 0;
                inputLen = 1;
                newNumber = false;
            } else {
                if (inputLen < 12) { // Limit to fit display
                    if (inputLen == 1 && inputBuffer[0] == '0') inputLen = 0;
                    inputBuffer[inputLen++] = c;
                    inputBuffer[inputLen] = 0;
                }
            }
        } else if (c == 'C') {
            value = 0;
            currentOp = 0;
            inputBuffer[0] = '0';
            inputBuffer[1] = 0;
            inputLen = 1;
        } else if (c == '=') {
            int64_t current = ParseInt(inputBuffer);
            if (currentOp == 1) value += current;
            else if (currentOp == 2) value -= current;
            else if (currentOp == 3) value *= current;
            else if (currentOp == 4 && current != 0) value /= current;
            
            FormatInt(value, inputBuffer);
            inputLen = StrLen(inputBuffer);
            currentOp = 0;
            newNumber = true;
        } else {
            value = ParseInt(inputBuffer);
            if (c == '+') currentOp = 1;
            else if (c == '-') currentOp = 2;
            else if (c == '*') currentOp = 3;
            else if (c == '/') currentOp = 4;
            newNumber = true;
        }
    }

    int64_t ParseInt(const char* s) {
        int64_t v = 0;
        int sign = 1;
        if (*s == '-') { sign = -1; s++; }
        while (*s) { v = v * 10 + (*s - '0'); s++; }
        return v * sign;
    }

    void FormatInt(int64_t v, char* buf) {
        if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
        int i = 0;
        if (v < 0) { buf[i++] = '-'; v = -v; }
        char tmp[32];
        int j = 0;
        while (v > 0) { tmp[j++] = (v % 10) + '0'; v /= 10; }
        while (j > 0) buf[i++] = tmp[--j];
        buf[i] = 0;
    }

    int StrLen(const char* s) {
        int l = 0;
        while (s[l]) l++;
        return l;
    }
};
