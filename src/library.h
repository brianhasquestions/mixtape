#pragma once
#include "util.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct Track {
    std::string path;    // UTF-8, native separators
    std::string title;
    std::string artist;
    std::string album;
    int duration = 0;    // seconds
    int trackNo = 0;
    int64_t mtime = 0;
    uint64_t size = 0;
    int root = -1;       // index into Library roots
    std::string folder;  // parent folder, relative to root (UTF-8, '/' separated)
    std::string key;     // lowercase search text
    std::string display; // "Artist - Title"
};

// Scans music folders on a background thread, reads tags with TagLib,
// caches results on disk so the next launch is instant.
class Library {
public:
    ~Library();

    void setRoots(std::vector<std::string> roots);  // triggers nothing; call rescan()
    const std::vector<std::string>& roots() const { return m_roots; }

    void rescan();                    // full rescan of all roots (background)
    void cancel();
    bool scanning() const { return m_scanning.load(); }
    int scanFound() const { return m_found.load(); }
    int scanTagged() const { return m_tagged.load(); }
    void pump();                      // call once per frame on the main thread

    const std::vector<Track>& tracks() const { return m_tracks; }
    uint64_t version() const { return m_version; }
    const Track* find(const std::string& path) const;  // by normalized path key
    int indexOf(const std::string& path) const;

    void loadCache(const fs::path& file);
    void saveCache(const fs::path& file) const;
    void removeRoot(int idx);         // drop tracks under that root immediately

private:
    void scanThread(std::vector<std::string> roots, std::unordered_map<std::string, Track> cache);
    void rebuildIndex();

    std::vector<std::string> m_roots;
    std::vector<Track> m_tracks;
    std::unordered_map<std::string, int> m_index;  // pathKey -> index
    uint64_t m_version = 1;

    std::thread m_thread;
    std::atomic<bool> m_scanning{false};
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_done{false};
    bool m_rescanRequested = false;
    std::atomic<int> m_found{0};
    std::atomic<int> m_tagged{0};
    std::mutex m_resultMutex;
    std::vector<Track> m_result;
    std::vector<Track> m_partial;  // progressive results during the first scan
    bool m_progressive = false;
    bool m_cacheDirty = false;
};

Track readTrack(const fs::path& p, int root, const std::string& rootPath);
