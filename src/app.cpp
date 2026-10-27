#include "app.h"
#include "platform.h"
#include "theme.h"

#include <GLFW/glfw3.h>
#include <imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <unordered_set>

using json = nlohmann::json;

// ============================================================================ Config

void Config::load(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    if (!in) return;
    json j;
    try { in >> j; } catch (...) { return; }
    if (!j.is_object()) return;
    playlistDir = j.value("playlistDir", playlistDir);
    theme = j.value("theme", theme);
    uiScale = j.value("uiScale", uiScale);
    saveOpts.relative = j.value("relativePaths", saveOpts.relative);
    saveOpts.backslashes = j.value("backslashes", saveOpts.backslashes);
    saveOpts.crlf = j.value("crlf", saveOpts.crlf);
    winW = j.value("winW", winW);
    winH = j.value("winH", winH);
    sidebarW = j.value("sidebarW", sidebarW);
    playlistW = j.value("playlistW", playlistW);
    viewMode = j.value("viewMode", viewMode);
    lastPlaylist = j.value("lastPlaylist", lastPlaylist);
    uiScale = std::clamp(uiScale, 0.6f, 2.5f);
}

void Config::save(const fs::path& f) const {
    json j;
    j["playlistDir"] = playlistDir;
    j["theme"] = theme;
    j["uiScale"] = uiScale;
    j["relativePaths"] = saveOpts.relative;
    j["backslashes"] = saveOpts.backslashes;
    j["crlf"] = saveOpts.crlf;
    j["winW"] = winW;
    j["winH"] = winH;
    j["sidebarW"] = sidebarW;
    j["playlistW"] = playlistW;
    j["viewMode"] = viewMode;
    j["lastPlaylist"] = lastPlaylist;
    std::error_code ec;
    fs::create_directories(f.parent_path(), ec);
    std::ofstream out(f, std::ios::binary);
    if (out) out << j.dump(2);
}

// ============================================================================ helpers

static const Theme& curTheme(const Config& c) {
    auto& t = themes();
    return t[std::clamp(c.theme, 0, (int)t.size() - 1)];
}

static float textW(const char* s) { return ImGui::CalcTextSize(s).x; }
static float buttonW(const char* s) { return ImGui::CalcTextSize(s).x + ImGui::GetStyle().FramePadding.x * 2.0f; }

static std::string sanitizeFilename(std::string s) {
    for (auto& c : s)
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    s = trim(s);
    return s.empty() ? "playlist" : s;
}

static int ciCompare(const std::string& a, const std::string& b) {
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return x < y ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

// Draggable divider between two panes. Returns true while being dragged.
static bool Splitter(const char* id, float thickness, float* sizeA, float* sizeB, float minA, float minB, float length,
                     ImVec4 accent, ImVec4 idle) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##splitter", ImVec2(thickness, length));
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (active) {
        float d = ImGui::GetIO().MouseDelta.x;
        if (*sizeA + d < minA) d = minA - *sizeA;
        if (*sizeB - d < minB) d = *sizeB - minB;
        *sizeA += d;
        *sizeB -= d;
    }
    ImVec4 c = (hovered || active) ? accent : idle;
    float cx = p.x + thickness * 0.5f;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(cx - 1, p.y + 8), ImVec2(cx + 1, p.y + length - 8), col32(c), 1.0f);
    ImGui::PopID();
    return active;
}

static void Spinner(float radius, float thickness, ImU32 color) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(radius * 2 + 4, radius * 2);
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = (float)ImGui::GetTime();
    float a0 = t * 5.0f;
    ImVec2 c(pos.x + radius, pos.y + radius);
    dl->PathArcTo(c, radius - thickness * 0.5f, a0, a0 + 4.5f, 24);
    dl->PathStroke(color, 0, thickness);
}

static void drawAccentUnderline(ImVec2 a, ImVec2 b, ImVec4 c1, ImVec4 c2) {
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(a, b, col32(c1), col32(c2), col32(c2), col32(c1));
}

// ============================================================================ App

App::App(GLFWwindow* window) : m_window(window) {
    m_native = platform::nativeHandle(window);
    m_configDir = platform::configDir();
    m_cfg.load(m_configDir / "config.json");
    if (m_cfg.playlistDir.empty()) m_cfg.playlistDir = u8s(platform::defaultPlaylistDir());
#ifdef _WIN32
    static bool firstRun = !fs::exists(m_configDir / "config.json");
    if (firstRun) m_cfg.saveOpts.backslashes = true;
#endif
    m_lib.loadCache(m_configDir / "library.json");
    m_savedLibVersion = m_lib.version();
    if (!m_lib.roots().empty()) m_lib.rescan();
    if (!m_cfg.lastPlaylist.empty()) {
        if (!openPlaylist(u8p(m_cfg.lastPlaylist))) m_cfg.lastPlaylist.clear();
        m_toasts.clear();
    }
}

App::~App() { saveState(); }

void App::saveState() {
    int w = 0, h = 0;
    glfwGetWindowSize(m_window, &w, &h);
    if (w > 200 && h > 200) {
        m_cfg.winW = (int)(w / m_dpi);
        m_cfg.winH = (int)(h / m_dpi);
    }
    m_cfg.save(m_configDir / "config.json");
    m_lib.saveCache(m_configDir / "library.json");
    m_savedLibVersion = m_lib.version();
}

ImVec4 App::clearColor() const { return curTheme(m_cfg).bg; }

static void collectCodepoints(const std::string& s, std::unordered_set<unsigned>& out) {
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { i++; continue; }
        unsigned cp = 0;
        size_t n = 1;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
        else { i++; continue; }
        if (i + n > s.size()) break;
        for (size_t k = 1; k < n; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        if (cp >= 0x80 && cp <= 0xFFFF) out.insert(cp);  // ImWchar is 16-bit in this build
        i += n;
    }
}

std::string App::glyphText() const {
    std::unordered_set<unsigned> cps;
    for (const Track& t : m_lib.tracks()) {
        collectCodepoints(t.title, cps);
        collectCodepoints(t.artist, cps);
        collectCodepoints(t.album, cps);
        collectCodepoints(t.folder, cps);
        collectCodepoints(t.path, cps);
    }
    for (const PlaylistEntry& e : m_pl.items) {
        collectCodepoints(e.display, cps);
        collectCodepoints(e.path, cps);
    }
    collectCodepoints(m_pl.name, cps);
    for (const fs::path& p : m_playlistFiles) collectCodepoints(u8s(p.filename()), cps);
    for (const std::string& r : m_lib.roots()) collectCodepoints(r, cps);
    std::vector<unsigned> sorted(cps.begin(), cps.end());
    std::sort(sorted.begin(), sorted.end());
    std::string out;
    out.reserve(sorted.size() * 3);
    for (unsigned cp : sorted) {
        if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

void App::applyThemeNow() { applyTheme(curTheme(m_cfg), m_dpi * m_cfg.uiScale); }

bool App::takeFontRebuild(float& scaleOut) {
    if (m_pendingScale <= 0.0f) return false;
    scaleOut = m_pendingScale;
    m_cfg.uiScale = m_pendingScale;
    m_pendingScale = 0.0f;
    return true;
}

bool App::requestQuit() {
    if (!m_pl.dirty) return true;
    performRequest(Pending::Quit);
    return false;
}

void App::toast(const std::string& msg, int kind) {
    m_toasts.push_back({msg, ImGui::GetTime() + 3.5, kind});
    if (m_toasts.size() > 5) m_toasts.erase(m_toasts.begin());
}

// ---------------------------------------------------------------------------- frame

void App::frame() {
    m_lib.pump();
    if (!m_lib.scanning() && m_lib.version() != m_savedLibVersion) {
        m_lib.saveCache(m_configDir / "library.json");
        m_savedLibVersion = m_lib.version();
    }
    if ((!m_lib.scanning() && m_lib.version() != m_glyphLibVersion) || m_glyphCheck) {
        m_glyphLibVersion = m_lib.version();
        m_glyphCheck = false;
        refreshPlaylistFiles(true);
        std::string t = glyphText();
        if (t != m_glyphTextBuilt) {
            m_glyphTextBuilt = t;
            m_fontsDirty = true;
        }
    }

    const Theme& T = curTheme(m_cfg);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * m_dpi, 8 * m_dpi));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);

    // global shortcuts
    bool anyPopup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    if (!anyPopup) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) m_focusSearch = true;
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) savePlaylist(false);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) savePlaylist(true);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) performRequest(Pending::NewPlaylist);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) startOpenPlaylistDialog();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) m_openSettings = true;
    }

    drawHeader();

    float statusH = ImGui::GetFrameHeightWithSpacing();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float bodyH = avail.y - statusH;
    float splitW = 8.0f * m_dpi;
    float minPane = 160.0f * m_dpi;
    float libW = avail.x - m_cfg.sidebarW - m_cfg.playlistW - 2 * splitW;
    if (libW < minPane * 1.5f) {
        float over = minPane * 1.5f - libW;
        m_cfg.playlistW = std::max(minPane, m_cfg.playlistW - over * 0.6f);
        m_cfg.sidebarW = std::max(minPane, m_cfg.sidebarW - over * 0.4f);
        libW = avail.x - m_cfg.sidebarW - m_cfg.playlistW - 2 * splitW;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, withAlpha(T.bgAlt, 0.6f));
    if (ImGui::BeginChild("##sidebar", ImVec2(m_cfg.sidebarW, bodyH), ImGuiChildFlags_Borders)) drawSidebar();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine(0, 0);
    Splitter("s1", splitW, &m_cfg.sidebarW, &libW, minPane, minPane * 1.5f, bodyH, T.accent, T.border);
    ImGui::SameLine(0, 0);

    if (ImGui::BeginChild("##library", ImVec2(libW, bodyH), ImGuiChildFlags_Borders)) drawLibrary();
    ImGui::EndChild();

    ImGui::SameLine(0, 0);
    Splitter("s2", splitW, &libW, &m_cfg.playlistW, minPane * 1.5f, minPane, bodyH, T.accent, T.border);
    ImGui::SameLine(0, 0);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, withAlpha(T.bgAlt, 0.6f));
    if (ImGui::BeginChild("##playlist", ImVec2(m_cfg.playlistW, bodyH), ImGuiChildFlags_Borders)) drawPlaylist();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    drawStatusBar();
    drawModals();
    if (m_browser.draw()) handleBrowserResult(m_browser.result());
    drawToasts();
    ImGui::End();
}

// ---------------------------------------------------------------------------- header

void App::drawHeader() {
    const Theme& T = curTheme(m_cfg);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetFrameHeight() + 10 * m_dpi;

    ImGui::PushFont(m_fontBold ? m_fontBold : ImGui::GetFont());
    float fsz = ImGui::GetFontSize();
    const char* title = "mixtape";
    float titleW = textW(title);
    dl->AddText(ImVec2(p.x + 4, p.y + (h - fsz) * 0.5f - 2), col32(T.accent), title);
    ImGui::PopFont();
    drawAccentUnderline(ImVec2(p.x + 4, p.y + h - 6), ImVec2(p.x + 4 + titleW, p.y + h - 3), T.accent, T.accent2);

    // right-side controls
    const char* bAdd = "+ Folder";
    const char* bRescan = "Rescan";
    const char* bSettings = "Settings";
    float sp = ImGui::GetStyle().ItemSpacing.x;
    float rightW = buttonW("Table") + buttonW("Tree") + ImGui::GetFrameHeight() * 2 + buttonW(bAdd) + buttonW(bRescan) +
                   buttonW(bSettings) + sp * 6;
    float searchX = p.x + titleW + 28 * m_dpi;
    float searchW = std::max(180.0f * m_dpi, w - (searchX - p.x) - rightW - sp);

    ImGui::SetCursorScreenPos(ImVec2(searchX, p.y + 5 * m_dpi));
    ImGui::SetNextItemWidth(searchW);
    if (m_focusSearch) {
        ImGui::SetKeyboardFocusHere();
        m_focusSearch = false;
    }
    ImGui::PushStyleColor(ImGuiCol_FrameBg, withAlpha(T.surface, 0.9f));
    if (ImGui::InputTextWithHint("##search", "Search title, artist, album, folder   (Ctrl+F)", &m_query)) m_scrollToTop = true;
    ImGui::PopStyleColor();
    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) m_query.clear();

    ImGui::SameLine();
    if (ImGui::RadioButton("Table", m_cfg.viewMode == 0)) m_cfg.viewMode = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Tree", m_cfg.viewMode == 1)) m_cfg.viewMode = 1;
    ImGui::SameLine();
    if (ImGui::Button(bAdd)) startAddRootDialog();
    ImGui::SetItemTooltip("Add a music folder to the library (you can also drop folders on the window)");
    ImGui::SameLine();
    ImGui::BeginDisabled(m_lib.roots().empty());
    if (ImGui::Button(bRescan)) m_lib.rescan();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(bSettings)) m_openSettings = true;

    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 2));
    dl->AddRectFilledMultiColor(ImVec2(p.x, p.y + h + 2), ImVec2(p.x + w, p.y + h + 3), col32(withAlpha(T.accent, 0.7f)),
                                col32(withAlpha(T.accent2, 0.05f)), col32(withAlpha(T.accent2, 0.05f)),
                                col32(withAlpha(T.accent, 0.7f)));
    ImGui::Dummy(ImVec2(0, 6 * m_dpi));
}

// ---------------------------------------------------------------------------- sidebar

void App::drawSidebar() {
    const Theme& T = curTheme(m_cfg);
    ImGui::PushStyleColor(ImGuiCol_Text, T.textDim);
    ImGui::SeparatorText("LIBRARY");
    ImGui::PopStyleColor();

    int total = (int)m_lib.tracks().size();
    std::string allLabel = "All folders";
    if (ImGui::Selectable(allLabel.c_str(), m_rootFilter == -1)) m_rootFilter = -1;
    ImGui::SameLine();
    ImGui::TextDisabled("%s", fmtCount(total).c_str());

    const auto& roots = m_lib.roots();
    int removeIdx = -1;
    for (int i = 0; i < (int)roots.size(); i++) {
        ImGui::PushID(i);
        fs::path rp = u8p(roots[i]);
        std::string name = u8s(rp.filename());
        if (name.empty()) name = roots[i];
        if (ImGui::Selectable(name.c_str(), m_rootFilter == i)) m_rootFilter = (m_rootFilter == i) ? -1 : i;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", roots[i].c_str());
        if (ImGui::BeginPopupContextItem("rootctx")) {
            if (ImGui::MenuItem("Add everything to playlist")) {
                int n = 0;
                for (int k = 0; k < total; k++)
                    if (m_lib.tracks()[k].root == i) { addTrack(k, false); n++; }
                toast("Added " + fmtCount(n) + " tracks");
            }
            if (ImGui::MenuItem("Reveal in file manager")) platform::revealInFileManager(rp);
            ImGui::Separator();
            if (ImGui::MenuItem("Remove from library")) removeIdx = i;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (removeIdx >= 0) {
        m_lib.removeRoot(removeIdx);
        if (m_rootFilter == removeIdx) m_rootFilter = -1;
        else if (m_rootFilter > removeIdx) m_rootFilter--;
        toast("Folder removed");
    }
    if (ImGui::SmallButton("+ Add folder")) startAddRootDialog();

    ImGui::Dummy(ImVec2(0, 8 * m_dpi));
    ImGui::PushStyleColor(ImGuiCol_Text, T.textDim);
    ImGui::SeparatorText("PLAYLISTS");
    ImGui::PopStyleColor();

    refreshPlaylistFiles();
    if (m_playlistFiles.empty()) ImGui::TextDisabled("No playlists yet");
    for (int i = 0; i < (int)m_playlistFiles.size(); i++) {
        ImGui::PushID(i);
        const fs::path& f = m_playlistFiles[i];
        bool cur = !m_pl.file.empty() && pathKey(m_pl.file) == pathKey(f);
        std::string label = u8s(f.stem());
        if (ImGui::Selectable(label.c_str(), cur)) {
            if (!cur) performRequest(Pending::OpenPlaylist, f);
        }
        if (ImGui::BeginPopupContextItem("plctx")) {
            if (ImGui::MenuItem("Reveal in file manager")) platform::revealInFileManager(f);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ New")) performRequest(Pending::NewPlaylist);
    ImGui::SameLine();
    if (ImGui::SmallButton("Open...")) startOpenPlaylistDialog();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", m_cfg.playlistDir.c_str());
    ImGui::PopTextWrapPos();
    ImGui::SetItemTooltip("Playlist folder (change in Settings)");
}

void App::refreshPlaylistFiles(bool force) {
    double now = ImGui::GetTime();
    if (!force && now - m_playlistFilesAt < 3.0) return;
    m_playlistFilesAt = now;
    m_playlistFiles.clear();
    std::error_code ec;
    fs::path dir = u8p(m_cfg.playlistDir);
    if (!fs::is_directory(dir, ec)) return;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        std::string ext = toLower(u8s(it->path().extension()));
        if (ext == ".m3u8" || ext == ".m3u") m_playlistFiles.push_back(it->path());
    }
    std::sort(m_playlistFiles.begin(), m_playlistFiles.end(),
              [](const fs::path& a, const fs::path& b) { return ciCompare(u8s(a.filename()), u8s(b.filename())) < 0; });
}

// ---------------------------------------------------------------------------- library

void App::refreshFiltered() {
    const auto& tr = m_lib.tracks();
    if (m_selVersion != m_lib.version() || m_sel.size() != tr.size()) {
        m_sel.assign(tr.size(), 0);
        m_selCount = 0;
        m_selAnchor = -1;
        m_selVersion = m_lib.version();
    }
    bool changed = m_filteredVersion != m_lib.version() || m_filteredQuery != m_query || m_filteredRoot != m_rootFilter;
    if (changed) {
        m_queryWords = splitWords(toLower(m_query));
        m_filtered.clear();
        m_filtered.reserve(tr.size());
        for (int i = 0; i < (int)tr.size(); i++) {
            if (m_rootFilter >= 0 && tr[i].root != m_rootFilter) continue;
            if (!m_queryWords.empty() && !containsAll(tr[i].key, m_queryWords)) continue;
            m_filtered.push_back(i);
        }
        m_filteredVersion = m_lib.version();
        m_filteredQuery = m_query;
        m_filteredRoot = m_rootFilter;
        m_sortDirty = true;
        m_treeDirty = true;
    }
    if (m_sortDirty) {
        sortFiltered();
        m_sortDirty = false;
    }
}

void App::sortFiltered() {
    const auto& tr = m_lib.tracks();
    int col = m_sortCol;
    bool asc = m_sortAsc;
    auto cmp = [&](int a, int b) {
        const Track& x = tr[a];
        const Track& y = tr[b];
        int c = 0;
        switch (col) {
            case 1: c = (x.trackNo < y.trackNo) ? -1 : (x.trackNo > y.trackNo) ? 1 : 0; break;
            case 2: c = ciCompare(x.title, y.title); break;
            case 3: c = ciCompare(x.artist, y.artist); break;
            case 4: c = ciCompare(x.album, y.album); if (c == 0) c = (x.trackNo < y.trackNo) ? -1 : (x.trackNo > y.trackNo) ? 1 : 0; break;
            case 5: c = (x.duration < y.duration) ? -1 : (x.duration > y.duration) ? 1 : 0; break;
            case 6: c = ciCompare(x.folder, y.folder); if (c == 0) c = (x.trackNo < y.trackNo) ? -1 : (x.trackNo > y.trackNo) ? 1 : 0; break;
            default: break;
        }
        if (c == 0) c = ciCompare(x.path, y.path);
        return asc ? c < 0 : c > 0;
    };
    std::sort(m_filtered.begin(), m_filtered.end(), cmp);
}

void App::setSelected(int idx, bool on) {
    if (idx < 0 || idx >= (int)m_sel.size()) return;
    if (m_sel[idx] == (on ? 1 : 0)) return;
    m_sel[idx] = on ? 1 : 0;
    m_selCount += on ? 1 : -1;
}

void App::clearSelection() {
    std::fill(m_sel.begin(), m_sel.end(), 0);
    m_selCount = 0;
}

void App::selectAllFiltered() {
    for (int idx : m_filtered) setSelected(idx, true);
}

void App::clickRow(int idx, int pos, bool shift) {
    if (shift && pos >= 0 && m_selAnchor >= 0 && m_selAnchor < (int)m_filtered.size()) {
        int a = std::min(m_selAnchor, pos), b = std::max(m_selAnchor, pos);
        for (int i = a; i <= b; i++) setSelected(m_filtered[i], true);
    } else {
        setSelected(idx, !m_sel[idx]);
        m_selAnchor = pos;
    }
}

void App::addTrack(int idx, bool notify) {
    const auto& tr = m_lib.tracks();
    if (idx < 0 || idx >= (int)tr.size()) return;
    const Track& t = tr[idx];
    m_pl.items.push_back({t.path, t.display, t.duration});
    playlistTouched();
    if (notify) toast("Added: " + t.display);
}

void App::addSelectedToPlaylist() {
    if (m_selCount == 0) return;
    int n = 0;
    // keep the visible (sorted) order
    for (int idx : m_filtered)
        if (m_sel[idx]) { addTrack(idx, false); n++; }
    // selected rows hidden by the current filter
    if (n < m_selCount)
        for (int i = 0; i < (int)m_sel.size(); i++)
            if (m_sel[i] && (m_rootFilter >= 0 && m_lib.tracks()[i].root != m_rootFilter ||
                             !m_queryWords.empty() && !containsAll(m_lib.tracks()[i].key, m_queryWords))) {
                addTrack(i, false);
                n++;
            }
    clearSelection();
    toast("Added " + fmtCount(n) + " track" + (n == 1 ? "" : "s"));
}

void App::addWhere(int what, const Track& like) {
    const auto& tr = m_lib.tracks();
    std::vector<int> idx;
    for (int i = 0; i < (int)tr.size(); i++) {
        const Track& t = tr[i];
        bool ok = false;
        if (what == 0) ok = !like.album.empty() && t.album == like.album && t.root == like.root && t.folder == like.folder;
        else if (what == 1) ok = !like.artist.empty() && ciCompare(t.artist, like.artist) == 0;
        else ok = t.root == like.root && t.folder == like.folder;
        if (ok) idx.push_back(i);
    }
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        if (tr[a].trackNo != tr[b].trackNo) return tr[a].trackNo < tr[b].trackNo;
        return ciCompare(tr[a].path, tr[b].path) < 0;
    });
    for (int i : idx) addTrack(i, false);
    toast("Added " + fmtCount(idx.size()) + " tracks");
}

void App::drawTrackContextMenu(int idx) {
    const Track& t = m_lib.tracks()[idx];
    if (ImGui::BeginPopupContextItem("trackctx")) {
        if (ImGui::MenuItem("Add to playlist")) addTrack(idx);
        if (m_selCount > 0) {
            std::string l = "Add " + fmtCount(m_selCount) + " selected";
            if (ImGui::MenuItem(l.c_str())) addSelectedToPlaylist();
        }
        ImGui::Separator();
        if (!t.album.empty()) {
            std::string l = "Add album: " + t.album;
            if (ImGui::MenuItem(l.c_str())) addWhere(0, t);
        }
        if (!t.artist.empty()) {
            std::string l = "Add everything by " + t.artist;
            if (ImGui::MenuItem(l.c_str())) addWhere(1, t);
        }
        if (ImGui::MenuItem("Add whole folder")) addWhere(2, t);
        ImGui::Separator();
        if (ImGui::MenuItem("Reveal in file manager")) platform::revealInFileManager(u8p(t.path));
        if (ImGui::MenuItem("Copy path")) ImGui::SetClipboardText(t.path.c_str());
        ImGui::EndPopup();
    }
}

void App::drawLibrary() {
    const Theme& T = curTheme(m_cfg);
    refreshFiltered();
    const auto& tr = m_lib.tracks();

    if (tr.empty() && !m_lib.scanning()) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosY(avail.y * 0.38f);
        ImGui::PushFont(m_fontBold ? m_fontBold : ImGui::GetFont());
        const char* t1 = "Your library is empty";
        ImGui::SetCursorPosX((avail.x - textW(t1)) * 0.5f);
        ImGui::TextColored(T.textDim, "%s", t1);
        ImGui::PopFont();
        const char* t2 = "Add a folder of music, or drop one onto this window.";
        ImGui::SetCursorPosX((avail.x - textW(t2)) * 0.5f);
        ImGui::TextDisabled("%s", t2);
        ImGui::Spacing();
        float bw = buttonW("+ Add music folder");
        ImGui::SetCursorPosX((avail.x - bw) * 0.5f);
        if (ImGui::Button("+ Add music folder")) startAddRootDialog();
        return;
    }

    // toolbar
    std::string info = fmtCount(m_filtered.size()) + (m_filtered.size() == 1 ? " track" : " tracks");
    if (!m_query.empty() || m_rootFilter >= 0) info += " of " + fmtCount(tr.size());
    ImGui::TextDisabled("%s", info.c_str());
    if (m_selCount > 0) {
        ImGui::SameLine();
        ImGui::TextColored(T.accent, "%s selected", fmtCount(m_selCount).c_str());
    }
    std::string addLabel = m_selCount > 0 ? "Add " + fmtCount(m_selCount) + " to playlist  [Enter]" : "Add to playlist";
    const char* clearLabel = "Clear";
    float rw = buttonW(addLabel.c_str()) + buttonW(clearLabel) + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - rw);
    ImGui::BeginDisabled(m_selCount == 0);
    ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(T.accent, m_selCount > 0 ? 0.35f : 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, withAlpha(T.accent, 0.5f));
    if (ImGui::Button(addLabel.c_str())) addSelectedToPlaylist();
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ImGui::Button(clearLabel)) clearSelection();
    ImGui::EndDisabled();

    if (m_cfg.viewMode == 0) drawLibraryTable();
    else drawLibraryTree();

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) addSelectedToPlaylist();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) selectAllFiltered();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) clearSelection();
    }
}

void App::drawLibraryTable() {
    const Theme& T = curTheme(m_cfg);
    const auto& tr = m_lib.tracks();
    ImGuiIO& io = ImGui::GetIO();
    float rowH = ImGui::GetFrameHeight();

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_Sortable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody |
                            ImGuiTableFlags_BordersOuterH;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8 * m_dpi, 2 * m_dpi));
    if (ImGui::BeginTable("lib", 7, flags, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##chk", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort |
                                             ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide |
                                             ImGuiTableColumnFlags_NoHeaderLabel | ImGuiTableColumnFlags_NoResize,
                                rowH, 0);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34 * m_dpi, 1);
        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 3.0f, 2);
        ImGui::TableSetupColumn("Artist", ImGuiTableColumnFlags_WidthStretch, 2.0f, 3);
        ImGui::TableSetupColumn("Album", ImGuiTableColumnFlags_WidthStretch, 2.0f, 4);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 58 * m_dpi, 5);
        ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 2.0f, 6);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
            if (ss->SpecsDirty) {
                if (ss->SpecsCount > 0) {
                    m_sortCol = (int)ss->Specs[0].ColumnUserID;
                    m_sortAsc = ss->Specs[0].SortDirection != ImGuiSortDirection_Descending;
                }
                sortFiltered();
                ss->SpecsDirty = false;
            }
        }
        if (m_scrollToTop) {
            ImGui::SetScrollY(0.0f);
            m_scrollToTop = false;
        }

        ImGuiListClipper clipper;
        clipper.Begin((int)m_filtered.size(), rowH + ImGui::GetStyle().CellPadding.y * 2);
        while (clipper.Step()) {
            for (int pos = clipper.DisplayStart; pos < clipper.DisplayEnd; pos++) {
                int idx = m_filtered[pos];
                const Track& t = tr[idx];
                bool isSel = m_sel[idx] != 0;
                ImGui::PushID(idx);
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
                if (isSel) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, col32(withAlpha(T.accent, 0.13f)));
                ImGui::TableSetColumnIndex(0);
                ImVec2 cp = ImGui::GetCursorPos();
                if (ImGui::Selectable("##row", isSel,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                          ImGuiSelectableFlags_AllowDoubleClick,
                                      ImVec2(0, rowH))) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) addTrack(idx);
                    else clickRow(idx, pos, io.KeyShift);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                    ImGui::SetTooltip("%s", t.path.c_str());
                drawTrackContextMenu(idx);
                ImGui::SameLine();
                ImGui::SetCursorPos(cp);
                bool chk = isSel;
                if (ImGui::Checkbox("##c", &chk)) {
                    setSelected(idx, chk);
                    m_selAnchor = pos;
                }
                ImGui::TableSetColumnIndex(1);
                if (t.trackNo > 0) ImGui::TextDisabled("%d", t.trackNo);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(t.title.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(t.artist.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(t.album.c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextDisabled("%s", fmtDuration(t.duration).c_str());
                ImGui::TableSetColumnIndex(6);
                ImGui::TextDisabled("%s", t.folder.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

void App::rebuildTree() {
    const auto& tr = m_lib.tracks();
    const auto& roots = m_lib.roots();
    m_tree.clear();
    m_treeRoots.clear();
    std::vector<std::unordered_map<std::string, int>> childMap;
    auto newNode = [&](const std::string& name) {
        m_tree.push_back(TreeNode{name, {}, {}, 0});
        childMap.emplace_back();
        return (int)m_tree.size() - 1;
    };
    for (auto& r : roots) {
        std::string name = u8s(u8p(r).filename());
        if (name.empty()) name = r;
        m_treeRoots.push_back(newNode(name));
    }
    for (int idx : m_filtered) {
        const Track& t = tr[idx];
        if (t.root < 0 || t.root >= (int)m_treeRoots.size()) continue;
        int cur = m_treeRoots[t.root];
        size_t start = 0;
        while (start < t.folder.size()) {
            size_t end = t.folder.find('/', start);
            if (end == std::string::npos) end = t.folder.size();
            std::string seg = t.folder.substr(start, end - start);
            start = end + 1;
            if (seg.empty()) continue;
            auto it = childMap[cur].find(seg);
            if (it == childMap[cur].end()) {
                int nn = newNode(seg);
                childMap[cur][seg] = nn;
                m_tree[cur].kids.push_back(nn);
                cur = nn;
            } else {
                cur = it->second;
            }
        }
        m_tree[cur].tracks.push_back(idx);
    }
    for (int n = (int)m_tree.size() - 1; n >= 0; n--) {
        TreeNode& node = m_tree[n];
        node.count = (int)node.tracks.size();
        for (int k : node.kids) node.count += m_tree[k].count;
        std::sort(node.kids.begin(), node.kids.end(),
                  [&](int a, int b) { return ciCompare(m_tree[a].name, m_tree[b].name) < 0; });
        std::sort(node.tracks.begin(), node.tracks.end(), [&](int a, int b) {
            if (tr[a].trackNo != tr[b].trackNo) return tr[a].trackNo < tr[b].trackNo;
            return ciCompare(tr[a].path, tr[b].path) < 0;
        });
    }
    m_treeDirty = false;
}

void App::addSubtree(int node) {
    std::vector<int> stack{node};
    int n = 0;
    while (!stack.empty()) {
        int cur = stack.back();
        stack.pop_back();
        for (int idx : m_tree[cur].tracks) { addTrack(idx, false); n++; }
        for (int k = (int)m_tree[cur].kids.size() - 1; k >= 0; k--) stack.push_back(m_tree[cur].kids[k]);
    }
    toast("Added " + fmtCount(n) + " tracks");
}

void App::drawTreeNode(int n, int depth) {
    const Theme& T = curTheme(m_cfg);
    const auto& tr = m_lib.tracks();
    ImGuiIO& io = ImGui::GetIO();
    const TreeNode& node = m_tree[n];
    if (node.count == 0) return;
    float rowH = ImGui::GetFrameHeight();
    ImGui::PushID(n);
    float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
                               ImGuiTreeNodeFlags_FramePadding;
    if (depth == 0) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * m_dpi, 3 * m_dpi));
    bool open = ImGui::TreeNodeEx("##node", flags, "%s", node.name.c_str());
    ImGui::PopStyleVar();
    std::string cnt = fmtCount(node.count);
    float cntW = textW(cnt.c_str());
    float addW = buttonW("+ all");
    ImGui::SameLine();
    ImGui::SetCursorPosX(rowRight - cntW - addW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::TextDisabled("%s", cnt.c_str());
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(T.accent, 0.2f));
    if (ImGui::SmallButton("+ all")) addSubtree(n);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Add every track in this folder (recursively)");

    if (open) {
        for (int k : node.kids) drawTreeNode(k, depth + 1);
        for (int idx : node.tracks) {
            const Track& t = tr[idx];
            bool isSel = m_sel[idx] != 0;
            ImGui::PushID(idx);
            ImVec2 cp = ImGui::GetCursorPos();
            if (isSel) {
                ImVec2 sp = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(sp.x - 4, sp.y), ImVec2(sp.x + ImGui::GetContentRegionAvail().x, sp.y + rowH),
                                                          col32(withAlpha(T.accent, 0.13f)), 4.0f);
            }
            if (ImGui::Selectable("##t", isSel, ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0, rowH))) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) addTrack(idx);
                else clickRow(idx, -1, io.KeyShift);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                ImGui::SetTooltip("%s", t.path.c_str());
            drawTrackContextMenu(idx);
            ImGui::SameLine();
            ImGui::SetCursorPos(cp);
            bool chk = isSel;
            if (ImGui::Checkbox("##c", &chk)) setSelected(idx, chk);
            ImGui::SameLine();
            if (t.trackNo > 0) {
                ImGui::TextDisabled("%02d", t.trackNo);
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(t.title.c_str());
            if (!t.artist.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("- %s", t.artist.c_str());
            }
            std::string dur = fmtDuration(t.duration);
            ImGui::SameLine();
            ImGui::SetCursorPosX(rowRight - textW(dur.c_str()));
            ImGui::TextDisabled("%s", dur.c_str());
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void App::drawLibraryTree() {
    if (m_treeDirty) rebuildTree();
    if (ImGui::BeginChild("##tree", ImVec2(0, 0), ImGuiChildFlags_None)) {
        if (m_scrollToTop) {
            ImGui::SetScrollY(0.0f);
            m_scrollToTop = false;
        }
        for (int r : m_treeRoots) drawTreeNode(r, 0);
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------- playlist

void App::playlistTouched() {
    m_pl.dirty = true;
    m_plVersion++;
}

void App::refreshPlaylistCaches() {
    size_t n = m_pl.items.size();
    if (m_plSel.size() != n) m_plSel.resize(n, 0);
    if (m_plCacheVersion == m_plVersion) return;
    m_plCacheVersion = m_plVersion;
    m_plMissing.assign(n, 0);
    m_plDupe.assign(n, 0);
    m_plMissingCount = m_plDupeCount = 0;
    std::unordered_map<std::string, int> seen;
    seen.reserve(n * 2);
    for (size_t i = 0; i < n; i++) {
        auto& e = m_pl.items[i];
        std::string key = pathKey(u8p(e.path));
        if (const Track* t = m_lib.find(e.path)) {
            if (e.duration == 0) e.duration = t->duration;
            if (e.display.empty()) e.display = t->display;
        } else {
            std::error_code ec;
            if (!fs::exists(u8p(e.path), ec)) { m_plMissing[i] = 1; m_plMissingCount++; }
        }
        int& c = seen[key];
        if (c++ > 0) { m_plDupe[i] = 1; m_plDupeCount++; }
    }
}

void App::newPlaylist() {
    m_pl = Playlist{};
    m_plSel.clear();
    m_plAnchor = -1;
    m_plVersion++;
    m_cfg.lastPlaylist.clear();
}

bool App::openPlaylist(const fs::path& p) {
    auto pl = Playlist::load(p);
    if (!pl) {
        toast("Could not open " + u8s(p.filename()), 2);
        return false;
    }
    m_pl = std::move(*pl);
    m_plSel.assign(m_pl.items.size(), 0);
    m_plAnchor = -1;
    m_plVersion++;
    m_cfg.lastPlaylist = u8s(p);
    m_glyphCheck = true;
    toast("Opened " + m_pl.name + " (" + fmtCount(m_pl.items.size()) + " tracks)");
    return true;
}

bool App::savePlaylist(bool saveAs) {
    if (m_pl.items.empty() && m_pl.file.empty()) {
        toast("Nothing to save yet", 1);
        return false;
    }
    if (!saveAs && !m_pl.file.empty()) {
        bool ok = m_pl.save(m_pl.file, m_cfg.saveOpts);
        toast(ok ? "Saved " + u8s(m_pl.file.filename()) : "Save failed", ok ? 0 : 2);
        if (ok) { m_cfg.lastPlaylist = u8s(m_pl.file); refreshPlaylistFiles(true); }
        return ok;
    }
    std::string def = sanitizeFilename(m_pl.name) + ".m3u8";
    fs::path dir = u8p(m_cfg.playlistDir);
    std::error_code ec;
    fs::create_directories(dir, ec);
    auto r = platform::saveFile(m_native, dir, def);
    if (r.status == platform::DialogStatus::Picked) {
        fs::path target = r.path;
        std::string ext = toLower(u8s(target.extension()));
        if (ext != ".m3u8" && ext != ".m3u") target += ".m3u8";
        bool ok = m_pl.save(target, m_cfg.saveOpts);
        toast(ok ? "Saved " + u8s(target.filename()) : "Save failed", ok ? 0 : 2);
        if (ok) { m_cfg.lastPlaylist = u8s(target); refreshPlaylistFiles(true); }
        return ok;
    }
    if (r.status == platform::DialogStatus::Unavailable) {
        m_browseFor = BrowseFor::SavePlaylist;
        m_browser.open(FileBrowser::Mode::SaveFile, dir, def);
    }
    return false;
}

void App::startOpenPlaylistDialog() {
    fs::path dir = u8p(m_cfg.playlistDir);
    auto r = platform::openFile(m_native, dir);
    if (r.status == platform::DialogStatus::Picked) performRequest(Pending::OpenPlaylist, r.path);
    else if (r.status == platform::DialogStatus::Unavailable) {
        m_browseFor = BrowseFor::OpenPlaylist;
        m_browser.open(FileBrowser::Mode::OpenFile, dir);
    }
}

void App::performRequest(Pending p, const fs::path& arg) {
    m_pending = p;
    m_pendingArg = arg;
    if (m_pl.dirty) m_openUnsavedModal = true;
    else doPending();
}

void App::doPending() {
    Pending p = m_pending;
    fs::path arg = m_pendingArg;
    m_pending = Pending::None;
    m_pendingArg.clear();
    switch (p) {
        case Pending::NewPlaylist: newPlaylist(); break;
        case Pending::OpenPlaylist: openPlaylist(arg); break;
        case Pending::Quit: m_quit = true; break;
        default: break;
    }
}

void App::removePlaylistSelected() {
    std::vector<PlaylistEntry> keep;
    keep.reserve(m_pl.items.size());
    int removed = 0;
    for (size_t i = 0; i < m_pl.items.size(); i++) {
        if (i < m_plSel.size() && m_plSel[i]) { removed++; continue; }
        keep.push_back(std::move(m_pl.items[i]));
    }
    if (!removed) return;
    m_pl.items = std::move(keep);
    m_plSel.assign(m_pl.items.size(), 0);
    m_plAnchor = -1;
    playlistTouched();
    toast("Removed " + fmtCount(removed) + " track" + (removed == 1 ? "" : "s"));
}

void App::drawPlaylist() {
    const Theme& T = curTheme(m_cfg);
    ImGuiIO& io = ImGui::GetIO();
    refreshPlaylistCaches();
    float rowH = ImGui::GetFrameHeight();

    ImGui::PushFont(m_fontBold ? m_fontBold : ImGui::GetFont());
    ImGui::TextColored(T.accent2, "PLAYLIST");
    ImGui::PopFont();
    if (m_pl.dirty) {
        ImGui::SameLine();
        ImGui::TextColored(T.yellow, "* unsaved");
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, withAlpha(T.surface, 0.6f));
    if (ImGui::InputTextWithHint("##plname", "Playlist name", &m_pl.name)) playlistTouched();
    ImGui::PopStyleColor();

    if (ImGui::Button("New")) performRequest(Pending::NewPlaylist);
    ImGui::SetItemTooltip("Ctrl+N");
    ImGui::SameLine();
    if (ImGui::Button("Open")) startOpenPlaylistDialog();
    ImGui::SetItemTooltip("Ctrl+O");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(T.accent, m_pl.dirty ? 0.4f : 0.18f));
    if (ImGui::Button("Save")) savePlaylist(false);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Ctrl+S");
    ImGui::SameLine();
    if (ImGui::Button("Save as")) savePlaylist(true);
    ImGui::SetItemTooltip("Ctrl+Shift+S");
    ImGui::SameLine();
    if (ImGui::Button("Format")) ImGui::OpenPopup("plopts");
    if (ImGui::BeginPopup("plopts")) {
        ImGui::TextDisabled("How paths are written in the .m3u8");
        ImGui::Checkbox("Relative to the playlist file", &m_cfg.saveOpts.relative);
        ImGui::Checkbox("Backslash separators (HiBy / Windows players)", &m_cfg.saveOpts.backslashes);
        ImGui::Checkbox("Windows line endings (CRLF)", &m_cfg.saveOpts.crlf);
        ImGui::EndPopup();
    }

    // stats
    std::string stats = fmtCount(m_pl.items.size()) + (m_pl.items.size() == 1 ? " track" : " tracks") + "   " +
                        fmtDuration(m_pl.totalDuration());
    ImGui::TextDisabled("%s", stats.c_str());
    if (m_plMissingCount) {
        ImGui::SameLine();
        ImGui::TextColored(T.red, "%d missing", m_plMissingCount);
    }
    if (m_plDupeCount) {
        ImGui::SameLine();
        ImGui::TextColored(T.yellow, "%d duplicate%s", m_plDupeCount, m_plDupeCount == 1 ? "" : "s");
    }
    if (!m_pl.file.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", u8s(m_pl.file).c_str());

    int plSelCount = 0;
    for (auto s : m_plSel) plSelCount += s;
    ImGui::BeginDisabled(plSelCount == 0);
    if (ImGui::SmallButton("Remove")) removePlaylistSelected();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Remove selected (Del)");
    ImGui::SameLine();
    ImGui::BeginDisabled(m_pl.items.empty());
    if (ImGui::SmallButton("Shuffle")) {
        std::shuffle(m_pl.items.begin(), m_pl.items.end(), std::mt19937{std::random_device{}()});
        std::fill(m_plSel.begin(), m_plSel.end(), 0);
        playlistTouched();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Sort A-Z")) {
        std::stable_sort(m_pl.items.begin(), m_pl.items.end(),
                         [](const PlaylistEntry& a, const PlaylistEntry& b) { return ciCompare(a.display, b.display) < 0; });
        std::fill(m_plSel.begin(), m_plSel.end(), 0);
        playlistTouched();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_plDupeCount == 0);
    if (ImGui::SmallButton("Dedupe")) {
        std::vector<PlaylistEntry> keep;
        for (size_t i = 0; i < m_pl.items.size(); i++)
            if (!m_plDupe[i]) keep.push_back(m_pl.items[i]);
        m_pl.items = std::move(keep);
        m_plSel.assign(m_pl.items.size(), 0);
        playlistTouched();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_plMissingCount == 0);
    if (ImGui::SmallButton("Drop missing")) {
        std::vector<PlaylistEntry> keep;
        for (size_t i = 0; i < m_pl.items.size(); i++)
            if (!m_plMissing[i]) keep.push_back(m_pl.items[i]);
        m_pl.items = std::move(keep);
        m_plSel.assign(m_pl.items.size(), 0);
        playlistTouched();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        m_pl.items.clear();
        m_plSel.clear();
        playlistTouched();
    }
    ImGui::EndDisabled();

    // list
    int removeOne = -1;
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_BordersOuterH;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6 * m_dpi, 2 * m_dpi));
    if (ImGui::BeginTable("pl", 4, flags, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36 * m_dpi);
        ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 56 * m_dpi);
        ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderLabel, rowH);
        ImGui::TableHeadersRow();

        int n = (int)m_pl.items.size();
        ImGuiListClipper clipper;
        clipper.Begin(n, rowH + ImGui::GetStyle().CellPadding.y * 2);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                PlaylistEntry& e = m_pl.items[i];
                bool isSel = m_plSel[i] != 0;
                ImGui::PushID(i);
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
                if (m_plMissing[i]) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, col32(withAlpha(T.red, 0.16f)));
                else if (m_plDupe[i]) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, col32(withAlpha(T.yellow, 0.10f)));
                ImGui::TableSetColumnIndex(0);
                ImVec2 cp = ImGui::GetCursorPos();
                if (ImGui::Selectable("##r", isSel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                      ImVec2(0, rowH))) {
                    if (io.KeyShift && m_plAnchor >= 0 && m_plAnchor < n) {
                        if (!io.KeyCtrl) std::fill(m_plSel.begin(), m_plSel.end(), 0);
                        int a = std::min(m_plAnchor, i), b = std::max(m_plAnchor, i);
                        for (int k = a; k <= b; k++) m_plSel[k] = 1;
                    } else if (io.KeyCtrl) {
                        m_plSel[i] = !m_plSel[i];
                        m_plAnchor = i;
                    } else {
                        std::fill(m_plSel.begin(), m_plSel.end(), 0);
                        m_plSel[i] = 1;
                        m_plAnchor = i;
                    }
                }
                // drag to reorder
                if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                    float dy = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y;
                    int next = i + (dy < 0.0f ? -1 : 1);
                    if (next >= 0 && next < n) {
                        std::swap(m_pl.items[i], m_pl.items[next]);
                        std::swap(m_plSel[i], m_plSel[next]);
                        std::swap(m_plMissing[i], m_plMissing[next]);
                        std::swap(m_plDupe[i], m_plDupe[next]);
                        m_plAnchor = next;
                        ImGui::ResetMouseDragDelta();
                        m_pl.dirty = true;
                        m_plVersion++;
                        m_plCacheVersion = m_plVersion;  // caches were swapped in step; no rebuild needed
                    }
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                    ImGui::SetTooltip("%s%s", e.path.c_str(), m_plMissing[i] ? "\n(file not found)" : "");
                if (ImGui::BeginPopupContextItem("plrow")) {
                    if (ImGui::MenuItem("Remove")) removeOne = i;
                    if (ImGui::MenuItem("Move to top")) {
                        std::rotate(m_pl.items.begin(), m_pl.items.begin() + i, m_pl.items.begin() + i + 1);
                        playlistTouched();
                    }
                    if (ImGui::MenuItem("Move to bottom")) {
                        std::rotate(m_pl.items.begin() + i, m_pl.items.begin() + i + 1, m_pl.items.end());
                        playlistTouched();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Reveal in file manager")) platform::revealInFileManager(u8p(e.path));
                    if (ImGui::MenuItem("Copy path")) ImGui::SetClipboardText(e.path.c_str());
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                ImGui::SetCursorPos(cp);
                ImGui::TextDisabled("%d", i + 1);
                ImGui::TableSetColumnIndex(1);
                if (m_plMissing[i]) ImGui::TextColored(T.red, "%s", e.display.c_str());
                else ImGui::TextUnformatted(e.display.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", fmtDuration(e.duration).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, withAlpha(T.red, 0.5f));
                if (ImGui::SmallButton("x")) removeOne = i;
                ImGui::PopStyleColor(2);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    if (removeOne >= 0 && removeOne < (int)m_pl.items.size()) {
        m_pl.items.erase(m_pl.items.begin() + removeOne);
        m_plSel.erase(m_plSel.begin() + removeOne);
        playlistTouched();
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) removePlaylistSelected();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) std::fill(m_plSel.begin(), m_plSel.end(), 1);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) std::fill(m_plSel.begin(), m_plSel.end(), 0);
        int n = (int)m_pl.items.size();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_UpArrow)) {
            for (int i = 1; i < n; i++)
                if (m_plSel[i] && !m_plSel[i - 1]) { std::swap(m_pl.items[i], m_pl.items[i - 1]); std::swap(m_plSel[i], m_plSel[i - 1]); }
            playlistTouched();
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_DownArrow)) {
            for (int i = n - 2; i >= 0; i--)
                if (m_plSel[i] && !m_plSel[i + 1]) { std::swap(m_pl.items[i], m_pl.items[i + 1]); std::swap(m_plSel[i], m_plSel[i + 1]); }
            playlistTouched();
        }
    }
}

// ---------------------------------------------------------------------------- status bar

void App::drawStatusBar() {
    const Theme& T = curTheme(m_cfg);
    ImGui::Dummy(ImVec2(0, 2 * m_dpi));
    if (m_lib.scanning()) {
        Spinner(ImGui::GetFontSize() * 0.5f, 2.0f * m_dpi, col32(T.accent));
        ImGui::SameLine();
        ImGui::TextColored(T.accent, "Scanning");
        ImGui::SameLine();
        ImGui::TextDisabled("%s files found, %s read", fmtCount(m_lib.scanFound()).c_str(), fmtCount(m_lib.scanTagged()).c_str());
    } else {
        ImGui::TextDisabled("%s tracks in %d folder%s", fmtCount(m_lib.tracks().size()).c_str(), (int)m_lib.roots().size(),
                            m_lib.roots().size() == 1 ? "" : "s");
    }
    ImGui::SameLine();
    std::string right = "double-click or Enter adds   -   drag rows to reorder   -   Del removes";
    float rw = textW(right.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - rw);
    ImGui::TextDisabled("%s", right.c_str());
}

// ---------------------------------------------------------------------------- dialogs / modals

void App::addRoot(const fs::path& p) {
    std::error_code ec;
    fs::path np = fs::absolute(p, ec).lexically_normal();
    std::string s = u8s(np);
    while (s.size() > 3 && (s.back() == '\\' || s.back() == '/')) s.pop_back();
    if (!fs::is_directory(u8p(s), ec)) {
        toast("Not a folder: " + s, 2);
        return;
    }
    std::string key = pathKey(u8p(s));
    for (auto& r : m_lib.roots())
        if (pathKey(u8p(r)) == key) {
            toast("Already in library", 1);
            return;
        }
    auto roots = m_lib.roots();
    roots.push_back(s);
    m_lib.setRoots(roots);
    m_lib.rescan();
    toast("Scanning " + s);
}

void App::startAddRootDialog() {
    fs::path start = m_lib.roots().empty() ? fs::path() : u8p(m_lib.roots().back()).parent_path();
    auto r = platform::pickFolder(m_native, start);
    if (r.status == platform::DialogStatus::Picked) addRoot(r.path);
    else if (r.status == platform::DialogStatus::Unavailable) {
        m_browseFor = BrowseFor::AddRoot;
        m_browser.open(FileBrowser::Mode::Folder, start);
    }
}

void App::handleBrowserResult(const fs::path& p) {
    BrowseFor what = m_browseFor;
    m_browseFor = BrowseFor::None;
    switch (what) {
        case BrowseFor::AddRoot: addRoot(p); break;
        case BrowseFor::OpenPlaylist: performRequest(Pending::OpenPlaylist, p); break;
        case BrowseFor::PlaylistDir: m_cfg.playlistDir = u8s(p); refreshPlaylistFiles(true); break;
        case BrowseFor::SavePlaylist: {
            bool ok = m_pl.save(p, m_cfg.saveOpts);
            toast(ok ? "Saved " + u8s(p.filename()) : "Save failed", ok ? 0 : 2);
            if (ok) {
                m_cfg.lastPlaylist = u8s(p);
                refreshPlaylistFiles(true);
                if (m_pending != Pending::None) doPending();
            }
            break;
        }
        default: break;
    }
}

void App::onDrop(const std::vector<fs::path>& paths) {
    int added = 0;
    for (auto& p : paths) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            addRoot(p);
        } else {
            std::string ext = toLower(u8s(p.extension()));
            if (ext == ".m3u8" || ext == ".m3u") {
                performRequest(Pending::OpenPlaylist, p);
            } else if (isAudioFile(p)) {
                if (const Track* t = m_lib.find(u8s(p))) {
                    m_pl.items.push_back({t->path, t->display, t->duration});
                } else {
                    Track nt = readTrack(p, -1, "");
                    m_pl.items.push_back({nt.path, nt.display, nt.duration});
                }
                added++;
            }
        }
    }
    if (added) {
        playlistTouched();
        m_glyphCheck = true;
        toast("Added " + fmtCount(added) + " dropped track" + (added == 1 ? "" : "s"));
    }
}

void App::drawModals() {
    const Theme& T = curTheme(m_cfg);
    if (m_openUnsavedModal) {
        ImGui::OpenPopup("Unsaved changes");
        m_openUnsavedModal = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("\"%s\" has unsaved changes.", m_pl.name.c_str());
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(T.accent, 0.4f));
        if (ImGui::Button("Save", ImVec2(110 * m_dpi, 0))) {
            if (savePlaylist(false)) {
                doPending();
                ImGui::CloseCurrentPopup();
            } else if (m_browser.isOpen()) {
                ImGui::CloseCurrentPopup();  // in-app save dialog will finish the pending action
            }
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(110 * m_dpi, 0))) {
            m_pl.dirty = false;
            doPending();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110 * m_dpi, 0))) {
            m_pending = Pending::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    drawSettings();
}

void App::drawSettings() {
    const Theme& T = curTheme(m_cfg);
    if (m_openSettings) {
        ImGui::OpenPopup("Settings");
        m_openSettings = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620 * m_dpi, 0), ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::BeginPopupModal("Settings", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SeparatorText("Appearance");
        auto& th = themes();
        const char* cur = th[std::clamp(m_cfg.theme, 0, (int)th.size() - 1)].name;
        ImGui::SetNextItemWidth(260 * m_dpi);
        if (ImGui::BeginCombo("Theme", cur)) {
            for (int i = 0; i < (int)th.size(); i++) {
                ImGui::PushID(i);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                float sw = ImGui::GetFontSize() * 0.8f;
                dl->AddRectFilled(p, ImVec2(p.x + sw, p.y + sw), col32(th[i].bg), 3.0f);
                dl->AddRectFilled(ImVec2(p.x + sw + 3, p.y), ImVec2(p.x + sw * 2 + 3, p.y + sw), col32(th[i].accent), 3.0f);
                dl->AddRectFilled(ImVec2(p.x + sw * 2 + 6, p.y), ImVec2(p.x + sw * 3 + 6, p.y + sw), col32(th[i].accent2), 3.0f);
                ImGui::Dummy(ImVec2(sw * 3 + 6, sw));
                ImGui::SameLine();
                if (ImGui::Selectable(th[i].name, i == m_cfg.theme)) {
                    m_cfg.theme = i;
                    applyThemeNow();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        static float scaleEdit = -1.0f;
        if (scaleEdit < 0.0f) scaleEdit = m_cfg.uiScale;
        ImGui::SetNextItemWidth(260 * m_dpi);
        ImGui::SliderFloat("UI scale", &scaleEdit, 0.7f, 2.0f, "%.2fx");
        if (ImGui::IsItemDeactivatedAfterEdit() && std::fabs(scaleEdit - m_cfg.uiScale) > 0.01f) m_pendingScale = scaleEdit;

        ImGui::SeparatorText("Playlists");
        ImGui::SetNextItemWidth(-(buttonW("Browse") + ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::InputText("##pldir", &m_cfg.playlistDir)) refreshPlaylistFiles(true);
        ImGui::SameLine();
        if (ImGui::Button("Browse")) {
            auto r = platform::pickFolder(m_native, u8p(m_cfg.playlistDir));
            if (r.status == platform::DialogStatus::Picked) {
                m_cfg.playlistDir = u8s(r.path);
                refreshPlaylistFiles(true);
            } else if (r.status == platform::DialogStatus::Unavailable) {
                m_browseFor = BrowseFor::PlaylistDir;
                m_browser.open(FileBrowser::Mode::Folder, u8p(m_cfg.playlistDir));
            }
        }
        ImGui::TextDisabled("Playlists in this folder are listed in the sidebar.");
        ImGui::Checkbox("Write paths relative to the playlist file", &m_cfg.saveOpts.relative);
        ImGui::Checkbox("Backslash separators (HiBy / Windows players)", &m_cfg.saveOpts.backslashes);
        ImGui::Checkbox("Windows line endings (CRLF)", &m_cfg.saveOpts.crlf);

        ImGui::SeparatorText("Library folders");
        int removeIdx = -1;
        for (int i = 0; i < (int)m_lib.roots().size(); i++) {
            ImGui::PushID(i);
            if (ImGui::SmallButton("Remove")) removeIdx = i;
            ImGui::SameLine();
            ImGui::TextUnformatted(m_lib.roots()[i].c_str());
            ImGui::PopID();
        }
        if (removeIdx >= 0) {
            m_lib.removeRoot(removeIdx);
            if (m_rootFilter == removeIdx) m_rootFilter = -1;
            else if (m_rootFilter > removeIdx) m_rootFilter--;
        }
        if (ImGui::SmallButton("+ Add folder")) startAddRootDialog();

        ImGui::Spacing();
        ImGui::TextDisabled("Config: %s", u8s(m_configDir).c_str());
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(T.accent, 0.4f));
        if (ImGui::Button("Done", ImVec2(120 * m_dpi, 0))) {
            m_cfg.save(m_configDir / "config.json");
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
    if (!open) m_cfg.save(m_configDir / "config.json");
}

void App::drawToasts() {
    if (m_toasts.empty()) return;
    const Theme& T = curTheme(m_cfg);
    double now = ImGui::GetTime();
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(), [&](const Toast& t) { return t.until < now; }),
                   m_toasts.end());
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float pad = 10 * m_dpi;
    float y = vp->WorkPos.y + vp->WorkSize.y - 36 * m_dpi;
    for (int i = (int)m_toasts.size() - 1; i >= 0; i--) {
        const Toast& t = m_toasts[i];
        float life = (float)(t.until - now);
        float alpha = std::clamp(life / 0.4f, 0.0f, 1.0f);
        ImVec2 ts = ImGui::CalcTextSize(t.text.c_str());
        float w = ts.x + pad * 2, h = ts.y + pad * 1.2f;
        ImVec2 a(vp->WorkPos.x + vp->WorkSize.x - w - 16 * m_dpi, y - h);
        ImVec2 b(a.x + w, a.y + h);
        ImVec4 accent = t.kind == 2 ? T.red : t.kind == 1 ? T.yellow : T.accent;
        dl->AddRectFilled(a, b, col32(withAlpha(T.bgAlt, 0.95f * alpha)), 8.0f);
        dl->AddRect(a, b, col32(withAlpha(accent, 0.8f * alpha)), 8.0f, 0, 1.5f);
        dl->AddRectFilled(ImVec2(a.x, a.y + 6), ImVec2(a.x + 3, b.y - 6), col32(withAlpha(accent, alpha)), 2.0f);
        dl->AddText(ImVec2(a.x + pad, a.y + pad * 0.6f), col32(withAlpha(T.text, alpha)), t.text.c_str());
        y = a.y - 8 * m_dpi;
    }
}
