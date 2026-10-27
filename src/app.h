#pragma once
#include "filebrowser.h"
#include "library.h"
#include "playlist.h"
#include "util.h"
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

struct Config {
    std::string playlistDir;
    int theme = 0;
    float uiScale = 1.0f;
    SaveOptions saveOpts;
    int winW = 1500, winH = 920;
    float sidebarW = 280.0f, playlistW = 500.0f;
    int viewMode = 0;  // 0 = table, 1 = folder tree
    std::string lastPlaylist;

    void load(const fs::path& f);
    void save(const fs::path& f) const;
};

class App {
public:
    explicit App(GLFWwindow* window);
    ~App();

    void frame();                                   // draw one frame (inside ImGui NewFrame/Render)
    bool requestQuit();                             // true = may close now
    bool shouldQuit() const { return m_quit; }
    bool takeFontRebuild(float& scaleOut);          // true when UI scale changed
    void setFonts(ImFont* regular, ImFont* bold) { m_fontRegular = regular; m_fontBold = bold; }
    void setDpi(float d) { m_dpi = d; }
    void applyThemeNow();
    void onDrop(const std::vector<fs::path>& paths);
    const Config& config() const { return m_cfg; }
    Config& config() { return m_cfg; }
    void saveState();
    ImVec4 clearColor() const;

    // Every non-ASCII character that appears anywhere in the library / playlists, as one UTF-8
    // string. main() feeds it to the font builder so exotic glyphs are rasterised on demand.
    std::string glyphText() const;
    bool fontsDirty() const { return m_fontsDirty; }
    void fontsBuilt() { m_fontsDirty = false; m_glyphTextBuilt = glyphText(); }

private:
    enum class Pending { None, NewPlaylist, OpenPlaylist, Quit };
    enum class BrowseFor { None, AddRoot, SavePlaylist, OpenPlaylist, PlaylistDir };

    // layout
    void drawHeader();
    void drawSidebar();
    void drawLibrary();
    void drawLibraryTable();
    void drawLibraryTree();
    void drawTreeNode(int n, int depth);
    void drawTrackContextMenu(int idx);
    void drawPlaylist();
    void drawStatusBar();
    void drawModals();
    void drawSettings();
    void drawToasts();

    // library helpers
    void refreshFiltered();
    void sortFiltered();
    void rebuildTree();
    void clickRow(int idx, int pos, bool shift);
    void setSelected(int idx, bool on);
    void clearSelection();
    void selectAllFiltered();
    void addSelectedToPlaylist();
    void addTrack(int idx, bool notify = true);
    void addSubtree(int node);
    void addWhere(int what, const Track& like);  // 0 album, 1 artist, 2 folder

    // playlist helpers
    void playlistTouched();
    void refreshPlaylistCaches();
    void newPlaylist();
    bool openPlaylist(const fs::path& p);
    bool savePlaylist(bool saveAs);
    void startOpenPlaylistDialog();
    void doPending();
    void performRequest(Pending p, const fs::path& arg = {});
    void refreshPlaylistFiles(bool force = false);
    void removePlaylistSelected();

    // roots
    void addRoot(const fs::path& p);
    void startAddRootDialog();
    void handleBrowserResult(const fs::path& p);

    void toast(const std::string& msg, int kind = 0);

    GLFWwindow* m_window = nullptr;
    void* m_native = nullptr;
    Config m_cfg;
    fs::path m_configDir;
    Library m_lib;
    uint64_t m_savedLibVersion = 0;
    Playlist m_pl;
    FileBrowser m_browser;
    BrowseFor m_browseFor = BrowseFor::None;
    ImFont* m_fontRegular = nullptr;
    ImFont* m_fontBold = nullptr;
    float m_dpi = 1.0f;

    // library view state
    std::string m_query;
    std::vector<std::string> m_queryWords;
    std::vector<int> m_filtered;
    uint64_t m_filteredVersion = 0;
    std::string m_filteredQuery;
    int m_rootFilter = -1;
    int m_filteredRoot = -2;
    int m_sortCol = 2;
    bool m_sortAsc = true;
    bool m_sortDirty = true;
    std::vector<uint8_t> m_sel;
    uint64_t m_selVersion = 0;
    int m_selCount = 0;
    int m_selAnchor = -1;
    bool m_focusSearch = false;
    bool m_scrollToTop = false;

    struct TreeNode {
        std::string name;
        std::vector<int> kids;
        std::vector<int> tracks;
        int count = 0;
    };
    std::vector<TreeNode> m_tree;
    std::vector<int> m_treeRoots;
    bool m_treeDirty = true;

    // playlist view state
    std::vector<uint8_t> m_plSel;
    int m_plAnchor = -1;
    int m_plVersion = 0;
    int m_plCacheVersion = -1;
    std::vector<uint8_t> m_plMissing;
    std::vector<uint8_t> m_plDupe;
    int m_plMissingCount = 0, m_plDupeCount = 0;
    std::vector<fs::path> m_playlistFiles;
    double m_playlistFilesAt = -100.0;

    // modals
    Pending m_pending = Pending::None;
    fs::path m_pendingArg;
    bool m_openUnsavedModal = false;
    bool m_openSettings = false;
    bool m_quit = false;
    float m_pendingScale = 0.0f;
    bool m_fontsDirty = false;
    bool m_glyphCheck = true;
    uint64_t m_glyphLibVersion = 0;
    std::string m_glyphTextBuilt;

    struct Toast {
        std::string text;
        double until;
        int kind;
    };
    std::vector<Toast> m_toasts;
};
