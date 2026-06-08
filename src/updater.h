#pragma once

#include <string>

namespace mdpad {

// The version compiled into this build (e.g. "1.1.0").
const char* version();

// The "owner/repo" this build checks for updates.
const char* repo();

// Query GitHub for the latest release version (the tag without a leading
// "v", e.g. "1.1").  Returns an empty string if the lookup fails or no
// network tool (curl/wget) is available.
std::string fetch_latest_version();

// Compare two dotted numeric version strings.  Returns >0 if a is newer than
// b, 0 if equal, <0 if older.  Missing components count as zero ("1.1" ==
// "1.1.0").
int version_compare(const std::string& a, const std::string& b);

// Download the latest release and install it in place (over wherever this
// executable lives, falling back to ~/.local).  Returns a process exit code:
// 0 on success.  Prints progress to stdout/stderr.
int run_self_update();

// Print whether a newer release is available (used by `--check-update`).
// Returns 0 if up to date, 10 if an update is available, 1 on lookup failure.
int print_update_status();

// Background, throttled check.  If more than a day has passed since the last
// check, spawn a detached `mdpad --bg-update-check`.  Cheap and silent.
void maybe_spawn_update_check();

// Run the actual background check now (used by `--bg-update-check`): refresh
// the cached "update available" marker.  Returns 0.
int run_background_check();

// If a cached check found a newer release, return its version; otherwise "".
std::string cached_update_version();

}  // namespace mdpad
