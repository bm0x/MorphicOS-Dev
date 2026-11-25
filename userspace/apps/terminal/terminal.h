#pragma once
#include "../../sdk/morphic_api.h"
#include "mcl.h"

class TerminalApp : public MorphicAPI::Window, public IOutput {
public:
    static const int ROWS = 20;
    static const int COLS = 74;

    TerminalApp();

    void OnRender(MorphicAPI::Graphics& g) override;
    void OnKeyDown(char c) override;
    void OnUpdate() override {}

    void Print(const char* text) override;
    void PrintLine(const char* text);
    void ClearScreen();

private:
    char lines[ROWS][COLS + 1];
    uint32_t lineColors[ROWS];
    uint32_t activeColor;
    int currentLine;
    int currentCol;

    char cwd[64];
    char inputBuffer[128];
    int inputLen;

    void Scroll();
    void NewLine();
    void PutChar(char c);
    void Prompt();
    void ExecuteCommand(const char* cmd);
    void BuildFullPath(const char* arg, char* out, int maxL);
    void PrintU64(uint64_t v);
    void DrawChar(MorphicAPI::Graphics& g, int x, int y, char c, uint32_t color);
};
