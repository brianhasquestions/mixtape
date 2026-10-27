#include "platform.h"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <shellapi.h>
#else
#include <GLFW/glfw3.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace platform {

fs::path configDir() {
#ifdef _WIN32
    if (const char* a = std::getenv("APPDATA")) return fs::path(a) / "mixtape";
    return fs::temp_directory_path() / "mixtape";
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME")) return fs::path(x) / "mixtape";
    if (const char* h = std::getenv("HOME")) return fs::path(h) / ".config" / "mixtape";
    return fs::temp_directory_path() / "mixtape";
#endif
}

fs::path defaultPlaylistDir() {
#ifdef _WIN32
    if (const char* u = std::getenv("USERPROFILE")) return fs::path(u) / "Music" / "Playlists";
#else
    if (const char* h = std::getenv("HOME")) return fs::path(h) / "Music" / "Playlists";
#endif
    return fs::current_path() / "Playlists";
}

std::vector<fs::path> roots() {
    std::vector<fs::path> out;
#ifdef _WIN32
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++)
        if (mask & (1u << i)) {
            std::string d = std::string(1, (char)('A' + i)) + ":\\";
            out.emplace_back(d);
        }
#else
    out.emplace_back("/");
    if (const char* h = std::getenv("HOME")) out.emplace_back(h);
    for (const char* m : {"/media", "/mnt", "/run/media"})
        if (fs::exists(m)) out.emplace_back(m);
#endif
    return out;
}

void* nativeHandle(GLFWwindow* w) {
#ifdef _WIN32
    return (void*)glfwGetWin32Window(w);
#else
    (void)w;
    return nullptr;
#endif
}

void applyDarkTitleBar(GLFWwindow* w) {
#ifdef _WIN32
    HWND hwnd = glfwGetWin32Window(w);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    COLORREF cap = RGB(22, 22, 30);
    DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof cap);
#else
    (void)w;
#endif
}

void revealInFileManager(const fs::path& p) {
#ifdef _WIN32
    std::wstring arg = L"/select,\"" + p.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
#else
    fs::path dir = fs::is_directory(p) ? p : p.parent_path();
    std::string cmd = "xdg-open \"" + u8s(dir) + "\" >/dev/null 2>&1 &";
    (void)std::system(cmd.c_str());
#endif
}

#ifdef _WIN32
struct ComScope {
    HRESULT hr;
    ComScope() { hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE); }
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

static DialogResult runDialog(void* native, bool save, bool folder, const fs::path& startDir,
                              const std::string& defaultName) {
    ComScope com;
    DialogResult r;
    IFileDialog* dlg = nullptr;
    HRESULT hr = save ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))
                      : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) return r;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
    if (folder) opts |= FOS_PICKFOLDERS;
    if (save) opts |= FOS_OVERWRITEPROMPT;
    dlg->SetOptions(opts);
    if (!folder) {
        COMDLG_FILTERSPEC types[] = {{L"Playlists (*.m3u8, *.m3u)", L"*.m3u8;*.m3u"}, {L"All files", L"*.*"}};
        dlg->SetFileTypes(2, types);
        dlg->SetDefaultExtension(L"m3u8");
    }
    if (!defaultName.empty()) dlg->SetFileName(u8p(defaultName).wstring().c_str());
    if (!startDir.empty() && fs::exists(startDir)) {
        IShellItem* folderItem = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(startDir.wstring().c_str(), nullptr, IID_PPV_ARGS(&folderItem)))) {
            dlg->SetFolder(folderItem);
            folderItem->Release();
        }
    }
    hr = dlg->Show((HWND)native);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR psz = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                r.path = fs::path(psz);
                r.status = DialogStatus::Picked;
                CoTaskMemFree(psz);
            }
            item->Release();
        }
    } else {
        r.status = DialogStatus::Cancelled;
    }
    dlg->Release();
    return r;
}

DialogResult pickFolder(void* native, const fs::path& start) { return runDialog(native, false, true, start, ""); }
DialogResult saveFile(void* native, const fs::path& startDir, const std::string& defaultName) {
    return runDialog(native, true, false, startDir, defaultName);
}
DialogResult openFile(void* native, const fs::path& startDir) { return runDialog(native, false, false, startDir, ""); }

#else  // ------------------------------------------------------------------------------------------- Linux

static bool haveTool(const char* name) {
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

static std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

static DialogResult runCmd(const std::string& cmd) {
    DialogResult r;
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return r;
    std::string out;
    char buf[4096];
    while (fgets(buf, sizeof buf, f)) out += buf;
    int status = pclose(f);
    out = trim(out);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && !out.empty()) {
        r.status = DialogStatus::Picked;
        r.path = u8p(out);
    } else {
        r.status = DialogStatus::Cancelled;
    }
    return r;
}

DialogResult pickFolder(void*, const fs::path& start) {
    std::string st = u8s(start);
    if (haveTool("zenity"))
        return runCmd("zenity --file-selection --directory --title='Add music folder'" +
                      (st.empty() ? std::string() : " --filename=" + shellQuote(st + "/")) + " 2>/dev/null");
    if (haveTool("kdialog"))
        return runCmd("kdialog --getexistingdirectory " + shellQuote(st.empty() ? "." : st) + " 2>/dev/null");
    return {};
}

DialogResult saveFile(void*, const fs::path& startDir, const std::string& defaultName) {
    std::string full = u8s(startDir.empty() ? fs::current_path() : startDir) + "/" + defaultName;
    if (haveTool("zenity"))
        return runCmd("zenity --file-selection --save --confirm-overwrite --title='Save playlist' --filename=" +
                      shellQuote(full) + " --file-filter='Playlists | *.m3u8 *.m3u' 2>/dev/null");
    if (haveTool("kdialog"))
        return runCmd("kdialog --getsavefilename " + shellQuote(full) + " '*.m3u8 *.m3u|Playlists' 2>/dev/null");
    return {};
}

DialogResult openFile(void*, const fs::path& startDir) {
    std::string st = u8s(startDir);
    if (haveTool("zenity"))
        return runCmd("zenity --file-selection --title='Open playlist'" +
                      (st.empty() ? std::string() : " --filename=" + shellQuote(st + "/")) +
                      " --file-filter='Playlists | *.m3u8 *.m3u' 2>/dev/null");
    if (haveTool("kdialog"))
        return runCmd("kdialog --getopenfilename " + shellQuote(st.empty() ? "." : st) +
                      " '*.m3u8 *.m3u|Playlists' 2>/dev/null");
    return {};
}
#endif

}  // namespace platform
