/**
 * @file ui_stubs.cpp
 * @brief Stub implementations for UI layer functions and platform
 *        functions that were previously provided by MainScene.cpp,
 *        AppDelegate.cpp, and the environ/ui/ directory.
 *
 * With the migration to Flutter-based UI, all of these are replaced by
 * minimal stubs that either log a warning or return a sensible default.
 *
 * Functions stubbed here are called from the engine core and must link,
 * but their functionality will be provided by the Flutter host layer.
 */

#include <spdlog/spdlog.h>
#include <string>
#include <vector>
#include <filesystem>

#include "tjsCommHead.h"
#include "tjsConfig.h"
#include "WindowIntf.h"
#include "MenuItemIntf.h"
#include "Platform.h"
#include "TVPWindow.h"
#include "Application.h"
#include "krkr_egl_context.h"

// ---------------------------------------------------------------------------
// FlutterWindowLayer — concrete iWindowLayer for Flutter host mode.
// Provides a logical window backed by the ANGLE EGL Pbuffer surface.
// Rendering output goes through glReadPixels in the engine_api layer.
// ---------------------------------------------------------------------------
class FlutterWindowLayer : public iWindowLayer {
public:
    explicit FlutterWindowLayer(tTJSNI_Window *owner)
        : owner_(owner), visible_(true), caption_("krkr2"),
          width_(0), height_(0), active_(true), closing_(false) {
        // Get initial size from EGL context
        auto& egl = krkr::GetEngineEGLContext();
        if (egl.IsValid()) {
            width_  = static_cast<tjs_int>(egl.GetWidth());
            height_ = static_cast<tjs_int>(egl.GetHeight());
        } else {
            width_  = 1280;
            height_ = 720;
        }
        spdlog::info("FlutterWindowLayer created: {}x{}", width_, height_);
    }

    ~FlutterWindowLayer() {
        spdlog::debug("FlutterWindowLayer destroyed");
    }

    // -- Pure virtual implementations --

    void SetPaintBoxSize(tjs_int w, tjs_int h) override {
        // Paint box size is managed by the EGL surface; no-op here
    }

    bool GetFormEnabled() override { return !closing_; }

    void SetDefaultMouseCursor() override {}

    void GetCursorPos(tjs_int &x, tjs_int &y) override {
        x = 0; y = 0;
    }

    void SetCursorPos(tjs_int x, tjs_int y) override {}

    void SetHintText(const ttstr &text) override {}

    void SetAttentionPoint(tjs_int left, tjs_int top,
                           const struct tTVPFont *font) override {}

    void ZoomRectangle(tjs_int &left, tjs_int &top, tjs_int &right,
                       tjs_int &bottom) override {
        // No zoom transformation — coordinates pass through 1:1
    }

    void BringToFront() override {}

    void ShowWindowAsModal() override {
        spdlog::warn("FlutterWindowLayer::ShowWindowAsModal: stub");
    }

    bool GetVisible() override { return visible_; }

    void SetVisible(bool bVisible) override { visible_ = bVisible; }

    const char *GetCaption() override { return caption_.c_str(); }

    void SetCaption(const std::string &cap) override { caption_ = cap; }

    void SetWidth(tjs_int w) override { width_ = w; }

    void SetHeight(tjs_int h) override { height_ = h; }

    void SetSize(tjs_int w, tjs_int h) override {
        width_ = w;
        height_ = h;
    }

    void GetSize(tjs_int &w, tjs_int &h) override {
        w = width_;
        h = height_;
    }

    [[nodiscard]] tjs_int GetWidth() const override { return width_; }

    [[nodiscard]] tjs_int GetHeight() const override { return height_; }

    void GetWinSize(tjs_int &w, tjs_int &h) override {
        w = width_;
        h = height_;
    }

    void SetZoom(tjs_int numer, tjs_int denom) override {
        ZoomNumer = numer;
        ZoomDenom = denom;
    }

    void UpdateDrawBuffer(iTVPTexture2D *tex) override {
        // In Flutter mode, frame readback is done via engine_read_frame_rgba.
        // This method is called by BasicDrawDevice after compositing.
        // No additional work needed here — the framebuffer is already
        // rendered in the EGL Pbuffer and will be read by the host.
    }

    void InvalidateClose() override {
        closing_ = true;
    }

    bool GetWindowActive() override { return active_; }

    void Close() override {
        closing_ = true;
        spdlog::debug("FlutterWindowLayer::Close called");
    }

    void OnCloseQueryCalled(bool b) override {}

    void InternalKeyDown(tjs_uint16 key, tjs_uint32 shift) override {}

    void OnKeyUp(tjs_uint16 vk, int shift) override {}

    void OnKeyPress(tjs_uint16 vk, int repeat, bool prevkeystate,
                    bool convertkey) override {}

    [[nodiscard]] tTVPImeMode GetDefaultImeMode() const override {
        return imDisable;
    }

    void SetImeMode(tTVPImeMode mode) override {}

    void ResetImeMode() override {}

    void UpdateWindow(tTVPUpdateType type) override {
        // Rendering is driven by engine_tick / engine_read_frame_rgba
    }

    void SetVisibleFromScript(bool b) override { visible_ = b; }

    void SetUseMouseKey(bool b) override {}

    [[nodiscard]] bool GetUseMouseKey() const override { return false; }

    void ResetMouseVelocity() override {}

    void ResetTouchVelocity(tjs_int id) override {}

    bool GetMouseVelocity(float &x, float &y, float &speed) const override {
        x = y = speed = 0;
        return false;
    }

    void TickBeat() override {
        // Called every ~50ms; nothing to do in Flutter mode
    }

    TVPOverlayNode *GetPrimaryArea() override { return nullptr; }

private:
    tTJSNI_Window *owner_;
    bool visible_;
    std::string caption_;
    tjs_int width_;
    tjs_int height_;
    bool active_;
    bool closing_;
};

// ---------------------------------------------------------------------------
// TVPInitUIExtension — originally in ui/extension/UIExtension.cpp
// Registered custom UI widgets (PageView, etc.)
// ---------------------------------------------------------------------------
void TVPInitUIExtension() {
    spdlog::debug("TVPInitUIExtension: stub (UI handled by Flutter)");
}

// ---------------------------------------------------------------------------
// TVPCreateAndAddWindow — originally in MainScene.cpp
// Creates a FlutterWindowLayer and registers it with the application.
// ---------------------------------------------------------------------------
iWindowLayer *TVPCreateAndAddWindow(tTJSNI_Window *w) {
    auto *layer = new FlutterWindowLayer(w);
    spdlog::info("TVPCreateAndAddWindow: created FlutterWindowLayer ({}x{})",
                 layer->GetWidth(), layer->GetHeight());
    return layer;
}

// ---------------------------------------------------------------------------
// TVPConsoleLog — originally in MainScene.cpp
// Logs engine console output. Redirect to spdlog.
// ---------------------------------------------------------------------------
void TVPConsoleLog(const ttstr &mes, bool important) {
    // Convert TJS string to UTF-8 for spdlog
    tTJSNarrowStringHolder narrow_mes(mes.c_str());
    if (important) {
        spdlog::info("[TVP Console] {}", narrow_mes.operator const char *());
    } else {
        spdlog::debug("[TVP Console] {}", narrow_mes.operator const char *());
    }
}

// ---------------------------------------------------------------------------
// TJS::TVPConsoleLog — originally in MainScene.cpp (TJS2 namespace version)
// ---------------------------------------------------------------------------
namespace TJS {
void TVPConsoleLog(const tTJSString &str) {
    tTJSNarrowStringHolder narrow(str.c_str());
    spdlog::debug("[TJS Console] {}", narrow.operator const char *());
}
} // namespace TJS

// ---------------------------------------------------------------------------
// TVPGetOSName / TVPGetPlatformName — originally in MainScene.cpp
// Returns OS/platform identification strings.
// ---------------------------------------------------------------------------
ttstr TVPGetOSName() {
#if defined(__APPLE__)
    return ttstr(TJS_W("macOS"));
#elif defined(_WIN32)
    return ttstr(TJS_W("Windows"));
#elif defined(__linux__)
    return ttstr(TJS_W("Linux"));
#else
    return ttstr(TJS_W("Unknown"));
#endif
}

ttstr TVPGetPlatformName() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return ttstr(TJS_W("ARM64"));
#elif defined(__x86_64__) || defined(_M_X64)
    return ttstr(TJS_W("x86_64"));
#else
    return ttstr(TJS_W("Unknown"));
#endif
}

// ---------------------------------------------------------------------------
// TVPGetInternalPreferencePath — originally in MainScene.cpp
// Returns the directory path for storing preferences/config files.
// ---------------------------------------------------------------------------
static std::string s_internalPreferencePath;

const std::string &TVPGetInternalPreferencePath() {
    if (s_internalPreferencePath.empty()) {
#if defined(__APPLE__)
        const char *home = getenv("HOME");
        if (home) {
            s_internalPreferencePath = std::string(home) + "/Library/Application Support/krkr2/";
        } else {
            s_internalPreferencePath = "/tmp/krkr2/";
        }
#else
        s_internalPreferencePath = "/tmp/krkr2/";
#endif
        std::filesystem::create_directories(s_internalPreferencePath);
    }
    return s_internalPreferencePath;
}

// ---------------------------------------------------------------------------
// TVPGetApplicationHomeDirectory — originally in MainScene.cpp
// Returns list of directories where the application searches for data files.
// ---------------------------------------------------------------------------
static std::vector<std::string> s_appHomeDirs;

const std::vector<std::string> &TVPGetApplicationHomeDirectory() {
    if (s_appHomeDirs.empty()) {
        // Use current working directory as default
        s_appHomeDirs.push_back(std::filesystem::current_path().string() + "/");
    }
    return s_appHomeDirs;
}

// ---------------------------------------------------------------------------
// TVPCopyFile — originally in CustomFileUtils.cpp
// Copies a file from source to destination.
// ---------------------------------------------------------------------------
bool TVPCopyFile(const std::string &from, const std::string &to) {
    std::error_code ec;
    std::filesystem::copy_file(from, to,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        spdlog::error("TVPCopyFile failed: {} -> {} ({})", from, to, ec.message());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// TVPShowFileSelector — originally in ui/FileSelectorForm.cpp
// Shows a file selection dialog. In Flutter mode, this is handled by the
// Flutter host layer. Returns empty string (no selection).
// ---------------------------------------------------------------------------
std::string TVPShowFileSelector(const std::string &title,
                                const std::string &init_dir,
                                std::string default_ext,
                                bool is_save) {
    spdlog::warn("TVPShowFileSelector: stub — file selection handled by Flutter");
    return "";
}

// ---------------------------------------------------------------------------
// TVPShowPopMenu — originally in ui/InGameMenuForm.cpp
// Shows a popup context menu. In Flutter mode, handled by Flutter host.
// ---------------------------------------------------------------------------
void TVPShowPopMenu(tTJSNI_MenuItem *menu) {
    spdlog::warn("TVPShowPopMenu: stub — popup menus handled by Flutter");
}

// ---------------------------------------------------------------------------
// TVPOpenPatchLibUrl — originally in AppDelegate.cpp
// Opens the URL for the patch library website.
// ---------------------------------------------------------------------------
void TVPOpenPatchLibUrl() {
    spdlog::warn("TVPOpenPatchLibUrl: stub — URL opening handled by Flutter");
}
