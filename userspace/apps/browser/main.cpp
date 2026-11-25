#include "browser.h"

extern "C" int main(void* args) {
    (void)args;
    MorphicBrowserApp app;
    app.Run();
    return 0;
}
