#include "app.h"
#include "platform.h"
#include "theme.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "font_bold.h"
#include "font_regular.h"
#include "icon32.h"
#include "icon64.h"

#include <chrono>
#include <cstdio>
#include <thread>

static App* g_app = nullptr;

static void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

static void dropCallback(GLFWwindow*, int count, const char** paths) {
    if (!g_app) return;
    std::vector<fs::path> v;
    for (int i = 0; i < count; i++) v.push_back(u8p(paths[i]));
    g_app->onDrop(v);
}

static void buildFonts(App& app, float px) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = {
        0x0020, 0x00FF,  // Basic Latin + Latin-1
        0x0100, 0x024F,  // Latin Extended A/B
        0x0370, 0x03FF,  // Greek
        0x0400, 0x052F,  // Cyrillic
        0x2000, 0x206F,  // punctuation
        0x2190, 0x21FF,  // arrows
        0x2500, 0x25FF,  // box drawing / shapes
        0,
    };
    // Base ranges + the common Japanese set + every character actually present in the library,
    // so rare kanji, emoji-adjacent symbols etc. are picked up from whichever font has them.
    static ImVector<ImWchar> builtRanges;  // must outlive Build()
    {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(ranges);
        b.AddRanges(io.Fonts->GetGlyphRangesJapanese());
        std::string extra = app.glyphText();
        if (!extra.empty()) b.AddText(extra.c_str());
        builtRanges.clear();
        b.BuildRanges(&builtRanges);
    }
    const ImWchar* allRanges = builtRanges.Data;

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    ImFont* reg = io.Fonts->AddFontFromMemoryTTF((void*)g_font_regular, (int)g_font_regular_len, px, &cfg, allRanges);

    // Merge system fonts for scripts JetBrains Mono does not cover (Japanese, Chinese, Korean,
    // plus symbols). Each script takes the first font that exists on this machine.
    // Each list is "first one that exists wins"; a merged font only contributes glyphs that are
    // still missing, so the order is: Japanese, Chinese, Korean, then a symbol font.
    static const std::vector<std::vector<const char*>> mergeLists = {
#ifdef _WIN32
        {"C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/msgothic.ttc"},
        {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simsun.ttc"},
        {"C:/Windows/Fonts/malgun.ttf", "C:/Windows/Fonts/gulim.ttc"},
        {"C:/Windows/Fonts/seguisym.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"},
#else
        {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
         "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
         "/usr/share/fonts/TTF/DroidSansFallback.ttf", "/usr/share/fonts/droid/DroidSansFallback.ttf",
         "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", "/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc"},
        {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
         "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
         "/usr/share/fonts/noto/NotoSans-Regular.ttf"},
#endif
    };
    for (const auto& list : mergeLists) {
        for (const char* path : list) {
            std::error_code ec;
            if (!fs::exists(path, ec)) continue;
            ImFontConfig m;
            m.MergeMode = true;
            m.PixelSnapH = true;
            if (io.Fonts->AddFontFromFileTTF(path, px, &m, allRanges)) break;
        }
    }

    ImFont* bold = io.Fonts->AddFontFromMemoryTTF((void*)g_font_bold, (int)g_font_bold_len, px * 1.3f, &cfg, ranges);
    io.Fonts->Build();
    app.setFonts(reg, bold);
}

int main(int, char**) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return 1;

    Config boot;
    boot.load(platform::configDir() / "config.json");
    float ms = 1.0f, msy = 1.0f;
    if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) glfwGetMonitorContentScale(mon, &ms, &msy);
    if (ms <= 0.0f) ms = 1.0f;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow((int)(boot.winW * ms), (int)(boot.winH * ms), "mixtape", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    platform::applyDarkTitleBar(window);
    {
        GLFWimage icons[2] = {{64, 64, (unsigned char*)g_icon64}, {32, 32, (unsigned char*)g_icon32}};
        glfwSetWindowIcon(window, 2, icons);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigDragClickToInputText = true;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    float xs = ms, ys = ms;
    glfwGetWindowContentScale(window, &xs, &ys);
    if (xs <= 0.0f) xs = 1.0f;

    App app(window);
    g_app = &app;
    app.setDpi(xs);
    buildFonts(app, 15.0f * xs * app.config().uiScale);
    app.fontsBuilt();
    app.applyThemeNow();
    glfwSetDropCallback(window, dropCallback);

    while (!app.shouldQuit()) {
        if (glfwWindowShouldClose(window)) {
            if (app.requestQuit()) break;
            glfwSetWindowShouldClose(window, GLFW_FALSE);
        }
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        float newScale = 0.0f;
        bool scaleChanged = app.takeFontRebuild(newScale);
        if (scaleChanged || app.fontsDirty()) {
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            buildFonts(app, 15.0f * xs * app.config().uiScale);
            ImGui_ImplOpenGL3_CreateFontsTexture();
            app.fontsBuilt();
            if (scaleChanged) app.applyThemeNow();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app.frame();
        ImGui::Render();

        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        ImVec4 bg = app.clearColor();
        glClearColor(bg.x, bg.y, bg.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        // Idle throttle: when the window is in the background there is nothing to animate.
        if (!glfwGetWindowAttrib(window, GLFW_FOCUSED)) std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    app.saveState();
    g_app = nullptr;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
