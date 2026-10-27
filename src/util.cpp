#include "util.h"
#include <algorithm>
#include <array>
#include <cstdio>

std::string toLower(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            out += (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
            i++;
            continue;
        }
        if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {  // 2-byte sequence
            unsigned cp = ((c & 0x1F) << 6) | ((unsigned char)s[i + 1] & 0x3F);
            if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) cp += 0x20;         // Latin-1 upper
            else if (cp >= 0x410 && cp <= 0x42F) cp += 0x20;                 // Cyrillic A-Ya
            else if (cp >= 0x400 && cp <= 0x40F) cp += 0x50;                 // Cyrillic Io etc.
            else if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) cp += 0x20;  // Greek
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
            i += 2;
            continue;
        }
        size_t n = (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        out.append(s, i, n);
        i += n;
    }
    return out;
}

std::string fmtDuration(int sec) {
    if (sec < 0) sec = 0;
    char buf[32];
    if (sec >= 3600) std::snprintf(buf, sizeof buf, "%d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
    else std::snprintf(buf, sizeof buf, "%d:%02d", sec / 60, sec % 60);
    return buf;
}

std::string fmtCount(size_t n) {
    std::string s = std::to_string(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, ",");
    return s;
}

std::string pathKey(const fs::path& p) {
    std::string s = u8s(p.lexically_normal());
    std::replace(s.begin(), s.end(), '\\', '/');
    while (s.size() > 1 && s.back() == '/') s.pop_back();
#ifdef _WIN32
    for (auto& ch : s)
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
#endif
    return s;
}

bool isAudioFile(const fs::path& p) {
    static const std::array<const char*, 16> exts = {".mp3", ".flac", ".m4a", ".aac", ".ogg", ".opus", ".wav", ".wma",
                                                      ".aiff", ".aif", ".ape", ".wv", ".mpc", ".dsf", ".dff", ".mp4"};
    std::string e = toLower(u8s(p.extension()));
    for (auto x : exts)
        if (e == x) return true;
    return false;
}

std::vector<std::string> splitWords(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool containsAll(const std::string& hay, const std::vector<std::string>& needles) {
    for (auto& n : needles)
        if (hay.find(n) == std::string::npos) return false;
    return true;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    return s.substr(i);
}
