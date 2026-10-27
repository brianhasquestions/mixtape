#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// UTF-8 <-> std::filesystem::path helpers
inline fs::path u8p(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
inline std::string u8s(const fs::path& p) { auto u = p.u8string(); return std::string(u.begin(), u.end()); }

std::string toLower(std::string s);              // ASCII + Latin-1 + Cyrillic + Greek aware lowercase (UTF-8)
std::string fmtDuration(int sec);                // 3:07 / 1:02:33
std::string fmtCount(size_t n);                  // 12,345
std::string pathKey(const fs::path& p);          // normalized key for comparing paths
bool isAudioFile(const fs::path& p);
std::vector<std::string> splitWords(const std::string& s);
bool containsAll(const std::string& hay, const std::vector<std::string>& needles);
std::string trim(std::string s);
