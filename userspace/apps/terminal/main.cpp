#include "../../sdk/morphic_api.h"
#include "terminal.h"

extern "C" int main(void* args) {
    MorphicAPI::Window* app = new TerminalApp();
    app->Run();
    delete app;
    return 0;
}
