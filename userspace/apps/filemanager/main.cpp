#include "../../sdk/morphic_api.h"
#include "filemanager.h"

extern "C" int main(void* args) {
    MorphicAPI::Window* app = new FileManagerApp();
    app->Run();
    delete app;
    return 0;
}
