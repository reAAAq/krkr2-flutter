#include "engine_api.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <new>
#include <string>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <memory>
#include <vector>

#include <csignal>
#include <cstdlib>
#include <execinfo.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "environ/Application.h"
#include "environ/EngineBootstrap.h"
#include "environ/EngineLoop.h"
#include "environ/MainScene.h"
#include "base/SysInitIntf.h"
#include "base/impl/SysInitImpl.h"
#include "visual/ogl/ogl_common.h"
#include "visual/ogl/krkr_egl_context.h"
#include "visual/impl/WindowImpl.h"
#include "visual/RenderManager.h"

int TVPDrawSceneOnce(int interval);

struct engine_handle_s {
  std::recursive_mutex mutex;
  std::string last_error;
  int state = 0;
  std::thread::id owner_thread;
  bool runtime_owner = false;
  uint32_t surface_width = 1280;
  uint32_t surface_height = 720;
  uint64_t frame_serial = 0;
  uint32_t frame_width = 0;
  uint32_t frame_height = 0;
  uint32_t frame_stride_bytes = 0;
  std::vector<uint8_t> frame_rgba;
  bool frame_ready = false;
  std::unordered_set<intptr_t> active_pointer_ids;
  std::deque<engine_input_event_t> pending_input_events;
  bool native_mouse_callbacks_disabled = false;
  bool iosurface_attached = false;
  bool frame_rendered_this_tick = false;
};

namespace {

enum class EngineState {
  kCreated = 0,
  kOpened,
  kPaused,
  kDestroyed,
};

inline int ToStateValue(EngineState state) {
  return static_cast<int>(state);
}

std::recursive_mutex g_registry_mutex;
std::unordered_set<engine_handle_t> g_live_handles;
thread_local std::string g_thread_error;
engine_handle_t g_runtime_owner = nullptr;
bool g_runtime_active = false;
bool g_runtime_started_once = false;
bool g_engine_bootstrapped = false;
std::once_flag g_loggers_init_once;

std::shared_ptr<spdlog::logger> EnsureNamedLogger(const char* name) {
  if (auto logger = spdlog::get(name); logger != nullptr) {
    return logger;
  }
  return spdlog::stdout_color_mt(name);
}

void CrashSignalHandler(int sig) {
  spdlog::critical("FATAL SIGNAL {} received!", sig);

  // Print a mini backtrace
  void* frames[32];
  int count = backtrace(frames, 32);
  char** symbols = backtrace_symbols(frames, count);
  if (symbols) {
    for (int i = 0; i < count; ++i) {
      spdlog::critical("  [{}] {}", i, symbols[i]);
    }
    free(symbols);
  }

  spdlog::default_logger()->flush();
  // Re-raise so the OS generates a proper crash report
  signal(sig, SIG_DFL);
  raise(sig);
}

void InstallCrashSignalHandlers() {
  signal(SIGSEGV, CrashSignalHandler);
  signal(SIGABRT, CrashSignalHandler);
  signal(SIGBUS,  CrashSignalHandler);
  signal(SIGFPE,  CrashSignalHandler);
}

void EnsureRuntimeLoggersInitialized() {
  std::call_once(g_loggers_init_once, []() {
    spdlog::set_level(spdlog::level::debug);
    // Flush every log message so crash logs are never lost
    spdlog::flush_on(spdlog::level::debug);
    auto core_logger = EnsureNamedLogger("core");
    EnsureNamedLogger("tjs2");
    EnsureNamedLogger("plugin");
    if (core_logger != nullptr) {
      spdlog::set_default_logger(core_logger);
    }
    InstallCrashSignalHandlers();
  });
}

void SetThreadError(const char* message) {
  g_thread_error = (message != nullptr) ? message : "";
}

engine_result_t SetThreadErrorAndReturn(engine_result_t result,
                                        const char* message) {
  SetThreadError(message);
  return result;
}

bool IsHandleLiveLocked(engine_handle_t handle) {
  return g_live_handles.find(handle) != g_live_handles.end();
}

engine_result_t ValidateHandleLocked(engine_handle_t handle,
                                     engine_handle_s** out_impl) {
  if (handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine handle is null");
  }
  if (!IsHandleLiveLocked(handle)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine handle is invalid or already destroyed");
  }
  *out_impl = reinterpret_cast<engine_handle_s*>(handle);
  return ENGINE_RESULT_OK;
}

void SetHandleErrorLocked(engine_handle_s* impl, const char* message) {
  impl->last_error = (message != nullptr) ? message : "";
}

engine_result_t SetHandleErrorAndReturnLocked(engine_handle_s* impl,
                                              engine_result_t result,
                                              const char* message) {
  SetHandleErrorLocked(impl, message);
  return result;
}

engine_result_t ValidateHandleThreadLocked(engine_handle_s* impl) {
  if (impl->owner_thread != std::this_thread::get_id()) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine handle must be used on the thread where engine_create was called");
  }
  return ENGINE_RESULT_OK;
}

void ClearHandleErrorLocked(engine_handle_s* impl) {
  impl->last_error.clear();
}

bool EnsureEngineRuntimeInitialized(uint32_t width, uint32_t height) {
  if (g_engine_bootstrapped) {
    return true;
  }
  if (!TVPEngineBootstrap::Initialize(width, height)) {
    return false;
  }
  g_engine_bootstrapped = true;
  return true;
}

struct FrameReadbackLayout {
  int32_t read_x = 0;
  int32_t read_y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride_bytes = 0;
};

FrameReadbackLayout GetFrameReadbackLayoutLocked(engine_handle_s* impl) {
  FrameReadbackLayout layout;
  layout.width = impl->surface_width;
  layout.height = impl->surface_height;

  GLint viewport[4] = {0, 0, 0, 0};
  glGetIntegerv(GL_VIEWPORT, viewport);
  if (glGetError() == GL_NO_ERROR && viewport[2] > 0 && viewport[3] > 0) {
    layout.read_x = viewport[0];
    layout.read_y = viewport[1];
    layout.width = static_cast<uint32_t>(viewport[2]);
    layout.height = static_cast<uint32_t>(viewport[3]);
  } else {
    // Fallback: use the EGL surface dimensions
    auto& egl = krkr::GetEngineEGLContext();
    if (egl.IsValid()) {
      const uint32_t egl_w = egl.GetWidth();
      const uint32_t egl_h = egl.GetHeight();
      if (egl_w > 0 && egl_h > 0) {
        layout.width = egl_w;
        layout.height = egl_h;
      }
    }
  }

  if (layout.width == 0) {
    layout.width = 1;
  }
  if (layout.height == 0) {
    layout.height = 1;
  }
  layout.stride_bytes = layout.width * 4u;
  return layout;
}

bool ReadCurrentFrameRgba(const FrameReadbackLayout& layout, void* out_pixels) {
  if (layout.width == 0 || layout.height == 0 || out_pixels == nullptr) {
    return false;
  }

  glFinish();
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(static_cast<GLint>(layout.read_x),
               static_cast<GLint>(layout.read_y),
               static_cast<GLsizei>(layout.width),
               static_cast<GLsizei>(layout.height), GL_RGBA,
               GL_UNSIGNED_BYTE, out_pixels);
  const GLenum read_pixels_error = glGetError();

  if (read_pixels_error != GL_NO_ERROR) {
    return false;
  }

  auto* bytes = static_cast<uint8_t*>(out_pixels);
  const size_t row_bytes = static_cast<size_t>(layout.stride_bytes);
  std::vector<uint8_t> row_buffer(row_bytes);
  const uint32_t half_rows = layout.height / 2u;
  for (uint32_t y = 0; y < half_rows; ++y) {
    uint8_t* row_top = bytes + static_cast<size_t>(y) * row_bytes;
    uint8_t* row_bottom =
        bytes + static_cast<size_t>(layout.height - 1u - y) * row_bytes;
    std::memcpy(row_buffer.data(), row_top, row_bytes);
    std::memcpy(row_top, row_bottom, row_bytes);
    std::memcpy(row_bottom, row_buffer.data(), row_bytes);
  }

  return true;
}

bool IsFinitePointerValue(double value) {
  return std::isfinite(value);
}

engine_result_t DispatchInputEventNow(engine_handle_s* impl,
                                      const engine_input_event_t& event,
                                      const char** out_error_message) {
  auto* loop = EngineLoop::GetInstance();
  if (loop == nullptr) {
    if (out_error_message != nullptr) {
      *out_error_message = "engine loop is unavailable";
    }
    return ENGINE_RESULT_INVALID_STATE;
  }

  // Convert engine_input_event_t → EngineInputEvent (bridge → core)
  EngineInputEvent core_event;
  core_event.type = event.type;
  core_event.x = event.x;
  core_event.y = event.y;
  core_event.delta_x = event.delta_x;
  core_event.delta_y = event.delta_y;
  core_event.pointer_id = event.pointer_id;
  core_event.button = event.button;
  core_event.key_code = event.key_code;
  core_event.modifiers = event.modifiers;
  core_event.unicode_codepoint = event.unicode_codepoint;

  if (!loop->HandleInputEvent(core_event)) {
    if (out_error_message != nullptr) {
      *out_error_message = "input event dispatch failed (no active window?)";
    }
    return ENGINE_RESULT_INVALID_STATE;
  }

  if (out_error_message != nullptr) {
    *out_error_message = nullptr;
  }
  return ENGINE_RESULT_OK;
}

}  // namespace

extern "C" {

engine_result_t engine_get_runtime_api_version(uint32_t* out_api_version) {
  if (out_api_version == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_api_version is null");
  }
  *out_api_version = ENGINE_API_VERSION;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_create(const engine_create_desc_t* desc,
                              engine_handle_t* out_handle) {
  if (desc == nullptr || out_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine_create requires non-null desc and out_handle");
  }

  if (desc->struct_size < sizeof(engine_create_desc_t)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine_create_desc_t.struct_size is too small");
  }

  const uint32_t expected_major = (ENGINE_API_VERSION >> 24u) & 0xFFu;
  const uint32_t caller_major = (desc->api_version >> 24u) & 0xFFu;
  if (caller_major != expected_major) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                   "unsupported engine API major version");
  }

  EnsureRuntimeLoggersInitialized();
  TVPHostSuppressProcessExit = true;

  auto* impl = new (std::nothrow) engine_handle_s();
  if (impl == nullptr) {
    *out_handle = nullptr;
    return SetThreadErrorAndReturn(ENGINE_RESULT_INTERNAL_ERROR,
                                   "failed to allocate engine handle");
  }

  impl->state = ToStateValue(EngineState::kCreated);
  impl->owner_thread = std::this_thread::get_id();
  impl->runtime_owner = false;

  auto handle = reinterpret_cast<engine_handle_t>(impl);
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    g_live_handles.insert(handle);
  }

  *out_handle = handle;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_destroy(engine_handle_t handle) {
  if (handle == nullptr) {
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  engine_handle_s* impl = nullptr;
  bool owned_runtime = false;

  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) {
      return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) {
      return result;
    }

    owned_runtime = (g_runtime_active && g_runtime_owner == handle);
    if (owned_runtime) {
      g_runtime_active = false;
      g_runtime_owner = nullptr;
      impl->runtime_owner = false;
    }

    impl->state = ToStateValue(EngineState::kDestroyed);
    ClearHandleErrorLocked(impl);
    g_live_handles.erase(handle);
  }

  if (owned_runtime) {
    try {
      Application->OnDeactivate();
    } catch (...) {
    }
    Application->FilterUserMessage(
        [](std::vector<std::tuple<void*, int, tTVPApplication::tMsg>>& queue) {
          queue.clear();
        });

    // Avoid triggering platform exit() path in the host process.
    TVPTerminated = false;
    TVPTerminateCode = 0;
  }

  delete impl;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_open_game(engine_handle_t handle,
                                 const char* game_root_path_utf8,
                                 const char* startup_script_utf8) {
  (void)startup_script_utf8;

  if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "game_root_path_utf8 is null or empty");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is already destroyed");
  }

  if (g_runtime_active) {
    if (g_runtime_owner != handle) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INVALID_STATE,
          "runtime is already active on another engine handle");
    }

    impl->state = ToStateValue(EngineState::kOpened);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (g_runtime_started_once) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_NOT_SUPPORTED,
        "runtime restart is not supported yet; restart process to open another game");
  }

  TVPTerminated = false;
  TVPTerminateCode = 0;
  TVPSystemUninitCalled = false;
  TVPTerminateOnWindowClose = false;
  TVPTerminateOnNoWindowStartup = false;
  TVPHostSuppressProcessExit = true;

  if (!EnsureEngineRuntimeInitialized(impl->surface_width, impl->surface_height)) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INTERNAL_ERROR,
        "failed to initialize engine runtime for host mode");
  }

  // Initialize loggers early so signal handlers and flush-on-debug are active
  EnsureRuntimeLoggersInitialized();

  spdlog::info("engine_open_game: runtime initialized, starting application with path: {}", game_root_path_utf8);
  spdlog::default_logger()->flush();

  try {
    spdlog::debug("engine_open_game: calling Application->StartApplication...");
    spdlog::default_logger()->flush();
    Application->StartApplication(ttstr(game_root_path_utf8));
    spdlog::info("engine_open_game: StartApplication returned successfully");
  } catch (const std::exception& e) {
    spdlog::error("engine_open_game: StartApplication threw std::exception: {}", e.what());
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INTERNAL_ERROR,
                                         "StartApplication threw an exception");
  } catch (...) {
    spdlog::error("engine_open_game: StartApplication threw unknown exception");
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INTERNAL_ERROR,
                                         "StartApplication threw an exception");
  }

  if (TVPTerminated) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "runtime requested termination during startup");
  }

  // Create EngineLoop and start the frame update loop.
  EngineLoop::CreateInstance();
  if (auto* loop = EngineLoop::GetInstance(); loop != nullptr) {
    loop->Start();
  }

  // Keep TVPMainScene alive for backward compatibility.
  if (auto* scene = TVPMainScene::GetInstance(); scene != nullptr) {
    scene->scheduleUpdate();
  }

  // No native GLFW window in ANGLE Pbuffer mode, so no mouse callbacks
  // to disable. The native_mouse_callbacks_disabled flag is kept for
  // backward compatibility but is a no-op now.
  impl->native_mouse_callbacks_disabled = true;

  g_runtime_active = true;
  g_runtime_owner = handle;
  g_runtime_started_once = true;

  impl->runtime_owner = true;
  impl->frame_width = 0;
  impl->frame_height = 0;
  impl->frame_stride_bytes = 0;
  impl->frame_rgba.clear();
  impl->frame_ready = false;
  impl->active_pointer_ids.clear();
  impl->pending_input_events.clear();
  impl->state = ToStateValue(EngineState::kOpened);
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_tick(engine_handle_t handle, uint32_t delta_ms) {
  (void)delta_ms;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_tick");
  }

  if (impl->state == ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is paused");
  }

  if (impl->state != ToStateValue(EngineState::kOpened)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is not in opened state");
  }

  while (!impl->pending_input_events.empty()) {
    const engine_input_event_t queued_event = impl->pending_input_events.front();
    impl->pending_input_events.pop_front();

    const char* dispatch_error = nullptr;
    const engine_result_t dispatch_result =
        DispatchInputEventNow(impl, queued_event, &dispatch_error);
    if (dispatch_result != ENGINE_RESULT_OK) {
      return SetHandleErrorAndReturnLocked(
          impl, dispatch_result,
          dispatch_error != nullptr ? dispatch_error : "input dispatch failed");
    }
  }

  if (TVPTerminated) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "runtime has been terminated");
  }

  // Drive one full frame (scene update + render + swap). In host mode
  // we must call Application->Run() which processes messages, triggers
  // scene composition, and invokes BasicDrawDevice::Show() →
  // form->UpdateDrawBuffer() — the actual rendering path.
  // TVPDrawSceneOnce() only restores GL state and calls SwapBuffer,
  // which is insufficient.
  if (::Application) {
    ::Application->Run();
  }
  ::TVPDrawSceneOnce(0);

  // Process deferred texture deletions. iTVPTexture2D::Release() uses
  // delayed deletion — textures are queued in _toDeleteTextures and only
  // freed when RecycleProcess() is called. Without this, every texture
  // released during the frame (via Independ/SetSize/Recreate) accumulates
  // indefinitely, causing a memory leak — especially visible in OpenGL
  // mode where each texture also holds GPU resources.
  iTVPTexture2D::RecycleProcess();

  if (TVPTerminated) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "runtime requested termination");
  }

  // Mark that a frame was rendered this tick (for IOSurface mode notification)
  impl->frame_rendered_this_tick = true;

  // In IOSurface mode, the engine renders directly to the shared IOSurface
  // via the FBO — no need for glReadPixels. Skip the expensive readback.
  if (!impl->iosurface_attached) {
    // Legacy Pbuffer readback path (slow, for backward compatibility)
    const FrameReadbackLayout layout = GetFrameReadbackLayoutLocked(impl);
    const size_t required_size =
        static_cast<size_t>(layout.stride_bytes) *
        static_cast<size_t>(layout.height);
    if (impl->frame_rgba.size() != required_size) {
      impl->frame_rgba.assign(required_size, 0);
    }

    if (required_size > 0 &&
        ReadCurrentFrameRgba(layout, impl->frame_rgba.data())) {
      impl->frame_width = layout.width;
      impl->frame_height = layout.height;
      impl->frame_stride_bytes = layout.stride_bytes;
      impl->frame_ready = true;
      impl->frame_serial += 1;
    } else if (!impl->frame_ready && required_size > 0) {
      std::fill(impl->frame_rgba.begin(), impl->frame_rgba.end(), 0);
      impl->frame_width = layout.width;
      impl->frame_height = layout.height;
      impl->frame_stride_bytes = layout.stride_bytes;
      impl->frame_ready = true;
      impl->frame_serial += 1;
    }
  } else {
    // IOSurface mode — just increment frame serial, no readback needed.
    // The render output is already in the shared IOSurface.
    glFlush(); // Ensure GPU commands are submitted
    impl->frame_serial += 1;
    impl->frame_ready = true;
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_pause(engine_handle_t handle) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_pause");
  }

  if (impl->state == ToStateValue(EngineState::kPaused)) {
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (impl->state != ToStateValue(EngineState::kOpened)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine_pause requires opened state");
  }

  Application->OnDeactivate();
  impl->active_pointer_ids.clear();
  impl->pending_input_events.clear();
  impl->state = ToStateValue(EngineState::kPaused);
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_resume(engine_handle_t handle) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_resume");
  }

  if (impl->state == ToStateValue(EngineState::kOpened)) {
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (impl->state != ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine_resume requires paused state");
  }

  Application->OnActivate();
  impl->state = ToStateValue(EngineState::kOpened);
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_option(engine_handle_t handle,
                                  const engine_option_t* option) {
  if (option == nullptr || option->key_utf8 == nullptr || option->key_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "option and option->key_utf8 must be non-null/non-empty");
  }
  if (option->value_utf8 == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "option->value_utf8 must be non-null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_surface_size(engine_handle_t handle,
                                        uint32_t width,
                                        uint32_t height) {
  if (width == 0 || height == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "width and height must be > 0");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is already destroyed");
  }

  impl->surface_width = width;
  impl->surface_height = height;
  impl->frame_width = 0;
  impl->frame_height = 0;
  impl->frame_stride_bytes = 0;
  impl->frame_rgba.clear();
  impl->frame_ready = false;

  // Propagate the new surface size to the EGL Pbuffer and viewport.
  if (g_runtime_active && g_runtime_owner == handle) {
    auto& egl = krkr::GetEngineEGLContext();
    if (egl.IsValid()) {
      const uint32_t cur_w = egl.GetWidth();
      const uint32_t cur_h = egl.GetHeight();
      if (cur_w != width || cur_h != height) {
        egl.Resize(width, height);
        glViewport(0, 0, static_cast<GLsizei>(width),
                   static_cast<GLsizei>(height));
      }
    }

    // Only update WindowSize here — DestRect is exclusively managed by
    // UpdateDrawBuffer() which calculates the correct letterbox viewport.
    // Setting DestRect here would overwrite the viewport offset and cause
    // mouse Y-axis misalignment when game aspect ratio != surface aspect ratio.
    if (TVPMainWindow) {
      auto* dd = TVPMainWindow->GetDrawDevice();
      if (dd) {
        dd->SetWindowSize(static_cast<tjs_int>(width),
                          static_cast<tjs_int>(height));
      }
    }
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_frame_desc(engine_handle_t handle,
                                      engine_frame_desc_t* out_frame_desc) {
  if (out_frame_desc == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_frame_desc is null");
  }
  if (out_frame_desc->struct_size < sizeof(engine_frame_desc_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_frame_desc_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is already destroyed");
  }

  FrameReadbackLayout layout;
  if (impl->frame_ready && impl->frame_width > 0 && impl->frame_height > 0 &&
      impl->frame_stride_bytes > 0) {
    layout.width = impl->frame_width;
    layout.height = impl->frame_height;
    layout.stride_bytes = impl->frame_stride_bytes;
  } else {
    layout = GetFrameReadbackLayoutLocked(impl);
  }

  std::memset(out_frame_desc, 0, sizeof(*out_frame_desc));
  out_frame_desc->struct_size = sizeof(engine_frame_desc_t);
  out_frame_desc->width = layout.width;
  out_frame_desc->height = layout.height;
  out_frame_desc->stride_bytes = layout.stride_bytes;
  out_frame_desc->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
  out_frame_desc->frame_serial = impl->frame_serial;

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_read_frame_rgba(engine_handle_t handle,
                                       void* out_pixels,
                                       size_t out_pixels_size) {
  if (out_pixels == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_pixels is null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_read_frame_rgba");
  }

  FrameReadbackLayout layout;
  if (impl->frame_ready && impl->frame_width > 0 && impl->frame_height > 0 &&
      impl->frame_stride_bytes > 0) {
    layout.width = impl->frame_width;
    layout.height = impl->frame_height;
    layout.stride_bytes = impl->frame_stride_bytes;
  } else {
    layout = GetFrameReadbackLayoutLocked(impl);
    const size_t required_size =
        static_cast<size_t>(layout.stride_bytes) *
        static_cast<size_t>(layout.height);
    impl->frame_rgba.assign(required_size, 0);
    impl->frame_width = layout.width;
    impl->frame_height = layout.height;
    impl->frame_stride_bytes = layout.stride_bytes;
    impl->frame_ready = true;
  }

  const size_t required_size =
      static_cast<size_t>(layout.stride_bytes) *
      static_cast<size_t>(layout.height);
  if (out_pixels_size < required_size) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_ARGUMENT,
        "out_pixels_size is smaller than required frame buffer size");
  }

  if (impl->frame_rgba.size() < required_size) {
    impl->frame_rgba.resize(required_size, 0);
  }
  std::memcpy(out_pixels, impl->frame_rgba.data(), required_size);

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_host_native_window(engine_handle_t handle,
                                              void** out_window_handle) {
  if (out_window_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_window_handle is null");
  }
  *out_window_handle = nullptr;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_get_host_native_window");
  }

#if defined(TARGET_OS_MAC) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  // No native GLFW window in ANGLE Pbuffer mode.
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "engine_get_host_native_window is not supported in headless ANGLE mode");
#else
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "engine_get_host_native_window is only supported on macOS runtime");
#endif
}

engine_result_t engine_get_host_native_view(engine_handle_t handle,
                                            void** out_view_handle) {
  if (out_view_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_view_handle is null");
  }
  *out_view_handle = nullptr;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_get_host_native_view");
  }

  // No native GLFW window in ANGLE Pbuffer mode — native view is unavailable.
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "engine_get_host_native_view is not supported in headless ANGLE mode");
}

engine_result_t engine_send_input(engine_handle_t handle,
                                  const engine_input_event_t* event) {
  if (event == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "event is null");
  }
  if (event->struct_size < sizeof(engine_input_event_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_input_event_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state == ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is paused");
  }
  if (impl->state != ToStateValue(EngineState::kOpened)) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_send_input");
  }

  switch (event->type) {
    case ENGINE_INPUT_EVENT_POINTER_DOWN:
    case ENGINE_INPUT_EVENT_POINTER_MOVE:
    case ENGINE_INPUT_EVENT_POINTER_UP:
    case ENGINE_INPUT_EVENT_POINTER_SCROLL:
    case ENGINE_INPUT_EVENT_KEY_DOWN:
    case ENGINE_INPUT_EVENT_KEY_UP:
    case ENGINE_INPUT_EVENT_TEXT_INPUT:
    case ENGINE_INPUT_EVENT_BACK:
      break;
    default:
      return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_NOT_SUPPORTED,
                                           "unsupported input event type");
  }

  if (event->type == ENGINE_INPUT_EVENT_POINTER_DOWN ||
      event->type == ENGINE_INPUT_EVENT_POINTER_MOVE ||
      event->type == ENGINE_INPUT_EVENT_POINTER_UP ||
      event->type == ENGINE_INPUT_EVENT_POINTER_SCROLL) {
    if (!IsFinitePointerValue(event->x) || !IsFinitePointerValue(event->y) ||
        !IsFinitePointerValue(event->delta_x) ||
        !IsFinitePointerValue(event->delta_y)) {
      return SetHandleErrorAndReturnLocked(
          impl, ENGINE_RESULT_INVALID_ARGUMENT,
          "pointer coordinates contain non-finite values");
    }
  }

  impl->pending_input_events.push_back(*event);
  constexpr size_t kMaxQueuedInputs = 512;
  if (impl->pending_input_events.size() > kMaxQueuedInputs) {
    impl->pending_input_events.pop_front();
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_render_target_iosurface(engine_handle_t handle,
                                                    uint32_t iosurface_id,
                                                    uint32_t width,
                                                    uint32_t height) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_set_render_target_iosurface");
  }

#if defined(__APPLE__)
  auto& egl = krkr::GetEngineEGLContext();
  if (!egl.IsValid()) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "EGL context not initialized");
  }

  if (iosurface_id == 0) {
    // Detach — revert to Pbuffer mode
    egl.DetachIOSurface();
    impl->iosurface_attached = false;
    spdlog::info("engine_set_render_target_iosurface: detached, Pbuffer mode");
  } else {
    if (width == 0 || height == 0) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INVALID_ARGUMENT,
          "width and height must be > 0 when setting IOSurface");
    }
    if (!egl.AttachIOSurface(iosurface_id, width, height)) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INTERNAL_ERROR,
          "failed to attach IOSurface as render target");
    }
    impl->iosurface_attached = true;
    spdlog::info("engine_set_render_target_iosurface: attached id={} {}x{}",
                 iosurface_id, width, height);

    // Only update WindowSize here — DestRect is exclusively managed by
    // UpdateDrawBuffer() which calculates the correct letterbox viewport.
    // Setting DestRect here would overwrite the viewport offset and cause
    // mouse Y-axis misalignment when game aspect ratio != surface aspect ratio.
    if (TVPMainWindow) {
      auto* dd = TVPMainWindow->GetDrawDevice();
      if (dd) {
        dd->SetWindowSize(static_cast<tjs_int>(width),
                          static_cast<tjs_int>(height));
      }
    }
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
#else
  (void)iosurface_id;
  (void)width;
  (void)height;
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "IOSurface render target is only supported on macOS");
#endif
}

engine_result_t engine_get_frame_rendered_flag(engine_handle_t handle,
                                                uint32_t* out_flag) {
  if (out_flag == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_flag is null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    *out_flag = 0;
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  *out_flag = impl->frame_rendered_this_tick ? 1 : 0;
  impl->frame_rendered_this_tick = false;

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_renderer_info(engine_handle_t handle,
                                         char* out_buffer,
                                         uint32_t buffer_size) {
  if (out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_buffer is null or buffer_size is 0");
  }
  out_buffer[0] = '\0';

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_get_renderer_info");
  }

  // Query GL renderer and version strings from the active ANGLE context.
  const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  if (!gl_renderer) gl_renderer = "(unknown)";
  if (!gl_version) gl_version = "(unknown)";

  // Build a combined info string.
  std::string info = std::string(gl_renderer) + " | " + std::string(gl_version);
  std::strncpy(out_buffer, info.c_str(), buffer_size - 1);
  out_buffer[buffer_size - 1] = '\0';

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

const char* engine_get_last_error(engine_handle_t handle) {
  if (handle == nullptr) {
    return g_thread_error.c_str();
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  if (!IsHandleLiveLocked(handle)) {
    SetThreadError("engine handle is invalid or already destroyed");
    return g_thread_error.c_str();
  }
  auto* impl = reinterpret_cast<engine_handle_s*>(handle);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  return impl->last_error.c_str();
}

}  // extern "C"

#else

#include <cstring>
#include <new>
#include <string>
#include <unordered_set>
#include <mutex>

struct engine_handle_s {
  std::recursive_mutex mutex;
  std::string last_error;
  int state = 0;
  uint32_t surface_width = 1280;
  uint32_t surface_height = 720;
  uint64_t frame_serial = 0;
};

namespace {

enum class EngineState {
  kCreated = 0,
  kOpened,
  kPaused,
  kDestroyed,
};

inline int ToStateValue(EngineState state) {
  return static_cast<int>(state);
}

std::recursive_mutex g_registry_mutex;
std::unordered_set<engine_handle_t> g_live_handles;
thread_local std::string g_thread_error;

void SetThreadError(const char* message) {
  g_thread_error = (message != nullptr) ? message : "";
}

engine_result_t SetThreadErrorAndReturn(engine_result_t result,
                                        const char* message) {
  SetThreadError(message);
  return result;
}

bool IsHandleLiveLocked(engine_handle_t handle) {
  return g_live_handles.find(handle) != g_live_handles.end();
}

engine_result_t ValidateHandleLocked(engine_handle_t handle,
                                     engine_handle_s** out_impl) {
  if (handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine handle is null");
  }
  if (!IsHandleLiveLocked(handle)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine handle is invalid or already destroyed");
  }
  *out_impl = reinterpret_cast<engine_handle_s*>(handle);
  return ENGINE_RESULT_OK;
}

void SetHandleErrorLocked(engine_handle_s* impl, const char* message) {
  impl->last_error = (message != nullptr) ? message : "";
}

}  // namespace

extern "C" {

engine_result_t engine_get_runtime_api_version(uint32_t* out_api_version) {
  if (out_api_version == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_api_version is null");
  }
  *out_api_version = ENGINE_API_VERSION;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_create(const engine_create_desc_t* desc,
                              engine_handle_t* out_handle) {
  if (desc == nullptr || out_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine_create requires non-null desc and out_handle");
  }

  if (desc->struct_size < sizeof(engine_create_desc_t)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine_create_desc_t.struct_size is too small");
  }

  const uint32_t expected_major = (ENGINE_API_VERSION >> 24u) & 0xFFu;
  const uint32_t caller_major = (desc->api_version >> 24u) & 0xFFu;
  if (caller_major != expected_major) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                   "unsupported engine API major version");
  }

  auto* impl = new (std::nothrow) engine_handle_s();
  if (impl == nullptr) {
    *out_handle = nullptr;
    return SetThreadErrorAndReturn(ENGINE_RESULT_INTERNAL_ERROR,
                                   "failed to allocate engine handle");
  }
  impl->state = ToStateValue(EngineState::kCreated);

  auto handle = reinterpret_cast<engine_handle_t>(impl);
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    g_live_handles.insert(handle);
  }

  *out_handle = handle;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_destroy(engine_handle_t handle) {
  if (handle == nullptr) {
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  engine_handle_s* impl = nullptr;
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    auto it = g_live_handles.find(handle);
    if (it == g_live_handles.end()) {
      return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                     "engine handle is invalid or already destroyed");
    }
    impl = reinterpret_cast<engine_handle_s*>(handle);
    g_live_handles.erase(it);
  }

  {
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    impl->state = ToStateValue(EngineState::kDestroyed);
    impl->last_error.clear();
  }
  delete impl;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_open_game(engine_handle_t handle,
                                 const char* game_root_path_utf8,
                                 const char* startup_script_utf8) {
  (void)startup_script_utf8;

  if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "game_root_path_utf8 is null or empty");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    SetHandleErrorLocked(impl, "engine is already destroyed");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->state = ToStateValue(EngineState::kOpened);
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_tick(engine_handle_t handle, uint32_t delta_ms) {
  (void)delta_ms;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(impl, "engine is paused");
    return ENGINE_RESULT_INVALID_STATE;
  }
  if (impl->state != ToStateValue(EngineState::kOpened)) {
    SetHandleErrorLocked(impl, "engine_open_game must succeed before engine_tick");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->frame_serial += 1;
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_pause(engine_handle_t handle) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kPaused)) {
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }
  if (impl->state != ToStateValue(EngineState::kOpened)) {
    SetHandleErrorLocked(impl, "engine_pause requires opened state");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->state = ToStateValue(EngineState::kPaused);
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_resume(engine_handle_t handle) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kOpened)) {
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }
  if (impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(impl, "engine_resume requires paused state");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->state = ToStateValue(EngineState::kOpened);
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_option(engine_handle_t handle,
                                  const engine_option_t* option) {
  if (option == nullptr || option->key_utf8 == nullptr || option->key_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "option and option->key_utf8 must be non-null/non-empty");
  }
  if (option->value_utf8 == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "option->value_utf8 must be non-null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    SetHandleErrorLocked(impl, "engine is already destroyed");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_surface_size(engine_handle_t handle,
                                        uint32_t width,
                                        uint32_t height) {
  if (width == 0 || height == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "width and height must be > 0");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    SetHandleErrorLocked(impl, "engine is already destroyed");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->surface_width = width;
  impl->surface_height = height;
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_frame_desc(engine_handle_t handle,
                                      engine_frame_desc_t* out_frame_desc) {
  if (out_frame_desc == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_frame_desc is null");
  }
  if (out_frame_desc->struct_size < sizeof(engine_frame_desc_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_frame_desc_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    SetHandleErrorLocked(impl, "engine is already destroyed");
    return ENGINE_RESULT_INVALID_STATE;
  }

  std::memset(out_frame_desc, 0, sizeof(*out_frame_desc));
  out_frame_desc->struct_size = sizeof(engine_frame_desc_t);
  out_frame_desc->width = impl->surface_width;
  out_frame_desc->height = impl->surface_height;
  out_frame_desc->stride_bytes = impl->surface_width * 4u;
  out_frame_desc->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
  out_frame_desc->frame_serial = impl->frame_serial;

  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_read_frame_rgba(engine_handle_t handle,
                                       void* out_pixels,
                                       size_t out_pixels_size) {
  if (out_pixels == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_pixels is null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(impl,
                         "engine_open_game must succeed before engine_read_frame_rgba");
    return ENGINE_RESULT_INVALID_STATE;
  }

  const size_t required_size =
      static_cast<size_t>(impl->surface_width) *
      static_cast<size_t>(impl->surface_height) * 4u;
  if (out_pixels_size < required_size) {
    SetHandleErrorLocked(
        impl,
        "out_pixels_size is smaller than required frame buffer size");
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }

  std::memset(out_pixels, 0, required_size);
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_host_native_window(engine_handle_t handle,
                                              void** out_window_handle) {
  if (out_window_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_window_handle is null");
  }
  *out_window_handle = nullptr;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(
        impl,
        "engine_open_game must succeed before engine_get_host_native_window");
    return ENGINE_RESULT_INVALID_STATE;
  }

  SetHandleErrorLocked(impl,
                       "engine_get_host_native_window is not supported");
  return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_get_host_native_view(engine_handle_t handle,
                                            void** out_view_handle) {
  if (out_view_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_view_handle is null");
  }
  *out_view_handle = nullptr;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(
        impl,
        "engine_open_game must succeed before engine_get_host_native_view");
    return ENGINE_RESULT_INVALID_STATE;
  }

  SetHandleErrorLocked(impl, "engine_get_host_native_view is not supported");
  return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_send_input(engine_handle_t handle,
                                  const engine_input_event_t* event) {
  if (event == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "event is null");
  }
  if (event->struct_size < sizeof(engine_input_event_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_input_event_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(impl, "engine is paused");
    return ENGINE_RESULT_INVALID_STATE;
  }
  if (impl->state != ToStateValue(EngineState::kOpened)) {
    SetHandleErrorLocked(impl,
                         "engine_open_game must succeed before engine_send_input");
    return ENGINE_RESULT_INVALID_STATE;
  }

  switch (event->type) {
    case ENGINE_INPUT_EVENT_POINTER_DOWN:
    case ENGINE_INPUT_EVENT_POINTER_MOVE:
    case ENGINE_INPUT_EVENT_POINTER_UP:
    case ENGINE_INPUT_EVENT_POINTER_SCROLL:
    case ENGINE_INPUT_EVENT_KEY_DOWN:
    case ENGINE_INPUT_EVENT_KEY_UP:
    case ENGINE_INPUT_EVENT_TEXT_INPUT:
    case ENGINE_INPUT_EVENT_BACK:
      break;
    default:
      SetHandleErrorLocked(impl, "unsupported input event type");
      return ENGINE_RESULT_NOT_SUPPORTED;
  }

  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_render_target_iosurface(engine_handle_t handle,
                                                    uint32_t iosurface_id,
                                                    uint32_t width,
                                                    uint32_t height) {
  (void)iosurface_id;
  (void)width;
  (void)height;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  SetHandleErrorLocked(impl,
                       "engine_set_render_target_iosurface is not supported in stub build");
  return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_get_frame_rendered_flag(engine_handle_t handle,
                                                uint32_t* out_flag) {
  if (out_flag == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_flag is null");
  }
  *out_flag = 0;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_renderer_info(engine_handle_t handle,
                                         char* out_buffer,
                                         uint32_t buffer_size) {
  if (out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_buffer is null or buffer_size is 0");
  }
  out_buffer[0] = '\0';

  // Stub build — return a placeholder string.
  const char* stub_info = "Stub (no runtime)";
  std::strncpy(out_buffer, stub_info, buffer_size - 1);
  out_buffer[buffer_size - 1] = '\0';
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

const char* engine_get_last_error(engine_handle_t handle) {
  if (handle == nullptr) {
    return g_thread_error.c_str();
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  if (!IsHandleLiveLocked(handle)) {
    SetThreadError("engine handle is invalid or already destroyed");
    return g_thread_error.c_str();
  }
  auto* impl = reinterpret_cast<engine_handle_s*>(handle);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  return impl->last_error.c_str();
}

}  // extern "C"

#endif
