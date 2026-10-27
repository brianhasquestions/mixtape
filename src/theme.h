#pragma once
#include <imgui.h>
#include <string>
#include <vector>

struct Theme {
    const char* name;
    ImVec4 bg, bgAlt, surface, surfaceHover, border;
    ImVec4 text, textDim;
    ImVec4 accent, accent2, green, red, yellow, cyan;
};

const std::vector<Theme>& themes();
void applyTheme(const Theme& t, float scale);

inline ImVec4 withAlpha(ImVec4 c, float a) { return ImVec4(c.x, c.y, c.z, a); }
inline ImU32 col32(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }
inline ImVec4 mix(ImVec4 a, ImVec4 b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
