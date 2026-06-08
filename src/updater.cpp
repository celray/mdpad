#include "updater.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef MDPAD_VERSION
#define MDPAD_VERSION "0.0.0"
#endif
#ifndef MDPAD_REPO
#define MDPAD_REPO "celray/mdpad"
#endif

namespace mdpad {

namespace {

const char* kInstallUrl =
    "https://raw.githubusercontent.com/" MDPAD_REPO "/master/install.sh";

// Run a shell command and capture its stdout.
std::string capture(const std::string& cmd) {
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    std::array<char, 4096> buf;
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), p)) > 0) {
        out.append(buf.data(), n);
    }
    pclose(p);
    return out;
}

bool have_tool(const char* name) {
    std::string cmd = "command -v ";
    cmd += name;
    cmd += " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

// A curl/wget invocation that prints the URL's body to stdout, or "" if no
// downloader is available.
std::string fetch_cmd(const std::string& url) {
    if (have_tool("curl")) return "curl -fsSL '" + url + "'";
    if (have_tool("wget")) return "wget -qO- '" + url + "'";
    return std::string();
}

// Pull the first "tag_name": "vX.Y" value out of a releases API response.
std::string parse_tag(const std::string& json) {
    const std::string key = "\"tag_name\"";
    size_t k = json.find(key);
    if (k == std::string::npos) return std::string();
    size_t colon = json.find(':', k + key.size());
    if (colon == std::string::npos) return std::string();
    size_t q1 = json.find('"', colon);
    if (q1 == std::string::npos) return std::string();
    size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return std::string();
    std::string tag = json.substr(q1 + 1, q2 - q1 - 1);
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
    return tag;
}

std::string cache_dir() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    std::string base;
    if (xdg && xdg[0]) {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        if (!home || !home[0]) return std::string();
        base = std::string(home) + "/.cache";
    }
    std::string dir = base + "/mdpad";
    mkdir(base.c_str(), 0700);
    mkdir(dir.c_str(), 0700);
    return dir;
}

std::string read_file(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::string out;
    std::array<char, 1024> buf;
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0)
        out.append(buf.data(), n);
    std::fclose(f);
    return out;
}

void write_file(const std::string& path, const std::string& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\n' || s[a] == '\r' || s[a] == '\t'))
        a++;
    while (b > a &&
           (s[b - 1] == ' ' || s[b - 1] == '\n' || s[b - 1] == '\r' ||
            s[b - 1] == '\t'))
        b--;
    return s.substr(a, b - a);
}

// Absolute path to this executable (resolves the ~/.local/bin symlink).
std::string self_exe() {
    std::array<char, 4096> buf;
    ssize_t n = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (n <= 0) return std::string();
    buf[n] = '\0';
    return std::string(buf.data());
}

std::string parent_dir(const std::string& p) {
    size_t slash = p.find_last_of('/');
    return (slash == std::string::npos) ? std::string() : p.substr(0, slash);
}

}  // namespace

const char* version() { return MDPAD_VERSION; }
const char* repo() { return MDPAD_REPO; }

int version_compare(const std::string& a, const std::string& b) {
    auto part = [](const std::string& s, size_t& i) -> long {
        long v = 0;
        bool any = false;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (s[i] - '0');
            i++;
            any = true;
        }
        while (i < s.size() && s[i] != '.') i++;  // skip pre-release suffixes
        if (i < s.size() && s[i] == '.') i++;
        (void)any;
        return v;
    };
    size_t ia = 0, ib = 0;
    for (int k = 0; k < 6; k++) {
        bool a_done = ia >= a.size();
        bool b_done = ib >= b.size();
        if (a_done && b_done) break;
        long va = a_done ? 0 : part(a, ia);
        long vb = b_done ? 0 : part(b, ib);
        if (va != vb) return va < vb ? -1 : 1;
    }
    return 0;
}

std::string fetch_latest_version() {
    std::string url =
        "https://api.github.com/repos/" + std::string(MDPAD_REPO) +
        "/releases/latest";
    std::string cmd = fetch_cmd(url);
    if (cmd.empty()) return std::string();
    return parse_tag(capture(cmd));
}

int run_self_update() {
    std::printf("mdpad %s: checking for updates...\n", MDPAD_VERSION);
    std::string latest = fetch_latest_version();
    if (latest.empty()) {
        std::fprintf(stderr,
                     "mdpad: could not reach the release server (need curl or "
                     "wget and a network connection)\n");
        return 1;
    }
    if (version_compare(latest, MDPAD_VERSION) <= 0) {
        std::printf("Already up to date (latest release is %s).\n",
                    latest.c_str());
        return 0;
    }
    std::printf("Updating %s -> %s ...\n", MDPAD_VERSION, latest.c_str());

    // Install into the prefix this binary lives in, when it follows the
    // .../lib/mdpad/mdpad layout; otherwise let the installer default to
    // ~/.local.
    std::string prefix_env;
    std::string exe = self_exe();
    std::string libdir = parent_dir(exe);            // .../lib/mdpad
    if (parent_dir(libdir).size() &&
        libdir.size() >= 10 &&
        libdir.compare(libdir.size() - 10, 10, "/lib/mdpad") == 0) {
        std::string prefix = parent_dir(parent_dir(libdir));  // strip lib/mdpad
        if (!prefix.empty())
            prefix_env = "MDPAD_PREFIX='" + prefix + "' ";
    }

    std::string dl = fetch_cmd(kInstallUrl);
    if (dl.empty()) {
        std::fprintf(stderr, "mdpad: need curl or wget to update\n");
        return 1;
    }
    std::string cmd = dl + " | " + prefix_env + "sh";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::fprintf(stderr, "mdpad: update failed (exit %d)\n", rc);
        return rc ? rc : 1;
    }
    std::printf("Updated to %s. Restart mdpad to use the new version.\n",
                latest.c_str());
    return 0;
}

int print_update_status() {
    std::string latest = fetch_latest_version();
    if (latest.empty()) {
        std::fprintf(stderr, "mdpad: could not check for updates\n");
        return 1;
    }
    if (version_compare(latest, MDPAD_VERSION) > 0) {
        std::printf("An update is available: %s -> %s\n", MDPAD_VERSION,
                    latest.c_str());
        std::printf("Run:  mdpad --update\n");
        return 10;
    }
    std::printf("mdpad %s is up to date.\n", MDPAD_VERSION);
    return 0;
}

int run_background_check() {
    std::string dir = cache_dir();
    if (!dir.empty()) write_file(dir + "/last-check", std::to_string(::time(nullptr)));
    std::string latest = fetch_latest_version();
    if (latest.empty()) return 0;
    std::string marker = dir.empty() ? std::string() : dir + "/update-available";
    if (!marker.empty()) {
        if (version_compare(latest, MDPAD_VERSION) > 0)
            write_file(marker, latest);
        else
            ::remove(marker.c_str());
    }
    return 0;
}

void maybe_spawn_update_check() {
    std::string dir = cache_dir();
    if (dir.empty()) return;
    std::string stamp = dir + "/last-check";
    struct stat st;
    if (stat(stamp.c_str(), &st) == 0) {
        if (::time(nullptr) - st.st_mtime < 24 * 60 * 60) return;  // throttle
    }
    // Touch now so concurrent launches don't all spawn checks.
    write_file(stamp, std::to_string(::time(nullptr)));

    std::string exe = self_exe();
    if (exe.empty()) return;
    // Detached, silent.  setsid keeps it alive past this process.
    std::string cmd = "setsid '" + exe +
                      "' --bg-update-check >/dev/null 2>&1 &";
    int rc = std::system(cmd.c_str());
    (void)rc;
}

std::string cached_update_version() {
    std::string dir = cache_dir();
    if (dir.empty()) return std::string();
    std::string v = trim(read_file(dir + "/update-available"));
    if (v.empty()) return std::string();
    if (version_compare(v, MDPAD_VERSION) > 0) return v;
    return std::string();
}

}  // namespace mdpad
