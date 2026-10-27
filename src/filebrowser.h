#pragma once
#include "util.h"
#include <string>
#include <vector>

// Minimal in-app folder / file picker. Used when no native dialog is available
// (e.g. Linux without zenity/kdialog) and always usable from the UI.
class FileBrowser {
public:
    enum class Mode { Folder, SaveFile, OpenFile };

    void open(Mode mode, const fs::path& start, const std::string& defaultName = "");
    bool draw();  // returns true once when the user confirmed; see result()
    const fs::path& result() const { return m_result; }
    bool isOpen() const { return m_open; }

private:
    struct Entry {
        std::string name;
        fs::path path;
        bool dir;
    };
    void navigate(const fs::path& p);
    void refresh();

    Mode m_mode = Mode::Folder;
    bool m_open = false;
    bool m_pendingOpen = false;
    fs::path m_cwd;
    fs::path m_result;
    std::string m_pathBuf;
    std::string m_nameBuf;
    std::string m_error;
    std::vector<Entry> m_entries;
    int m_selected = -1;
};
