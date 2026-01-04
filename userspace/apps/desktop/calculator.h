#pragma once

#include "compositor.h"

class Calculator {
public:
    Calculator() {
        value = 0;
        currentOp = 0;
        inputBuffer[0] = '0';
        inputBuffer[1] = 0;
        inputLen = 1;
    }

    void Draw(int x, int y, int w, int h) {
        // Background
        Compositor::DrawRect(x, y, w, h, 0xFF303030);

        // Display
        Compositor::DrawRect(x + 10, y + 10, w - 20, 40, 0xFF000000);
        Compositor::DrawText(x + 20, y + 22, inputBuffer, 0xFF00FF00, 2);

        // Buttons (Grid)
        const char* labels[] = {
            "7", "8", "9", "/",
            "4", "5", "6", "*",
            "1", "2", "3", "-",
            "C", "0", "=", "+"
        };

        int btnW = (w - 50) / 4;
        int btnH = (h - 70) / 4;
        
        for (int i = 0; i < 16; i++) {
            int col = i % 4;
            int row = i / 4;
            int bx = x + 10 + col * (btnW + 10);
            int by = y + 60 + row * (btnH + 10);
            
            Compositor::DrawRect(bx, by, btnW, btnH, 0xFF505050);
            Compositor::DrawText(bx + btnW/2 - 4, by + btnH/2 - 8, labels[i], 0xFFFFFFFF, 1);
        }
    }

    void OnClick(int x, int y, int winX, int winY, int w, int h) {
        // Check buttons
        int btnW = (w - 50) / 4;
        int btnH = (h - 70) / 4;

        for (int i = 0; i < 16; i++) {
            int col = i % 4;
            int row = i / 4;
            int bx = winX + 10 + col * (btnW + 10);
            int by = winY + 60 + row * (btnH + 10);

            if (x >= bx && x < bx + btnW && y >= by && y < by + btnH) {
                HandleInput(i);
                return;
            }
        }
    }

private:
    char inputBuffer[32];
    int inputLen;
    int64_t value;
    int currentOp; // 0:None, 1:+, 2:-, 3:*, 4:/
    bool newNumber;

    void HandleInput(int btnIdx) {
        const char* labels[] = {
            "7", "8", "9", "/",
            "4", "5", "6", "*",
            "1", "2", "3", "-",
            "C", "0", "=", "+"
        };
        char c = labels[btnIdx][0];

        if (c >= '0' && c <= '9') {
            if (newNumber) {
                inputBuffer[0] = c;
                inputBuffer[1] = 0;
                inputLen = 1;
                newNumber = false;
            } else {
                if (inputLen < 30) {
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
            // Operator
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
