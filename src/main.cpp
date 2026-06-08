#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "app.h"
#include "updater.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::printf(
        "mdpad %s - lightweight markdown viewer\n\n"
        "Usage: mdpad [options] [file.md ...]\n\n"
        "Options:\n"
        "  -v, --version       print version and exit\n"
        "  -h, --help          show this help and exit\n"
        "      --check-update  check whether a newer release is available\n"
        "      --update        download and install the latest release\n",
        mdpad::version());
}

}  // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> file_paths;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "-v") == 0 || std::strcmp(a, "--version") == 0) {
            std::printf("mdpad %s\n", mdpad::version());
            return EXIT_SUCCESS;
        }
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            print_usage();
            return EXIT_SUCCESS;
        }
        if (std::strcmp(a, "--update") == 0) {
            return mdpad::run_self_update();
        }
        if (std::strcmp(a, "--check-update") == 0) {
            return mdpad::print_update_status();
        }
        if (std::strcmp(a, "--bg-update-check") == 0) {
            return mdpad::run_background_check();
        }
        file_paths.emplace_back(a);
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
