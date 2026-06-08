#pragma once

#include "markdown_parser.h"
#include "renderer.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <string>
#include <vector>

namespace mdpad {

struct Tab {
    std::string file_path;
    std::string title;
    Document document;
    float scroll_y = 0.0f;
    SDL_Texture* title_texture = nullptr;
    int title_width = 0;
    int title_height = 0;

    // File watching
    int watch_fd = -1;         // inotify watch descriptor
    uint64_t reload_at = 0;    // tick when we should reload (0 = not pending)
};

class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init(const std::vector<std::string>& file_paths);
    void run();

    // Single-instance IPC: try to hand off files to an already-running
    // instance.  Returns true if another instance accepted them (caller
    // should exit).
    static bool try_send_to_existing(const std::vector<std::string>& paths);
    static std::string socket_path();

private:
    void handle_event(const SDL_Event& event);
    void update();
    void render();

    void open_tab(const std::string& path);
    void close_tab(int index);
    void switch_tab(int index);
    void update_window_title();
    void open_file_dialog();
    void print_active_tab();

    static void file_dialog_callback(void* userdata,
                                     const char* const* filelist, int filter);

    // Tab bar
    int tab_bar_height() const;
    void render_tab_bar(int window_width);
    int tab_hit_test(float x, float y) const;
    bool tab_close_hit_test(int tab_index, float x, float y) const;

    // IPC listener
    void setup_ipc_listener();
    void check_ipc();
    void cleanup_ipc();

    // File watching (inotify)
    void setup_file_watcher();
    void cleanup_file_watcher();
    void add_file_watch(int tab_index);
    void remove_file_watch(int tab_index);
    void check_file_changes();
    void process_pending_reloads();
    void reload_tab(int index);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* sdl_renderer_ = nullptr;
    bool running_ = false;
    std::string font_dir_;

    Renderer renderer_;
    MarkdownParser parser_;

    std::vector<Tab> tabs_;
    int active_tab_ = -1;
    TTF_Font* ui_font_ = nullptr;

    // Cached tab geometry for hit testing
    struct TabRect {
        float x, w;
        float close_x;
    };
    std::vector<TabRect> tab_rects_;

    // IPC
    int listen_fd_ = -1;

    // File watcher
    int inotify_fd_ = -1;

    // Non-empty when a background check found a newer release available;
    // surfaced in the window title.
    std::string update_version_;
};

}  // namespace mdpad
