#pragma once

#include "compositor.h"
#include "mcl.h"

class Terminal : public IOutput {
public:
    static const int ROWS = 25;
    static const int COLS = 80;
    
    Terminal();
    
    void Draw(int x, int y, int w, int h);
    void OnChar(char c);
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
};
