/**
 * @file EngineLoop.h
 * @brief Core engine loop driver.
 * @brief 核心引擎循环驱动。
 *
 * Replaces the old Scene-based MainScene with a cleaner,
 * platform-agnostic loop and input dispatcher.
 * 用更清晰、与平台解耦的循环与输入分发器，
 * 取代旧的基于 Scene 的 MainScene。
 *
 * Responsibilities / 职责：
 *   - Drive Application::Run() on each frame.
 *   - 每帧驱动 Application::Run()。
 *   - Forward input events from engine_api to the engine core.
 *   - 将输入事件从 engine_api 转发到引擎核心。
 *   - Maintain async key state for TJS2 System.getKeyState.
 *   - 维护异步按键状态，供 TJS2 System.getKeyState 查询。
 */
#pragma once

#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Input event types / 输入事件类型
// Mirrors engine_api.h while staying local, so core does not depend on bridge.
// 对齐 engine_api.h 的取值，但保持本地声明，避免 core 反向依赖 bridge。
// ---------------------------------------------------------------------------
enum EngineInputEventType : uint32_t {
    kEngineInputPointerDown = 1,
    kEngineInputPointerMove = 2,
    kEngineInputPointerUp = 3,
    kEngineInputPointerScroll = 4,
    kEngineInputKeyDown = 5,
    kEngineInputKeyUp = 6,
    kEngineInputTextInput = 7,
    kEngineInputBack = 8,
};

/**
 * @brief Lightweight input event payload passed from engine_api into core.
 * @brief 从 engine_api 传入 core 的轻量输入事件结构。
 */
struct EngineInputEvent {
    // Event type / 事件类型
    uint32_t type = 0;
    // Pointer X in logical pixels / 逻辑像素 X
    double x = 0;
    // Pointer Y in logical pixels / 逻辑像素 Y
    double y = 0;
    // Scroll delta X / 滚动增量 X
    double delta_x = 0;
    // Scroll delta Y / 滚动增量 Y
    double delta_y = 0;
    // Pointer or touch ID / 指针或触点 ID
    int32_t pointer_id = 0;
    // Button index / 按钮编号：0=left, 1=right, 2=middle
    int32_t button = 0;
    // Virtual key code / 虚拟键码
    int32_t key_code = 0;
    // TVP_SS_* compatible flags
    int32_t modifiers = 0;
    // Text code point / 文本 Unicode 码点
    uint32_t unicode_codepoint = 0;
};

/**
 * @class EngineLoop
 * @brief Singleton engine loop driver with input forwarding.
 * @brief 带输入转发能力的单例引擎循环驱动。
 *
 * Lifecycle / 生命周期：
 *   1. CreateInstance() from engine_open_game.
 *   1. 由 engine_open_game 调用 CreateInstance() 创建实例。
 *   2. Optional StartupFrom(path) for standalone mode.
 *   2. 独立模式下可选调用 StartupFrom(path)。
 *   3. Start() to enable frame updates.
 *   3. 调用 Start() 开启逐帧更新。
 *   4. Tick(delta) each frame.
 *   4. 每帧调用 Tick(delta)。
 *   5. HandleInputEvent(event) before the tick as needed.
 *   5. 在需要时于 Tick 前调用 HandleInputEvent(event)。
 */
class EngineLoop {
    EngineLoop();

public:
    ~EngineLoop();

    // Non-copyable / 禁止拷贝
    EngineLoop(const EngineLoop&) = delete;
    EngineLoop& operator=(const EngineLoop&) = delete;

    /** Get the singleton instance / 获取单例实例（未创建时返回 nullptr）。 */
    static EngineLoop* GetInstance();

    /** Create the singleton instance / 创建单例实例（幂等）。 */
    static EngineLoop* CreateInstance();

    /**
     * @brief Start the engine from the given path in standalone mode.
     * @brief 在独立模式下通过给定路径启动引擎。
     *
     * In Flutter host mode, Application::StartApplication is called directly
     * by engine_open_game, so this path is usually unused.
     * 在 Flutter 宿主模式下，engine_open_game 会直接调用
     * Application::StartApplication，因此这里通常不会走到。
     */
    bool StartupFrom(const std::string& path);

    /**
     * @brief Enable per-frame updates.
     * @brief 启用逐帧更新。
     */
    void Start();

    /**
     * @brief Drive one frame of the main loop.
     * @brief 驱动主循环执行一帧。
     *
     * Called by engine_tick() or by the host frame callback.
     * 由 engine_tick() 或宿主帧回调触发。
     *
     * @param delta Seconds elapsed since the previous tick.
     * @param delta 距离上一次 tick 经过的秒数。
     */
    void Tick(float delta);

    /** Whether the loop has been started / 当前循环是否已经启动。 */
    bool IsStarted() const { return started_; }

    // -----------------------------------------------------------------------
    // Input event handling / 输入事件处理
    // -----------------------------------------------------------------------

    /**
     * @brief Dispatch one input event into the engine core.
     * @brief 向引擎核心分发一条输入事件。
     *
     * Converts EngineInputEvent into TVP input events and posts them through
     * TVPPostInputEvent. Also keeps async key state in sync.
     * 该函数会把 EngineInputEvent 转换成 TVP 输入事件并通过
     * TVPPostInputEvent 投递，同时维护异步按键状态。
     *
     * @param event Input event from engine_api.
     * @param event 来自 engine_api 的输入事件。
     * @return true if dispatch succeeded; otherwise false.
     * @return 分发成功返回 true，否则返回 false。
     */
    bool HandleInputEvent(const EngineInputEvent& event);

private:
    void DoStartup(const std::string& path);

    // Input helpers / 输入辅助函数
    void HandlePointerDown(const EngineInputEvent& event);
    void HandlePointerMove(const EngineInputEvent& event);
    void HandlePointerUp(const EngineInputEvent& event);
    void HandlePointerScroll(const EngineInputEvent& event);
    void HandleKeyDown(const EngineInputEvent& event);
    void HandleKeyUp(const EngineInputEvent& event);
    void HandleTextInput(const EngineInputEvent& event);

    /**
     * @brief Convert modifier flags to TVP shift-state flags.
     * @brief 将修饰键位图转换成 TVP shift-state 标志。
     */
    static uint32_t ConvertModifiers(int32_t modifiers);

    bool started_ = false;
    bool update_enabled_ = false;

    // Stored mouse-down position used when posting click events.
    // Mirrors the original engine's LastMouseDownX/Y behavior.
    // 缓存 mouse-down 坐标，供 click 事件使用。
    // 这里保持与原引擎 LastMouseDownX/Y 相同的行为。
    int32_t last_mouse_down_x_ = 0;
    int32_t last_mouse_down_y_ = 0;
};
