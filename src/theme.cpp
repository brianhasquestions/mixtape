#include "theme.h"

static ImVec4 hex(unsigned rgb, float a = 1.0f) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

const std::vector<Theme>& themes() {
    static const std::vector<Theme> t = {
        {"Tokyo Night", hex(0x1a1b26), hex(0x16161e), hex(0x24283b), hex(0x2f3549), hex(0x3b4261), hex(0xc0caf5),
         hex(0x565f89), hex(0x7aa2f7), hex(0xbb9af7), hex(0x9ece6a), hex(0xf7768e), hex(0xe0af68), hex(0x7dcfff)},
        {"Catppuccin Mocha", hex(0x1e1e2e), hex(0x181825), hex(0x313244), hex(0x45475a), hex(0x585b70), hex(0xcdd6f4),
         hex(0x6c7086), hex(0xcba6f7), hex(0xf5c2e7), hex(0xa6e3a1), hex(0xf38ba8), hex(0xf9e2af), hex(0x89dceb)},
        {"Gruvbox Dark", hex(0x282828), hex(0x1d2021), hex(0x3c3836), hex(0x504945), hex(0x665c54), hex(0xebdbb2),
         hex(0x928374), hex(0xfe8019), hex(0xfabd2f), hex(0xb8bb26), hex(0xfb4934), hex(0xfabd2f), hex(0x8ec07c)},
        {"Nord", hex(0x2e3440), hex(0x272c36), hex(0x3b4252), hex(0x434c5e), hex(0x4c566a), hex(0xeceff4),
         hex(0x7b88a1), hex(0x88c0d0), hex(0x81a1c1), hex(0xa3be8c), hex(0xbf616a), hex(0xebcb8b), hex(0x8fbcbb)},
        {"Rose Pine", hex(0x191724), hex(0x1f1d2e), hex(0x26233a), hex(0x2a283e), hex(0x403d52), hex(0xe0def4),
         hex(0x6e6a86), hex(0xc4a7e7), hex(0xebbcba), hex(0x9ccfd8), hex(0xeb6f92), hex(0xf6c177), hex(0x31748f)},
        {"Dracula", hex(0x282a36), hex(0x21222c), hex(0x44475a), hex(0x50536b), hex(0x6272a4), hex(0xf8f8f2),
         hex(0x6272a4), hex(0xbd93f9), hex(0xff79c6), hex(0x50fa7b), hex(0xff5555), hex(0xf1fa8c), hex(0x8be9fd)},
        {"Everforest", hex(0x2d353b), hex(0x272e33), hex(0x3d484d), hex(0x475258), hex(0x56635f), hex(0xd3c6aa),
         hex(0x859289), hex(0xa7c080), hex(0x83c092), hex(0xa7c080), hex(0xe67e80), hex(0xdbbc7f), hex(0x7fbbb3)},
        {"Kanagawa", hex(0x1f1f28), hex(0x16161d), hex(0x2a2a37), hex(0x363646), hex(0x54546d), hex(0xdcd7ba),
         hex(0x727169), hex(0x7e9cd8), hex(0x957fb8), hex(0x98bb6c), hex(0xe46876), hex(0xe6c384), hex(0x7fb4ca)},
    };
    return t;
}

void applyTheme(const Theme& t, float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();

    s.WindowRounding = 10.0f;
    s.ChildRounding = 8.0f;
    s.FrameRounding = 6.0f;
    s.PopupRounding = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.GrabRounding = 6.0f;
    s.TabRounding = 6.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.TabBorderSize = 0.0f;
    s.WindowPadding = ImVec2(12, 12);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.CellPadding = ImVec2(8, 4);
    s.ScrollbarSize = 10.0f;
    s.GrabMinSize = 12.0f;
    s.IndentSpacing = 18.0f;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    s.SeparatorTextPadding = ImVec2(12, 4);
    s.ScaleAllSizes(scale);

    ImVec4* c = s.Colors;
    const ImVec4 none(0, 0, 0, 0);
    c[ImGuiCol_Text] = t.text;
    c[ImGuiCol_TextDisabled] = t.textDim;
    c[ImGuiCol_WindowBg] = t.bg;
    c[ImGuiCol_ChildBg] = none;
    c[ImGuiCol_PopupBg] = t.bgAlt;
    c[ImGuiCol_Border] = t.border;
    c[ImGuiCol_BorderShadow] = none;
    c[ImGuiCol_FrameBg] = t.surface;
    c[ImGuiCol_FrameBgHovered] = t.surfaceHover;
    c[ImGuiCol_FrameBgActive] = t.surfaceHover;
    c[ImGuiCol_TitleBg] = t.bgAlt;
    c[ImGuiCol_TitleBgActive] = t.bgAlt;
    c[ImGuiCol_TitleBgCollapsed] = t.bgAlt;
    c[ImGuiCol_MenuBarBg] = t.bgAlt;
    c[ImGuiCol_ScrollbarBg] = none;
    c[ImGuiCol_ScrollbarGrab] = t.border;
    c[ImGuiCol_ScrollbarGrabHovered] = t.textDim;
    c[ImGuiCol_ScrollbarGrabActive] = t.accent;
    c[ImGuiCol_CheckMark] = t.accent;
    c[ImGuiCol_SliderGrab] = t.accent;
    c[ImGuiCol_SliderGrabActive] = t.accent2;
    c[ImGuiCol_Button] = t.surface;
    c[ImGuiCol_ButtonHovered] = t.surfaceHover;
    c[ImGuiCol_ButtonActive] = withAlpha(t.accent, 0.55f);
    c[ImGuiCol_Header] = withAlpha(t.accent, 0.22f);
    c[ImGuiCol_HeaderHovered] = withAlpha(t.accent, 0.32f);
    c[ImGuiCol_HeaderActive] = withAlpha(t.accent, 0.45f);
    c[ImGuiCol_Separator] = t.border;
    c[ImGuiCol_SeparatorHovered] = t.accent;
    c[ImGuiCol_SeparatorActive] = t.accent2;
    c[ImGuiCol_ResizeGrip] = none;
    c[ImGuiCol_ResizeGripHovered] = withAlpha(t.accent, 0.5f);
    c[ImGuiCol_ResizeGripActive] = t.accent;
    c[ImGuiCol_Tab] = t.surface;
    c[ImGuiCol_TabHovered] = withAlpha(t.accent, 0.4f);
    c[ImGuiCol_TabSelected] = withAlpha(t.accent, 0.3f);
    c[ImGuiCol_TabSelectedOverline] = t.accent;
    c[ImGuiCol_TabDimmed] = t.bgAlt;
    c[ImGuiCol_TabDimmedSelected] = t.surface;
    c[ImGuiCol_TabDimmedSelectedOverline] = t.textDim;
    c[ImGuiCol_DockingPreview] = withAlpha(t.accent, 0.5f);
    c[ImGuiCol_DockingEmptyBg] = t.bgAlt;
    c[ImGuiCol_PlotLines] = t.accent;
    c[ImGuiCol_PlotLinesHovered] = t.accent2;
    c[ImGuiCol_PlotHistogram] = t.accent;
    c[ImGuiCol_PlotHistogramHovered] = t.accent2;
    c[ImGuiCol_TableHeaderBg] = t.bgAlt;
    c[ImGuiCol_TableBorderStrong] = t.border;
    c[ImGuiCol_TableBorderLight] = withAlpha(t.border, 0.5f);
    c[ImGuiCol_TableRowBg] = none;
    c[ImGuiCol_TableRowBgAlt] = withAlpha(t.text, 0.025f);
    c[ImGuiCol_TextLink] = t.cyan;
    c[ImGuiCol_TextSelectedBg] = withAlpha(t.accent, 0.35f);
    c[ImGuiCol_DragDropTarget] = t.accent2;
    c[ImGuiCol_NavCursor] = withAlpha(t.accent, 0.8f);
    c[ImGuiCol_NavWindowingHighlight] = t.accent;
    c[ImGuiCol_NavWindowingDimBg] = withAlpha(t.bgAlt, 0.6f);
    c[ImGuiCol_ModalWindowDimBg] = withAlpha(t.bgAlt, 0.7f);
}
