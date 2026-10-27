#include "library.h"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>

using json = nlohmann::json;

static void inferFromFilename(const std::string& stem, std::string& artist, std::string& title) {
    // "Artist - Title" / "01 - Artist - Title" / "01. Title"
    std::string s = stem;
    size_t i = 0;
    while (i < s.size() && isdigit((unsigned char)s[i])) i++;
    if (i > 0 && i <= 3 && i < s.size()) {
        size_t j = i;
        while (j < s.size() && (s[j] == ' ' || s[j] == '.' || s[j] == '-' || s[j] == '_')) j++;
        if (j > i) s = s.substr(j);
    }
    size_t dash = s.find(" - ");
    if (dash != std::string::npos) {
        if (artist.empty()) artist = trim(s.substr(0, dash));
        if (title.empty()) title = trim(s.substr(dash + 3));
    } else if (title.empty()) {
        title = trim(s);
    }
}

Track readTrack(const fs::path& p, int root, const std::string& rootPath) {
    Track t;
    t.path = u8s(p);
    t.root = root;
    std::error_code ec;
    t.size = (uint64_t)fs::file_size(p, ec);
    t.mtime = (int64_t)fs::last_write_time(p, ec).time_since_epoch().count();

    {
#ifdef _WIN32
        TagLib::FileRef f(TagLib::FileName(p.wstring().c_str()), true, TagLib::AudioProperties::Fast);
#else
        TagLib::FileRef f(p.c_str(), true, TagLib::AudioProperties::Fast);
#endif
        if (!f.isNull()) {
            if (auto* tag = f.tag()) {
                t.title = trim(tag->title().to8Bit(true));
                t.artist = trim(tag->artist().to8Bit(true));
                t.album = trim(tag->album().to8Bit(true));
                t.trackNo = (int)tag->track();
            }
            if (auto* ap = f.audioProperties()) t.duration = ap->lengthInSeconds();
        }
    }
    inferFromFilename(u8s(p.stem()), t.artist, t.title);

    // folder relative to root
    std::string full = u8s(p.parent_path());
    std::string rel = full.size() > rootPath.size() ? full.substr(rootPath.size()) : std::string();
    while (!rel.empty() && (rel[0] == '\\' || rel[0] == '/')) rel.erase(0, 1);
    std::replace(rel.begin(), rel.end(), '\\', '/');
    t.folder = rel;

    t.display = t.artist.empty() ? t.title : t.artist + " - " + t.title;
    t.key = toLower(t.title + " " + t.artist + " " + t.album + " " + u8s(p.filename()) + " " + rel);
    return t;
}

Library::~Library() {
    cancel();
    if (m_thread.joinable()) m_thread.join();
}

void Library::setRoots(std::vector<std::string> roots) { m_roots = std::move(roots); }

void Library::cancel() { m_cancel = true; }

void Library::rescan() {
    if (m_scanning) {  // abort the running scan and start over once it has stopped
        m_cancel = true;
        m_rescanRequested = true;
        return;
    }
    if (m_thread.joinable()) m_thread.join();
    std::unordered_map<std::string, Track> cache;
    cache.reserve(m_tracks.size());
    for (auto& t : m_tracks) cache.emplace(t.path, t);
    m_progressive = m_tracks.empty();  // first scan: show tracks as they come in
    m_cancel = false;
    m_done = false;
    m_found = 0;
    m_tagged = 0;
    m_scanning = true;
    m_thread = std::thread(&Library::scanThread, this, m_roots, std::move(cache));
}

void Library::scanThread(std::vector<std::string> roots, std::unordered_map<std::string, Track> cache) {
    struct Item {
        fs::path path;
        int root;
    };
    std::vector<Item> files;

    // phase 1: enumerate
    for (int r = 0; r < (int)roots.size() && !m_cancel; r++) {
        fs::path rootPath = u8p(roots[r]);
        std::error_code ec;
        if (!fs::is_directory(rootPath, ec)) continue;
        fs::recursive_directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, ec), end;
        for (; it != end && !m_cancel; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            const auto& e = *it;
            if (e.is_regular_file(ec) && isAudioFile(e.path())) {
                files.push_back({e.path(), r});
                m_found++;
            }
        }
    }

    // phase 2: read tags in parallel (the work is I/O latency bound)
    std::vector<Track> out(files.size());
    std::vector<uint8_t> ok(files.size(), 0);
    std::atomic<size_t> next{0};
    unsigned nthreads = std::clamp(std::thread::hardware_concurrency(), 2u, 8u);
    if (files.size() < 64) nthreads = 1;
    auto worker = [&]() {
        std::error_code ec;
        for (;;) {
            size_t i = next.fetch_add(1);
            if (i >= files.size() || m_cancel) return;
            const Item& it = files[i];
            std::string key = u8s(it.path);
            auto c = cache.find(key);
            bool reused = false;
            if (c != cache.end()) {
                uint64_t sz = (uint64_t)fs::file_size(it.path, ec);
                int64_t mt = (int64_t)fs::last_write_time(it.path, ec).time_since_epoch().count();
                if (c->second.size == sz && c->second.mtime == mt) {
                    out[i] = c->second;
                    out[i].root = it.root;
                    reused = true;
                }
            }
            if (!reused) out[i] = readTrack(it.path, it.root, roots[it.root]);
            ok[i] = 1;
            m_tagged++;
            if (m_progressive) {
                std::lock_guard<std::mutex> lk(m_resultMutex);
                m_partial.push_back(out[i]);
            }
        }
    };
    std::vector<std::thread> pool;
    for (unsigned k = 1; k < nthreads; k++) pool.emplace_back(worker);
    worker();
    for (auto& th : pool) th.join();

    std::vector<Track> result;
    result.reserve(files.size());
    for (size_t i = 0; i < files.size(); i++)
        if (ok[i]) result.push_back(std::move(out[i]));
    {
        std::lock_guard<std::mutex> lk(m_resultMutex);
        m_result = std::move(result);
    }
    m_done = true;
}

void Library::pump() {
    if (!m_scanning) return;
    if (m_progressive && !m_done) {
        std::vector<Track> batch;
        {
            std::lock_guard<std::mutex> lk(m_resultMutex);
            if (!m_partial.empty()) batch.swap(m_partial);
        }
        if (!batch.empty()) {
            for (auto& t : batch) {
                m_index[pathKey(u8p(t.path))] = (int)m_tracks.size();
                m_tracks.push_back(std::move(t));
            }
            m_version++;
        }
    }
    if (m_done) {
        if (m_thread.joinable()) m_thread.join();
        if (!m_cancel) {
            std::lock_guard<std::mutex> lk(m_resultMutex);
            m_tracks = std::move(m_result);
            m_result.clear();
            m_partial.clear();
            rebuildIndex();
            m_version++;
            m_cacheDirty = true;
        } else if (m_progressive) {
            m_tracks.clear();
            m_partial.clear();
            rebuildIndex();
            m_version++;
        }
        m_scanning = false;
        m_done = false;
        m_progressive = false;
        if (m_rescanRequested) {
            m_rescanRequested = false;
            rescan();
        }
    }
}

void Library::rebuildIndex() {
    m_index.clear();
    m_index.reserve(m_tracks.size() * 2);
    for (int i = 0; i < (int)m_tracks.size(); i++) m_index[pathKey(u8p(m_tracks[i].path))] = i;
}

const Track* Library::find(const std::string& path) const {
    int i = indexOf(path);
    return i < 0 ? nullptr : &m_tracks[i];
}

int Library::indexOf(const std::string& path) const {
    auto it = m_index.find(pathKey(u8p(path)));
    return it == m_index.end() ? -1 : it->second;
}

void Library::removeRoot(int idx) {
    if (idx < 0 || idx >= (int)m_roots.size()) return;
    m_roots.erase(m_roots.begin() + idx);
    std::vector<Track> keep;
    keep.reserve(m_tracks.size());
    for (auto& t : m_tracks) {
        if (t.root == idx) continue;
        if (t.root > idx) t.root--;
        keep.push_back(std::move(t));
    }
    m_tracks = std::move(keep);
    rebuildIndex();
    m_version++;
    m_cacheDirty = true;
}

void Library::loadCache(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    json j;
    try { in >> j; } catch (...) { return; }
    if (!j.is_object()) return;
    try {
        m_roots = j.value("roots", std::vector<std::string>{});
        m_tracks.clear();
        for (auto& e : j["tracks"]) {
            Track t;
            t.path = e.value("p", "");
            t.title = e.value("t", "");
            t.artist = e.value("a", "");
            t.album = e.value("al", "");
            t.duration = e.value("d", 0);
            t.trackNo = e.value("n", 0);
            t.mtime = e.value("m", (int64_t)0);
            t.size = e.value("s", (uint64_t)0);
            t.root = e.value("r", -1);
            t.folder = e.value("f", "");
            if (t.path.empty() || t.root < 0 || t.root >= (int)m_roots.size()) continue;
            t.display = t.artist.empty() ? t.title : t.artist + " - " + t.title;
            t.key = toLower(t.title + " " + t.artist + " " + t.album + " " + u8s(u8p(t.path).filename()) + " " + t.folder);
            m_tracks.push_back(std::move(t));
        }
    } catch (...) {
        m_tracks.clear();
    }
    rebuildIndex();
    m_version++;
}

void Library::saveCache(const fs::path& file) const {
    json j;
    j["roots"] = m_roots;
    json arr = json::array();
    for (auto& t : m_tracks) {
        arr.push_back({{"p", t.path}, {"t", t.title}, {"a", t.artist}, {"al", t.album}, {"d", t.duration},
                       {"n", t.trackNo}, {"m", t.mtime}, {"s", t.size}, {"r", t.root}, {"f", t.folder}});
    }
    j["tracks"] = std::move(arr);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    if (out) out << j.dump();
}
