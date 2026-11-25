#pragma once
#include "../../sdk/morphic_api.h"
#include "../../sdk/morphic_ui.h"

class CalculatorApp : public MorphicAPI::Window {
public:
    CalculatorApp() : MorphicAPI::Window(320, 440) {
        value = 0;
        currentOp = 0;
        inputBuffer[0] = '0';
        inputBuffer[1] = 0;
        inputLen = 1;
        newNumber = true;
    }

    void OnUpdate() override {}

    void OnMouseMove(int x, int y) override {
        (void)x; (void)y;
        Invalidate();
    }

    void OnRender(MorphicAPI::Graphics& g) override {
        using namespace MorphicUI;
        DrawWindowBackdrop(g);
        DrawHeaderBar(g, "Calculator", nullptr, 42);

        // Glass Display Card
        Rect disp = {18, 56, (int)width - 36, 72};
        DrawCard(g, disp, Theme::RADIUS_MD, 0xFF0C1322);

        int tw = MorphicAPI::Graphics::MeasureTextWidth(inputBuffer, 2);
        int textX = disp.x + disp.w - tw - 14;
        if (textX < disp.x + 12) textX = disp.x + 12;
        g.DrawText(textX, disp.y + 20, inputBuffer, Theme::ACCENT_CYAN, 2);

        const char* labels[] = {
            "7", "8", "9", "/",
            "4", "5", "6", "*",
            "1", "2", "3", "-",
            "C", "0", "=", "+"
        };

        int startY = 144;
        int gap = 10;
        int btnW = ((int)width - 36 - (3 * gap)) / 4;
        int btnH = ((int)height - startY - 18 - (3 * gap)) / 4;

        for (int i = 0; i < 16; i++) {
            int col = i % 4;
            int row = i / 4;
            Rect r = {18 + col * (btnW + gap), startY + row * (btnH + gap), btnW, btnH};
            char ch = labels[i][0];
            ButtonStyle st = ButtonStyle::SECONDARY;
            if (ch == 'C') st = ButtonStyle::DANGER;
            else if (ch == '=') st = ButtonStyle::SUCCESS;
            else if (ch == '/' || ch == '*' || ch == '-' || ch == '+') st = ButtonStyle::PRIMARY;

            DrawButton(g, r, labels[i], st, r.Contains(mouseX, mouseY));
        }
    }

    void OnMouseDown(int x, int y, int btn) override {
        (void)btn;
        const char* labels[] = {
            "7", "8", "9", "/",
            "4", "5", "6", "*",
            "1", "2", "3", "-",
            "C", "0", "=", "+"
        };

        int startY = 144;
        int gap = 10;
        int btnW = ((int)width - 36 - (3 * gap)) / 4;
        int btnH = ((int)height - startY - 18 - (3 * gap)) / 4;

        for (int i = 0; i < 16; i++) {
            int col = i % 4;
            int row = i / 4;
            MorphicUI::Rect r = {18 + col * (btnW + gap), startY + row * (btnH + gap), btnW, btnH};
            if (r.Contains(x, y)) {
                ProcessInput(labels[i][0]);
                Invalidate();
                return;
            }
        }
    }

    void OnKeyDown(char c) override {
        if (c == 13) c = '=';
        if (c == 27) c = 'C';
        if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '*' || c == '/' || c == '=' || c == 'C') {
            ProcessInput(c);
            Invalidate();
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
            } else if (inputLen < 12) {
                if (inputLen == 1 && inputBuffer[0] == '0') inputLen = 0;
                inputBuffer[inputLen++] = c;
                inputBuffer[inputLen] = 0;
            }
        } else if (c == 'C') {
            value = 0;
            currentOp = 0;
            inputBuffer[0] = '0';
            inputBuffer[1] = 0;
            inputLen = 1;
            newNumber = true;
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
        int64_t v = 0, sign = 1;
        if (*s == '-') { sign = -1; s++; }
        while (*s) { v = v * 10 + (*s - '0'); s++; }
        return v * sign;
    }

    void FormatInt(int64_t v, char* buf) {
        if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
        int i = 0;
        if (v < 0) { buf[i++] = '-'; v = -v; }
        char tmp[32]; int j = 0;
        while (v > 0) { tmp[j++] = (char)((v % 10) + '0'); v /= 10; }
        while (j > 0) buf[i++] = tmp[--j];
        buf[i] = 0;
    }

    int StrLen(const char* s) { int l = 0; while (s[l]) l++; return l; }
};
