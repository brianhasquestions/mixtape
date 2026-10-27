#include "filebrowser.h"
#include "platform.h"
#include "theme.h"
#include <algorithm>
#include <imgui.h>
#include <imgui_stdlib.h>

void FileBrowser::open(Mode mode, const fs::path& start, const std::string& defaultName) {
    m_mode = mode;
    m_nameBuf = defaultName;
    m_error.clear();
    m_selected = -1;
    std::error_code ec;
    fs::path s = start;
    if (s.empty() || !fs::is_directory(s, ec)) {
        auto r = platform::roots();
        s = r.empty() ? fs::current_path(ec) : r.back();
    }
    navigate(s);
    m_open = true;
    m_pendingOpen = true;
}

void FileBrowser::navigate(const fs::path& p) {
    std::error_code ec;
    fs::path np = fs::absolute(p, ec).lexically_normal();
    if (!fs::is_directory(np, ec)) {
        m_error = "Not a folder: " + u8s(np);
        return;
    }
    m_cwd = np;
    m_pathBuf = u8s(m_cwd);
    m_selected = -1;
    m_error.clear();
    refresh();
}

void FileBrowser::refresh() {
    m_entries.clear();
    std::error_code ec;
    for (fs::directory_iterator it(m_cwd, fs::directory_options::skip_permission_denied, ec), end; it != end;
         it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const auto& e = *it;
        bool dir = e.is_directory(ec);
        std::string name = u8s(e.path().filename());
        if (name.empty() || name[0] == '.') continue;
        if (!dir) {
            if (m_mode == Mode::Folder) continue;
            std::string ext = toLower(u8s(e.path().extension()));
            if (ext != ".m3u8" && ext != ".m3u") continue;
        }
        m_entries.push_back({name, e.path(), dir});
    }
    std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        if (a.dir != b.dir) return a.dir;
        return toLower(a.name) < toLower(b.name);
    });
}

bool FileBrowser::draw() {
    if (!m_open) return false;
    const char* title = m_mode == Mode::Folder ? "Choose a folder##fb" : m_mode == Mode::SaveFile ? "Save playlist##fb" : "Open playlist##fb";
    if (m_pendingOpen) {
        ImGui::OpenPopup(title);
        m_pendingOpen = false;
    }
    bool confirmed = false;
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720, 540), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(title, &m_open, ImGuiWindowFlags_NoCollapse)) {
        // Quick roots
        for (auto& r : platform::roots()) {
            std::string label = u8s(r);
            if (label.size() > 18) label = u8s(r.filename()).empty() ? label : u8s(r.filename());
            if (ImGui::SmallButton(label.c_str())) navigate(r);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("Up")) {
            if (m_cwd.has_parent_path() && m_cwd.parent_path() != m_cwd) navigate(m_cwd.parent_path());
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##path", &m_pathBuf, ImGuiInputTextFlags_EnterReturnsTrue)) navigate(u8p(m_pathBuf));
        if (!m_error.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_error.c_str());

        float footer = ImGui::GetFrameHeightWithSpacing() * (m_mode == Mode::SaveFile ? 2.2f : 1.2f);
        if (ImGui::BeginChild("##list", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
            for (int i = 0; i < (int)m_entries.size(); i++) {
                auto& e = m_entries[i];
                ImGui::PushID(i);
                std::string label = (e.dir ? "[ ] " : "    ") + e.name;
                if (ImGui::Selectable(label.c_str(), m_selected == i, ImGuiSelectableFlags_AllowDoubleClick)) {
                    m_selected = i;
                    if (!e.dir) m_nameBuf = e.name;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (e.dir) {
                            navigate(e.path);
                            ImGui::PopID();
                            break;
                        } else if (m_mode == Mode::OpenFile) {
                            m_result = e.path;
                            confirmed = true;
                        }
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        if (m_mode == Mode::SaveFile) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##name", "playlist name", &m_nameBuf);
        }
        bool canOk = true;
        if (m_mode == Mode::SaveFile) canOk = !trim(m_nameBuf).empty();
        if (m_mode == Mode::OpenFile) canOk = m_selected >= 0 && !m_entries[m_selected].dir;
        ImGui::BeginDisabled(!canOk);
        const char* okLabel = m_mode == Mode::Folder ? "Use this folder" : m_mode == Mode::SaveFile ? "Save" : "Open";
        if (ImGui::Button(okLabel, ImVec2(160, 0))) {
            if (m_mode == Mode::Folder) {
                m_result = (m_selected >= 0 && m_entries[m_selected].dir) ? m_entries[m_selected].path : m_cwd;
            } else if (m_mode == Mode::SaveFile) {
                std::string n = trim(m_nameBuf);
                std::string ext = toLower(u8s(u8p(n).extension()));
                if (ext != ".m3u8" && ext != ".m3u") n += ".m3u8";
                m_result = m_cwd / u8p(n);
            } else {
                m_result = m_entries[m_selected].path;
            }
            confirmed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) m_open = false;
        if (confirmed) m_open = false;
        if (!m_open) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return confirmed;
}
