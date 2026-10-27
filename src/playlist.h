#pragma once
#include "util.h"
#include <optional>
#include <string>
#include <vector>

struct PlaylistEntry {
    std::string path;     // absolute, UTF-8, native separators
    std::string display;  // "Artist - Title"
    int duration = 0;
};

struct SaveOptions {
    bool relative = true;      // paths relative to the playlist file
    bool backslashes = false;  // use '\' separators (HiBy / Windows players)
    bool crlf = false;         // Windows line endings
};

struct Playlist {
    std::string name = "Untitled";
    fs::path file;  // empty until saved
    std::vector<PlaylistEntry> items;
    bool dirty = false;

    int totalDuration() const;
    static std::optional<Playlist> load(const fs::path& file);
    bool save(const fs::path& file, const SaveOptions& opt);
};
