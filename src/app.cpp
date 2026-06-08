#include "app.h"

#include "html_export.h"
#include "updater.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <fstream>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#ifdef __linux__
#include <sys/inotify.h>
#endif

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace mdpad {

static constexpr int kDefaultWidth = 900;
static constexpr int kDefaultHeight = 700;
static constexpr const char* kWindowTitle = "mdpad";
static constexpr int kTabBarHeight = 36;
static constexpr float kTabPadding = 14.0f;
static constexpr float kTabCloseSize = 14.0f;
static constexpr float kTabGap = 1.0f;
static constexpr uint64_t kReloadDebounceMs = 150;

// Tab bar colors
static constexpr SDL_Color kTabBarBg = {235, 235, 235, 255};
static constexpr SDL_Color kTabActiveBg = {255, 255, 255, 255};
static constexpr SDL_Color kTabInactiveBg = {225, 225, 225, 255};
static constexpr SDL_Color kTabTextColor = {60, 60, 60, 255};
static constexpr SDL_Color kTabCloseColor = {120, 120, 120, 255};
static constexpr SDL_Color kTabBorderColor = {210, 210, 210, 255};

// ---------------------------------------------------------------------------
// IPC helpers (Unix only)
// ---------------------------------------------------------------------------

std::string App::socket_path() {
#ifndef _WIN32
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg) {
        return std::string(xdg) + "/mdpad.sock";
    }
    return "/tmp/mdpad-" + std::to_string(getuid()) + ".sock";
#else
    return {};
#endif
}

bool App::try_send_to_existing(const std::vector<std::string>& paths) {
#ifndef _WIN32
    std::string sock = socket_path();
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sock.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    // Protocol: one line per command.  "RAISE" brings the window forward;
    // anything else is treated as a file path to open.
    std::string msg = "RAISE\n";
    for (const auto& p : paths) {
        // Resolve to absolute path so the receiver can find the file
        // regardless of working directory.
        char* abs = realpath(p.c_str(), nullptr);
        if (abs) {
            msg += std::string(abs) + "\n";
            free(abs);
        } else {
            msg += p + "\n";
        }
    }

    // Small message, single write is fine for a local socket.
    ssize_t written = write(fd, msg.data(), msg.size());
    close(fd);
    return written > 0;
#else
    (void)paths;
    return false;
#endif
}

void App::setup_ipc_listener() {
#ifndef _WIN32
    std::string sock = socket_path();

    // Remove stale socket left behind by a crashed instance.
    unlink(sock.c_str());

    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return;

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sock.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
             sizeof(addr)) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    listen(listen_fd_, 5);

    // Non-blocking so we can poll from the event loop without stalling.
    int flags = fcntl(listen_fd_, F_GETFL, 0);
    fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);
#endif
}

void App::check_ipc() {
#ifndef _WIN32
    if (listen_fd_ < 0) return;

    int client = accept(listen_fd_, nullptr, nullptr);
    if (client < 0) return;  // EAGAIN — nothing pending

    char buf[4096];
    ssize_t n = read(client, buf, sizeof(buf) - 1);
    close(client);

    if (n <= 0) return;
    buf[n] = '\0';

    std::istringstream ss(buf);
    std::string line;
    while (std::getline(ss, line)) {
        if (line == "RAISE") {
            SDL_RaiseWindow(window_);
        } else if (!line.empty()) {
            open_tab(line);
        }
    }
#endif
}

void App::cleanup_ipc() {
#ifndef _WIN32
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        unlink(socket_path().c_str());
        listen_fd_ = -1;
    }
#endif
}

// ---------------------------------------------------------------------------
// File watching (inotify)
// ---------------------------------------------------------------------------

void App::setup_file_watcher() {
#ifdef __linux__
    inotify_fd_ = inotify_init1(IN_NONBLOCK);
    if (inotify_fd_ < 0) {
        SDL_Log("inotify_init failed — auto-reload disabled");
    }
#endif
}

void App::cleanup_file_watcher() {
#ifdef __linux__
    if (inotify_fd_ >= 0) {
        // Watches are removed automatically when the fd is closed.
        close(inotify_fd_);
        inotify_fd_ = -1;
    }
#endif
}

void App::add_file_watch(int tab_index) {
#ifdef __linux__
    if (inotify_fd_ < 0) return;
    if (tab_index < 0 || tab_index >= static_cast<int>(tabs_.size())) return;

    auto& tab = tabs_[tab_index];
    // IN_CLOSE_WRITE: normal save.  IN_MODIFY: partial writes.
    // IN_MOVE_SELF / IN_DELETE_SELF: editors that atomic-save (vim, etc.)
    tab.watch_fd = inotify_add_watch(
        inotify_fd_, tab.file_path.c_str(),
        IN_CLOSE_WRITE | IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF);
#else
    (void)tab_index;
#endif
}

void App::remove_file_watch(int tab_index) {
#ifdef __linux__
    if (inotify_fd_ < 0) return;
    if (tab_index < 0 || tab_index >= static_cast<int>(tabs_.size())) return;

    auto& tab = tabs_[tab_index];
    if (tab.watch_fd >= 0) {
        inotify_rm_watch(inotify_fd_, tab.watch_fd);
        tab.watch_fd = -1;
    }
#else
    (void)tab_index;
#endif
}

void App::check_file_changes() {
#ifdef __linux__
    if (inotify_fd_ < 0) return;

    // Read all pending events.
    alignas(struct inotify_event) char buf[4096];
    for (;;) {
        ssize_t len = read(inotify_fd_, buf, sizeof(buf));
        if (len <= 0) break;  // EAGAIN or error

        const char* ptr = buf;
        while (ptr < buf + len) {
            const auto* ev = reinterpret_cast<const struct inotify_event*>(ptr);

            // Find the tab that owns this watch descriptor.
            for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
                if (tabs_[i].watch_fd != ev->wd) continue;

                if (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
                    // Editor did an atomic save (delete + recreate or rename).
                    // The old watch is auto-removed; try to re-add after a
                    // short delay so the new file is in place.
                    tabs_[i].watch_fd = -1;
                    tabs_[i].reload_at = SDL_GetTicks() + kReloadDebounceMs;
                } else {
                    // Normal modification — schedule a debounced reload.
                    tabs_[i].reload_at = SDL_GetTicks() + kReloadDebounceMs;
                }
                break;
            }

            ptr += sizeof(struct inotify_event) + ev->len;
        }
    }
#endif
}

void App::process_pending_reloads() {
    uint64_t now = SDL_GetTicks();
    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        if (tabs_[i].reload_at == 0) continue;
        if (now < tabs_[i].reload_at) continue;

        tabs_[i].reload_at = 0;
        reload_tab(i);

        // Re-add watch if it was removed (atomic save).
        if (tabs_[i].watch_fd < 0) {
            add_file_watch(i);
        }
    }
}

void App::reload_tab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;

    auto& tab = tabs_[index];
    Document doc;
    if (!parser_.parse_file(tab.file_path, doc)) return;

    tab.document = std::move(doc);

    // If this is the active tab, refresh the renderer (preserve scroll).
    if (index == active_tab_) {
        float saved_scroll = renderer_.get_scroll_y();
        renderer_.set_document(tab.document);
        renderer_.set_scroll_y(saved_scroll);
    }
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------

App::App() = default;

App::~App() {
    cleanup_file_watcher();
    cleanup_ipc();
    for (auto& tab : tabs_) {
        if (tab.title_texture) {
            SDL_DestroyTexture(tab.title_texture);
        }
    }
    if (ui_font_) {
        TTF_CloseFont(ui_font_);
    }
    if (sdl_renderer_) {
        SDL_DestroyRenderer(sdl_renderer_);
    }
    if (window_) {
        SDL_DestroyWindow(window_);
    }
}

bool App::init(const std::vector<std::string>& file_paths) {
    window_ = SDL_CreateWindow(kWindowTitle, kDefaultWidth, kDefaultHeight,
                               SDL_WINDOW_RESIZABLE);
    if (!window_) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return false;
    }

    sdl_renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!sdl_renderer_) {
        SDL_Log("Failed to create renderer: %s", SDL_GetError());
        return false;
    }

    const char* base_path = SDL_GetBasePath();
    if (base_path) {
        font_dir_ = std::string(base_path) + "assets/fonts";
    } else {
        font_dir_ = "assets/fonts";
    }

    if (!renderer_.init(sdl_renderer_, font_dir_)) {
        SDL_Log("Failed to initialize renderer");
        return false;
    }

    std::string ui_font_path = font_dir_ + "/JetBrainsMonoNerdFont-Regular.ttf";
    ui_font_ = TTF_OpenFont(ui_font_path.c_str(), 13.0f);
    if (!ui_font_) {
        SDL_Log("Failed to load UI font: %s", SDL_GetError());
        return false;
    }

    setup_ipc_listener();
    setup_file_watcher();

    // Surface any update found by a previous background check, then kick off
    // a fresh (throttled, detached) check for next time.
    update_version_ = cached_update_version();
    maybe_spawn_update_check();

    // Text I-beam cursor over the document area.  We don't bother
    // swapping based on hover zone — the tab bar is a small strip and
    // an I-beam there is acceptable.
    SDL_Cursor* ibeam = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    if (ibeam) SDL_SetCursor(ibeam);

    for (const auto& path : file_paths) {
        open_tab(path);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------

static std::string filename_from_path(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    if (pos != std::string::npos) {
        return path.substr(pos + 1);
    }
    return path;
}

void App::open_tab(const std::string& path) {
    // If already open, just switch to it
    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        if (tabs_[i].file_path == path) {
            switch_tab(i);
            return;
        }
    }

    Document doc;
    if (!parser_.parse_file(path, doc)) {
        SDL_Log("Failed to parse file: %s", path.c_str());
        return;
    }

    Tab tab;
    tab.file_path = path;
    tab.title = filename_from_path(path);
    tab.document = std::move(doc);
    tab.scroll_y = 0.0f;

    SDL_Surface* surf = TTF_RenderText_Blended(ui_font_, tab.title.c_str(), 0,
                                                kTabTextColor);
    if (surf) {
        tab.title_texture = SDL_CreateTextureFromSurface(sdl_renderer_, surf);
        tab.title_width = surf->w;
        tab.title_height = surf->h;
        SDL_DestroySurface(surf);
    }

    tabs_.push_back(std::move(tab));
    int new_index = static_cast<int>(tabs_.size()) - 1;
    add_file_watch(new_index);
    switch_tab(new_index);
}

void App::close_tab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;

    remove_file_watch(index);
    if (tabs_[index].title_texture) {
        SDL_DestroyTexture(tabs_[index].title_texture);
    }
    tabs_.erase(tabs_.begin() + index);

    if (tabs_.empty()) {
        active_tab_ = -1;
        renderer_.set_document(Document{});
        SDL_SetWindowTitle(window_, kWindowTitle);
        return;
    }

    if (active_tab_ >= static_cast<int>(tabs_.size())) {
        active_tab_ = static_cast<int>(tabs_.size()) - 1;
    }
    switch_tab(active_tab_);
}

void App::switch_tab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;

    if (active_tab_ >= 0 && active_tab_ < static_cast<int>(tabs_.size())) {
        tabs_[active_tab_].scroll_y = renderer_.get_scroll_y();
    }

    active_tab_ = index;
    renderer_.set_document(tabs_[index].document);
    renderer_.set_scroll_y(tabs_[index].scroll_y);
    update_window_title();
}

void App::update_window_title() {
    std::string title = kWindowTitle;
    if (active_tab_ >= 0 && active_tab_ < static_cast<int>(tabs_.size())) {
        title += " - " + tabs_[active_tab_].title;
    }
    if (!update_version_.empty()) {
        title += "  (update " + update_version_ + " available, run: mdpad --update)";
    }
    SDL_SetWindowTitle(window_, title.c_str());
}

void App::file_dialog_callback(void* userdata, const char* const* filelist,
                                int /*filter*/) {
    auto* app = static_cast<App*>(userdata);
    if (filelist && filelist[0]) {
        app->open_tab(filelist[0]);
    }
}

void App::print_active_tab() {
    if (active_tab_ < 0 || active_tab_ >= static_cast<int>(tabs_.size())) {
        return;
    }
    const auto& tab = tabs_[active_tab_];

    std::string html =
        document_to_html(tab.document, renderer_.theme(), tab.title);

    // Write to a predictable per-user path — overwrites any previous
    // print dump, which is fine: the browser opens it once, fires the
    // print dialog, and is done.
    char* pref = SDL_GetPrefPath(nullptr, "mdpad");
    std::string path;
    if (pref && *pref) {
        path = std::string(pref) + "print.html";
        SDL_free(pref);
    } else {
        if (pref) SDL_free(pref);
        path = "/tmp/mdpad-print.html";
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        SDL_Log("Print: failed to open %s for writing", path.c_str());
        return;
    }
    out << html;
    out.close();

    // Hand off to the user's default browser.  SDL_OpenURL takes a URL;
    // for a local file we need the file:// scheme.
    std::string url = "file://" + path;
    if (!SDL_OpenURL(url.c_str())) {
        SDL_Log("Print: SDL_OpenURL failed: %s", SDL_GetError());
    }
}

void App::open_file_dialog() {
    static const SDL_DialogFileFilter filters[] = {
        {"Markdown files", "md;markdown;txt"},
        {"All files", "*"},
    };

    SDL_ShowOpenFileDialog(file_dialog_callback, this, window_, filters, 2,
                           nullptr, false);
}

// ---------------------------------------------------------------------------
// Tab bar rendering
// ---------------------------------------------------------------------------

int App::tab_bar_height() const {
    if (tabs_.size() <= 1) return 0;
    return kTabBarHeight;
}

int App::tab_hit_test(float x, float y) const {
    if (y < 0 || y >= static_cast<float>(tab_bar_height())) return -1;
    for (int i = 0; i < static_cast<int>(tab_rects_.size()); ++i) {
        if (x >= tab_rects_[i].x && x < tab_rects_[i].x + tab_rects_[i].w) {
            return i;
        }
    }
    return -1;
}

bool App::tab_close_hit_test(int tab_index, float x, float y) const {
    if (tab_index < 0 || tab_index >= static_cast<int>(tab_rects_.size())) {
        return false;
    }
    const auto& r = tab_rects_[tab_index];
    float cx = r.close_x;
    float cy = (static_cast<float>(kTabBarHeight) - kTabCloseSize) / 2.0f;
    return x >= cx && x <= cx + kTabCloseSize &&
           y >= cy && y <= cy + kTabCloseSize;
}

void App::render_tab_bar(int window_width) {
    if (tabs_.size() <= 1) {
        tab_rects_.clear();
        return;
    }

    SDL_FRect bar = {0, 0, static_cast<float>(window_width),
                     static_cast<float>(kTabBarHeight)};
    SDL_SetRenderDrawColor(sdl_renderer_, kTabBarBg.r, kTabBarBg.g,
                           kTabBarBg.b, 255);
    SDL_RenderFillRect(sdl_renderer_, &bar);

    SDL_FRect border = {0, static_cast<float>(kTabBarHeight - 1),
                        static_cast<float>(window_width), 1.0f};
    SDL_SetRenderDrawColor(sdl_renderer_, kTabBorderColor.r, kTabBorderColor.g,
                           kTabBorderColor.b, 255);
    SDL_RenderFillRect(sdl_renderer_, &border);

    tab_rects_.resize(tabs_.size());
    float x = 4.0f;
    float tab_y = 4.0f;
    float tab_h = static_cast<float>(kTabBarHeight) - 4.0f;

    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        const auto& tab = tabs_[i];
        bool active = (i == active_tab_);

        float tab_w = kTabPadding + static_cast<float>(tab.title_width) +
                      kTabPadding + kTabCloseSize + kTabPadding;
        float min_w = 80.0f;
        if (tab_w < min_w) tab_w = min_w;

        SDL_FRect tab_rect = {x, tab_y, tab_w, tab_h};
        SDL_Color bg = active ? kTabActiveBg : kTabInactiveBg;
        SDL_SetRenderDrawColor(sdl_renderer_, bg.r, bg.g, bg.b, 255);
        SDL_RenderFillRect(sdl_renderer_, &tab_rect);

        if (active) {
            SDL_FRect conn = {x, static_cast<float>(kTabBarHeight - 1),
                              tab_w, 1.0f};
            SDL_SetRenderDrawColor(sdl_renderer_, kTabActiveBg.r,
                                   kTabActiveBg.g, kTabActiveBg.b, 255);
            SDL_RenderFillRect(sdl_renderer_, &conn);
        }

        if (tab.title_texture) {
            float text_y = tab_y + (tab_h - static_cast<float>(tab.title_height)) / 2.0f;
            SDL_FRect dst = {x + kTabPadding, text_y,
                             static_cast<float>(tab.title_width),
                             static_cast<float>(tab.title_height)};
            SDL_RenderTexture(sdl_renderer_, tab.title_texture, nullptr, &dst);
        }

        float close_x = x + tab_w - kTabPadding - kTabCloseSize;
        float close_y = tab_y + (tab_h - kTabCloseSize) / 2.0f;
        SDL_SetRenderDrawColor(sdl_renderer_, kTabCloseColor.r,
                               kTabCloseColor.g, kTabCloseColor.b, 255);
        float m = 3.0f;
        SDL_RenderLine(sdl_renderer_, close_x + m, close_y + m,
                       close_x + kTabCloseSize - m, close_y + kTabCloseSize - m);
        SDL_RenderLine(sdl_renderer_, close_x + kTabCloseSize - m, close_y + m,
                       close_x + m, close_y + kTabCloseSize - m);

        tab_rects_[i].x = x;
        tab_rects_[i].w = tab_w;
        tab_rects_[i].close_x = close_x;

        x += tab_w + kTabGap;
    }
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void App::run() {
    running_ = true;

    while (running_) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            handle_event(event);
        }

        check_ipc();
        check_file_changes();
        process_pending_reloads();
        update();
        render();
        SDL_Delay(4);
    }
}

void App::handle_event(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        break;

    case SDL_EVENT_KEY_DOWN:
        if (event.key.key == SDLK_ESCAPE) {
            if (renderer_.has_selection()) {
                renderer_.clear_selection();
            } else {
                running_ = false;
            }
        }
        if (event.key.key == SDLK_O && (event.key.mod & SDL_KMOD_CTRL)) {
            open_file_dialog();
        }
        if (event.key.key == SDLK_P && (event.key.mod & SDL_KMOD_CTRL)) {
            print_active_tab();
        }
        if (event.key.key == SDLK_W && (event.key.mod & SDL_KMOD_CTRL)) {
            if (active_tab_ >= 0) {
                close_tab(active_tab_);
            }
        }
        if (event.key.key == SDLK_C && (event.key.mod & SDL_KMOD_CTRL)) {
            if (renderer_.has_selection()) {
                std::string text = renderer_.selection_text();
                if (!text.empty()) {
                    SDL_SetClipboardText(text.c_str());
                }
            }
        }
        if (event.key.key == SDLK_A && (event.key.mod & SDL_KMOD_CTRL)) {
            renderer_.select_all();
        }
        {
            bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
            bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
            switch (event.key.key) {
            case SDLK_LEFT:
                if (ctrl) renderer_.caret_move_word(false, shift);
                else renderer_.caret_move_char(false, shift);
                break;
            case SDLK_RIGHT:
                if (ctrl) renderer_.caret_move_word(true, shift);
                else renderer_.caret_move_char(true, shift);
                break;
            case SDLK_UP:
                renderer_.caret_move_line(false, shift);
                break;
            case SDLK_DOWN:
                renderer_.caret_move_line(true, shift);
                break;
            case SDLK_HOME:
                if (ctrl) renderer_.caret_move_doc_edge(false, shift);
                else renderer_.caret_move_line_edge(false, shift);
                break;
            case SDLK_END:
                if (ctrl) renderer_.caret_move_doc_edge(true, shift);
                else renderer_.caret_move_line_edge(true, shift);
                break;
            default:
                break;
            }
        }
        if (event.key.key == SDLK_TAB && (event.key.mod & SDL_KMOD_CTRL)) {
            if (!tabs_.empty()) {
                int next;
                if (event.key.mod & SDL_KMOD_SHIFT) {
                    next = (active_tab_ - 1 + static_cast<int>(tabs_.size())) %
                           static_cast<int>(tabs_.size());
                } else {
                    next = (active_tab_ + 1) % static_cast<int>(tabs_.size());
                }
                switch_tab(next);
            }
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_LEFT) {
            float mx = event.button.x;
            float my = event.button.y;
            int hit = tab_hit_test(mx, my);
            if (hit >= 0) {
                if (tab_close_hit_test(hit, mx, my)) {
                    close_tab(hit);
                } else {
                    switch_tab(hit);
                }
            } else if (my >= static_cast<float>(tab_bar_height())) {
                float doc_mx = mx;
                float doc_my = my - static_cast<float>(tab_bar_height());
                int clicks = event.button.clicks;
                bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                if (clicks >= 3) {
                    renderer_.select_line_at(doc_mx, doc_my);
                } else if (clicks == 2) {
                    renderer_.select_word_at(doc_mx, doc_my);
                } else if (shift) {
                    renderer_.extend_selection_to(doc_mx, doc_my);
                } else {
                    renderer_.begin_selection(doc_mx, doc_my);
                }
            }
        }
        if (event.button.button == SDL_BUTTON_MIDDLE) {
            int hit = tab_hit_test(event.button.x, event.button.y);
            if (hit >= 0) {
                close_tab(hit);
            }
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        if (renderer_.is_selecting()) {
            float mx = event.motion.x;
            float my = event.motion.y - static_cast<float>(tab_bar_height());
            renderer_.update_selection(mx, my);
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_LEFT &&
            renderer_.is_selecting()) {
            renderer_.end_selection();
        }
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        renderer_.scroll(event.wheel.y);
        break;

    case SDL_EVENT_WINDOW_RESIZED:
        break;

    default:
        break;
    }
}

void App::update() {
    // Future: animations, etc.
}

void App::render() {
    const SDL_Color& bg = renderer_.theme().background;
    SDL_SetRenderDrawColor(sdl_renderer_, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(sdl_renderer_);

    int w, h;
    SDL_GetCurrentRenderOutputSize(sdl_renderer_, &w, &h);

    int tbh = tab_bar_height();
    render_tab_bar(w);

    if (tbh > 0) {
        SDL_Rect viewport = {0, tbh, w, h - tbh};
        SDL_SetRenderViewport(sdl_renderer_, &viewport);
    }

    renderer_.render(w, h - tbh);

    if (tbh > 0) {
        SDL_SetRenderViewport(sdl_renderer_, nullptr);
    }

    SDL_RenderPresent(sdl_renderer_);
}

}  // namespace mdpad
