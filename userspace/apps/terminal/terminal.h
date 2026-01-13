#pragma once
#include "../../sdk/morphic_api.h"
#include "mcl.h"

class TerminalApp : public MorphicAPI::Window, public IOutput {
public:
    static const int ROWS = 25;
    static const int COLS = 80;

    TerminalApp();
    
    // Window Overrides
    void OnRender(MorphicAPI::Graphics& g) override;
    void OnKeyDown(char c) override;
    void OnUpdate() override {} // No idle animation

    // IOutput Overrides
    void Print(const char* text) override;

private:
    char lines[ROWS][COLS + 1];
    int currentLine;
    int currentCol;
    
    char inputBuffer[128];
    int inputLen;

    void Scroll();
    void NewLine();
    void PutChar(char c);
    void Prompt();
    
    // Font Helper
    void DrawChar(MorphicAPI::Graphics& g, int x, int y, char c, uint32_t color);
};
