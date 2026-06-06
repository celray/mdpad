#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "app.h"

#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    std::vector<std::string> file_paths;
    for (int i = 1; i < argc; ++i) {
        file_paths.emplace_back(argv[i]);
    }

    // If another instance is already running, hand files to it and exit.
    if (mdpad::App::try_send_to_existing(file_paths)) {
        return EXIT_SUCCESS;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return EXIT_FAILURE;
    }

    if (!TTF_Init()) {
        SDL_Log("Failed to initialize SDL_ttf: %s", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    {
        mdpad::App app;
        if (!app.init(file_paths)) {
            SDL_Log("Failed to initialize application");
            TTF_Quit();
            SDL_Quit();
            return EXIT_FAILURE;
        }
        app.run();
    }

    TTF_Quit();
    SDL_Quit();
    return EXIT_SUCCESS;
}
