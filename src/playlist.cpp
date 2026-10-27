#include "playlist.h"
#include <algorithm>
#include <fstream>
#include <sstream>

int Playlist::totalDuration() const {
    int s = 0;
    for (auto& e : items) s += e.duration;
    return s;
}

std::optional<Playlist> Playlist::load(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string data = ss.str();
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
        data.erase(0, 3);  // UTF-8 BOM

    Playlist pl;
    pl.file = file;
    pl.name = u8s(file.stem());
    fs::path base = file.parent_path();

    std::string pendingDisplay;
    int pendingDur = 0;
    std::istringstream lines(data);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (line.rfind("#EXTINF:", 0) == 0) {
                std::string rest = line.substr(8);
                size_t comma = rest.find(',');
                std::string durStr = comma == std::string::npos ? rest : rest.substr(0, comma);
                pendingDur = (int)std::strtod(durStr.c_str(), nullptr);
                pendingDisplay = comma == std::string::npos ? "" : trim(rest.substr(comma + 1));
            }
            continue;
        }
        std::string p = line;
#ifndef _WIN32
        std::replace(p.begin(), p.end(), '\\', '/');
#endif
        fs::path fp = u8p(p);
        if (fp.is_relative()) fp = base / fp;
        fp = fp.lexically_normal();
        PlaylistEntry e;
        e.path = u8s(fp);
        e.display = pendingDisplay.empty() ? u8s(fp.stem()) : pendingDisplay;
        e.duration = pendingDur;
        pl.items.push_back(std::move(e));
        pendingDisplay.clear();
        pendingDur = 0;
    }
    return pl;
}

bool Playlist::save(const fs::path& target, const SaveOptions& opt) {
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const char* nl = opt.crlf ? "\r\n" : "\n";
    out << "#EXTM3U" << nl;
    fs::path base = target.parent_path();
    for (auto& e : items) {
        fs::path p = u8p(e.path);
        std::string outPath;
        if (opt.relative) {
            fs::path rel = p.lexically_relative(base);
            outPath = rel.empty() ? u8s(p) : u8s(rel);
        } else {
            outPath = u8s(p);
        }
        if (opt.backslashes) std::replace(outPath.begin(), outPath.end(), '/', '\\');
        else std::replace(outPath.begin(), outPath.end(), '\\', '/');
        out << "#EXTINF:" << e.duration << "," << e.display << nl;
        out << outPath << nl;
    }
    file = target;
    name = u8s(target.stem());
    dirty = false;
    return true;
}
