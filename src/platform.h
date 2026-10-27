#pragma once
#include "util.h"
#include <optional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace platform {

enum class DialogStatus { Picked, Cancelled, Unavailable };
struct DialogResult {
    DialogStatus status = DialogStatus::Unavailable;
    fs::path path;
};

fs::path configDir();          // %APPDATA%\mixtape | $XDG_CONFIG_HOME/mixtape
fs::path defaultPlaylistDir(); // ~/Music/Playlists
std::vector<fs::path> roots(); // drive letters / "/" + $HOME + mounts
void* nativeHandle(GLFWwindow* w);
void applyDarkTitleBar(GLFWwindow* w);
void revealInFileManager(const fs::path& p);

DialogResult pickFolder(void* native, const fs::path& start);
DialogResult saveFile(void* native, const fs::path& startDir, const std::string& defaultName);
DialogResult openFile(void* native, const fs::path& startDir);

}  // namespace platform
