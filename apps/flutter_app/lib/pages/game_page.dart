import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../engine/engine_bridge.dart';
import '../engine/flutter_engine_bridge_adapter.dart';
import '../widgets/engine_surface.dart';
import '../widgets/performance_overlay.dart';

/// The game running page — full-screen engine surface with auto-start flow.
class GamePage extends StatefulWidget {
  const GamePage({
    super.key,
    required this.gamePath,
    this.ffiLibraryPath,
    this.engineBridgeBuilder = createEngineBridge,
  });

  final String gamePath;
  final String? ffiLibraryPath;
  final EngineBridgeBuilder engineBridgeBuilder;

  @override
  State<GamePage> createState() => _GamePageState();
}

class _GamePageState extends State<GamePage>
    with WidgetsBindingObserver {
  static const int _engineResultOk = 0;

  late EngineBridge _bridge;
  final GlobalKey<EngineSurfaceState> _surfaceKey =
      GlobalKey<EngineSurfaceState>();

  static const String _perfOverlayKey = 'krkr2_perf_overlay';
  static const String _targetFpsKey = 'krkr2_target_fps';
  static const String _rendererKey = 'krkr2_renderer';
  static const int _defaultFps = 60;

  Ticker? _ticker;
  bool _tickInFlight = false;
  bool _isTicking = false;
  bool _autoPausedByLifecycle = false;
  bool _resumeTickAfterLifecycle = false;
  bool _lifecycleTransitionInFlight = false;
  /// When true, a resume was requested while a pause transition was
  /// still in flight. The pause handler checks this on completion and
  /// triggers a resume automatically.
  bool _pendingLifecycleResumed = false;

  // Frame rate
  int _targetFps = _defaultFps;
  /// How many vsync ticks to skip between engine ticks.
  /// e.g. 0 = every vsync (60 FPS on a 60 Hz display),
  ///      1 = every other vsync (30 FPS on a 60 Hz display).
  int _vsyncSkip = 0;

  // Performance overlay
  bool _showPerfOverlay = false;
  String _rendererInfo = '';
  final GlobalKey<EnginePerformanceOverlayState> _perfOverlayKey0 =
      GlobalKey<EnginePerformanceOverlayState>();

  // State
  _EnginePhase _phase = _EnginePhase.initializing;
  String? _errorMessage;
  bool _showOverlay = false;
  bool _showDebug = false;
  int _tickCount = 0;
  final List<String> _logs = [];
  static const int _maxLogs = 80;

  // ScrollController for boot log auto-scroll
  final ScrollController _bootLogScrollController = ScrollController();

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _bridge = widget.engineBridgeBuilder(ffiLibraryPath: widget.ffiLibraryPath);
    _loadSettings();
    _log('Initializing engine for: ${widget.gamePath}');
    if (widget.ffiLibraryPath != null) {
      _log('Using custom dylib: ${widget.ffiLibraryPath}');
    }
    // Defer engine startup until after the first frame is rendered,
    // so the boot-log UI is visible before blocking FFI calls begin.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) {
        unawaited(_autoStart());
      }
    });
  }

  @override
  void dispose() {
    _bootLogScrollController.dispose();
    WidgetsBinding.instance.removeObserver(this);
    _stopTickLoop(notify: false);
    unawaited(_bridge.engineDestroy());
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    switch (state) {
      case AppLifecycleState.resumed:
        unawaited(_resumeForLifecycle());
        break;
      case AppLifecycleState.hidden:
      case AppLifecycleState.paused:
        // Only pause when the app is truly invisible (hidden/paused).
        // On macOS desktop, switching windows only triggers 'inactive',
        // which should NOT pause the engine — the window is still
        // partially visible and the user expects the game to keep running.
        unawaited(_pauseForLifecycle());
        break;
      case AppLifecycleState.inactive:
        // On mobile, 'inactive' precedes 'hidden'/'paused' so we let those
        // handle the pause. On desktop, 'inactive' is just focus-lost and
        // should be ignored to avoid freezing the game on window switch.
        break;
      case AppLifecycleState.detached:
        break;
    }
  }

  // --- Auto-start flow: create → open → tick ---

  Future<void> _autoStart() async {
    setState(() => _phase = _EnginePhase.creating);
    _log('engine_create...');

    // Yield to let the UI paint the current log state before the
    // synchronous FFI call blocks the main thread.
    await Future<void>.delayed(Duration.zero);

    final int createResult = await _bridge.engineCreate();
    if (createResult != _engineResultOk) {
      _fail('engine_create failed: result=$createResult, '
          'error=${_bridge.engineGetLastError()}');
      return;
    }
    _log('engine_create => OK');

    // Set renderer pipeline (opengl / software) before opening the game
    final prefs = await SharedPreferences.getInstance();
    final renderer = prefs.getString(_rendererKey) ?? 'opengl';
    _log('Setting renderer=$renderer');
    await _bridge.engineSetOption(key: 'renderer', value: renderer);

    if (!mounted) return;
    setState(() => _phase = _EnginePhase.opening);
    _log('engine_open_game(${widget.gamePath})...');
    _log('Starting application — this may take a moment...');

    // Yield again so the "Opening game..." log line is painted before
    // the heavy engine_open_game call blocks the thread.
    await Future<void>.delayed(Duration.zero);

    final int openResult = await _bridge.engineOpenGame(widget.gamePath);
    if (openResult != _engineResultOk) {
      _fail('engine_open_game failed: result=$openResult, '
          'error=${_bridge.engineGetLastError()}');
      return;
    }
    _log('engine_open_game => OK');

    if (!mounted) return;
    setState(() => _phase = _EnginePhase.running);
    _fetchRendererInfo();
    _startTickLoop();
  }

  void _fail(String message) {
    _log('ERROR: $message');
    if (!mounted) return;
    setState(() {
      _phase = _EnginePhase.error;
      _errorMessage = message;
    });
  }

  // --- Tick loop (vsync-driven) ---

  void _startTickLoop() {
    if (_isTicking) return;
    setState(() => _isTicking = true);
    _log('Tick loop started');

    Duration lastElapsed = Duration.zero;
    int vsyncCounter = 0;
    _ticker = Ticker((Duration elapsed) async {
      if (_tickInFlight) return;

      // Throttle: skip vsync ticks to match target FPS
      if (_vsyncSkip > 0) {
        vsyncCounter++;
        if (vsyncCounter <= _vsyncSkip) return;
        vsyncCounter = 0;
      }

      final int deltaMs = lastElapsed == Duration.zero
          ? 16
          : (elapsed - lastElapsed).inMilliseconds.clamp(1, 100);
      lastElapsed = elapsed;

      _tickInFlight = true;
      try {
        final int result = await _bridge.engineTick(deltaMs: deltaMs);
        if (!mounted) return;

        if (result != _engineResultOk) {
          _ticker?.stop();
          final error = _bridge.engineGetLastError();
          _log('Tick faulted: result=$result, error=$error');
          setState(() {
            _isTicking = false;
            _phase = _EnginePhase.error;
            _errorMessage = 'engine_tick failed: $result ($error)';
          });
          return;
        }

        // Report frame delta to the performance overlay
        _perfOverlayKey0.currentState?.reportFrameDelta(deltaMs.toDouble());

        // Poll frame immediately after tick
        await _surfaceKey.currentState?.pollFrame();
        _tickCount += 1;

        if (_tickCount % 300 == 0) {
          _log('Tick alive: count=$_tickCount');
        }
      } finally {
        _tickInFlight = false;
      }
    });
    _ticker!.start();
  }

  void _stopTickLoop({bool notify = true}) {
    _ticker?.stop();
    _ticker?.dispose();
    _ticker = null;

    _isTicking = false;
    if (notify && mounted) {
      setState(() {});
    }
  }

  // --- Lifecycle ---

  Future<void> _pauseForLifecycle() async {
    // If a resume comes while we are still pausing, record it so we
    // can bounce back after the pause finishes.
    if (_lifecycleTransitionInFlight) {
      // Another transition is running — just clear the pending-resume
      // flag; we want to stay paused.
      _pendingLifecycleResumed = false;
      return;
    }
    if (!mounted || _autoPausedByLifecycle || _phase != _EnginePhase.running) {
      return;
    }
    _lifecycleTransitionInFlight = true;
    _pendingLifecycleResumed = false;
    final bool wasTicking = _isTicking;
    if (wasTicking) _stopTickLoop();

    try {
      final int result = await _bridge.enginePause();
      if (result == _engineResultOk && mounted) {
        _autoPausedByLifecycle = true;
        _resumeTickAfterLifecycle = wasTicking;
        _log('Lifecycle paused');
      }
    } finally {
      _lifecycleTransitionInFlight = false;
    }

    // If a resume was requested while the pause was in-flight, honour
    // it now so the engine doesn't stay frozen.
    if (_pendingLifecycleResumed && mounted) {
      _pendingLifecycleResumed = false;
      await _resumeForLifecycle();
    }
  }

  Future<void> _resumeForLifecycle() async {
    // If a pause is still in-flight, record that we want to resume
    // once it completes.
    if (_lifecycleTransitionInFlight) {
      _pendingLifecycleResumed = true;
      return;
    }
    if (!mounted || !_autoPausedByLifecycle) {
      return;
    }
    _lifecycleTransitionInFlight = true;
    _pendingLifecycleResumed = false;

    try {
      final int result = await _bridge.engineResume();
      if (result == _engineResultOk && mounted) {
        final bool resumeTick = _resumeTickAfterLifecycle;
        _autoPausedByLifecycle = false;
        _resumeTickAfterLifecycle = false;
        _log('Lifecycle resumed');
        if (resumeTick) _startTickLoop();
      }
    } finally {
      _lifecycleTransitionInFlight = false;
    }
  }

  // --- Settings ---

  Future<void> _loadSettings() async {
    final prefs = await SharedPreferences.getInstance();
    if (mounted) {
      final fps = prefs.getInt(_targetFpsKey) ?? _defaultFps;
      setState(() {
        _showPerfOverlay = prefs.getBool(_perfOverlayKey) ?? false;
        _targetFps = fps;
        _updateVsyncSkip();
      });
    }
  }

  /// Recalculate how many vsync ticks to skip based on _targetFps.
  /// Assumes the display runs at ~60 Hz (Flutter's default Ticker rate).
  void _updateVsyncSkip() {
    const int displayHz = 60;
    if (_targetFps >= displayHz) {
      // 60 FPS or higher → tick every vsync
      _vsyncSkip = 0;
    } else if (_targetFps > 0) {
      // e.g. 30 FPS → skip = round(60/30) - 1 = 1 (tick every 2nd vsync)
      _vsyncSkip = (displayHz / _targetFps).round() - 1;
      if (_vsyncSkip < 0) _vsyncSkip = 0;
    } else {
      _vsyncSkip = 0;
    }
  }

  void _fetchRendererInfo() {
    try {
      _rendererInfo = _bridge.engineGetRendererInfo();
      if (mounted) setState(() {});
    } catch (_) {
      // Renderer info is best-effort
    }
  }

  // --- Logging ---

  void _log(String message) {
    final now = DateTime.now();
    final time =
        '${now.hour.toString().padLeft(2, '0')}:${now.minute.toString().padLeft(2, '0')}:${now.second.toString().padLeft(2, '0')}';
    _logs.insert(0, '[$time] $message');
    if (_logs.length > _maxLogs) {
      _logs.removeRange(_maxLogs, _logs.length);
    }
    // Try to update the UI if we're in a loading phase
    if (mounted && _phase != _EnginePhase.running) {
      setState(() {});
    }
  }

  // --- UI ---

  void _toggleOverlay() {
    setState(() => _showOverlay = !_showOverlay);
  }

  void _toggleDebug() {
    setState(() => _showDebug = !_showDebug);
  }

  void _exitGame() {
    _stopTickLoop(notify: false);
    Navigator.of(context).pop();
  }

  @override
  Widget build(BuildContext context) {
    final bool surfaceActive = _phase == _EnginePhase.running;

    return Scaffold(
      backgroundColor: Colors.black,
      body: Stack(
        fit: StackFit.expand,
        children: [
          // Full-screen engine surface
          Positioned.fill(
            child: _phase == _EnginePhase.error
                ? _buildErrorView()
                : _phase == _EnginePhase.running
                    ? EngineSurface(
                        key: _surfaceKey,
                        bridge: _bridge,
                        active: surfaceActive,
                        surfaceMode: EngineSurfaceMode.iosurface,
                        externalTickDriven: _isTicking,
                        onLog: (msg) => _log('surface: $msg'),
                        onError: (msg) => _log('surface error: $msg'),
                      )
                    : _buildBootLogView(),
          ),

          // Performance overlay (top-left)
          if (_showPerfOverlay && _phase == _EnginePhase.running)
            EnginePerformanceOverlay(
              key: _perfOverlayKey0,
              rendererInfo: _rendererInfo,
            ),

          // Floating menu button (top-right)
          Positioned(
            right: 16,
            top: MediaQuery.of(context).padding.top + 8,
            child: AnimatedOpacity(
              opacity: _showOverlay ? 1.0 : 0.6,
              duration: const Duration(milliseconds: 200),
              child: Material(
                color: Colors.black45,
                borderRadius: BorderRadius.circular(8),
                child: InkWell(
                  borderRadius: BorderRadius.circular(8),
                  onTap: _toggleOverlay,
                  child: Padding(
                    padding: const EdgeInsets.all(8),
                    child: Icon(
                      _showOverlay ? Icons.close : Icons.menu,
                      color: Colors.white70,
                      size: 24,
                    ),
                  ),
                ),
              ),
            ),
          ),

          // Overlay controls
          if (_showOverlay) _buildOverlay(),

          // Debug panel
          if (_showDebug) _buildDebugPanel(),
        ],
      ),
    );
  }

  Widget _buildBootLogView() {
    // Full-screen terminal-style boot log — no animations that would
    // freeze during synchronous FFI calls.
    final String phaseLabel;
    switch (_phase) {
      case _EnginePhase.initializing:
        phaseLabel = 'INITIALIZING';
        break;
      case _EnginePhase.creating:
        phaseLabel = 'CREATING ENGINE';
        break;
      case _EnginePhase.opening:
        phaseLabel = 'LOADING GAME';
        break;
      default:
        phaseLabel = 'LOADING';
    }

    // Reversed logs so newest is at the bottom (terminal style)
    final reversedLogs = _logs.reversed.toList();

    // Schedule scroll-to-bottom after this frame
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_bootLogScrollController.hasClients) {
        _bootLogScrollController.jumpTo(
          _bootLogScrollController.position.maxScrollExtent,
        );
      }
    });

    return Container(
      color: Colors.black,
      child: SafeArea(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // Header bar
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
              decoration: const BoxDecoration(
                border: Border(
                  bottom: BorderSide(color: Colors.white10),
                ),
              ),
              child: Row(
                children: [
                  Container(
                    width: 10,
                    height: 10,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      color: _phase == _EnginePhase.opening
                          ? Colors.orangeAccent
                          : Colors.greenAccent,
                    ),
                  ),
                  const SizedBox(width: 10),
                  Text(
                    phaseLabel,
                    style: const TextStyle(
                      color: Colors.white,
                      fontSize: 14,
                      fontWeight: FontWeight.bold,
                      fontFamily: 'monospace',
                      letterSpacing: 1.2,
                    ),
                  ),
                  const Spacer(),
                  Text(
                    'krkr2 engine',
                    style: TextStyle(
                      color: Colors.white.withValues(alpha: 0.3),
                      fontSize: 12,
                      fontFamily: 'monospace',
                    ),
                  ),
                ],
              ),
            ),
            // Game path info
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              color: Colors.white.withValues(alpha: 0.03),
              child: Text(
                widget.gamePath,
                style: TextStyle(
                  color: Colors.white.withValues(alpha: 0.4),
                  fontSize: 12,
                  fontFamily: 'monospace',
                ),
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
            ),
            // Log area — scrollable terminal output
            Expanded(
              child: reversedLogs.isEmpty
                  ? Center(
                      child: Text(
                        'Waiting...',
                        style: TextStyle(
                          color: Colors.white.withValues(alpha: 0.2),
                          fontFamily: 'monospace',
                        ),
                      ),
                    )
                  : ListView.builder(
                      controller: _bootLogScrollController,
                      padding: const EdgeInsets.all(12),
                      itemCount: reversedLogs.length,
                      itemBuilder: (context, index) {
                        final log = reversedLogs[index];
                        final isError = log.contains('ERROR');
                        final isOk = log.contains('=> OK');
                        return Padding(
                          padding: const EdgeInsets.only(bottom: 2),
                          child: Text(
                            log,
                            style: TextStyle(
                              color: isError
                                  ? Colors.redAccent
                                  : isOk
                                      ? Colors.greenAccent
                                      : Colors.white.withValues(alpha: 0.6),
                              fontSize: 12,
                              fontFamily: 'monospace',
                              height: 1.4,
                            ),
                          ),
                        );
                      },
                    ),
            ),
            // Bottom status bar
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
              decoration: const BoxDecoration(
                border: Border(
                  top: BorderSide(color: Colors.white10),
                ),
              ),
              child: Row(
                children: [
                  // Static blinking cursor indicator (just a block char)
                  const Text(
                    '█',
                    style: TextStyle(
                      color: Colors.greenAccent,
                      fontSize: 14,
                      fontFamily: 'monospace',
                    ),
                  ),
                  const SizedBox(width: 8),
                  Text(
                    _phase == _EnginePhase.opening
                        ? 'Engine is loading game resources...'
                        : 'Preparing engine...',
                    style: TextStyle(
                      color: Colors.white.withValues(alpha: 0.4),
                      fontSize: 12,
                      fontFamily: 'monospace',
                    ),
                  ),
                  const Spacer(),
                  GestureDetector(
                    onTap: _exitGame,
                    child: Text(
                      'Cancel',
                      style: TextStyle(
                        color: Colors.white.withValues(alpha: 0.3),
                        fontSize: 12,
                        fontFamily: 'monospace',
                        decoration: TextDecoration.underline,
                        decorationColor: Colors.white.withValues(alpha: 0.3),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildErrorView() {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.error_outline, color: Colors.redAccent, size: 64),
            const SizedBox(height: 16),
            const Text(
              'Engine Error',
              style: TextStyle(
                color: Colors.white,
                fontSize: 20,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 12),
            Container(
              padding: const EdgeInsets.all(16),
              decoration: BoxDecoration(
                color: Colors.white10,
                borderRadius: BorderRadius.circular(8),
              ),
              child: SelectableText(
                _errorMessage ?? 'Unknown error',
                style: const TextStyle(
                  color: Colors.white70,
                  fontSize: 13,
                  fontFamily: 'monospace',
                ),
              ),
            ),
            const SizedBox(height: 24),
            Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                OutlinedButton.icon(
                  onPressed: _exitGame,
                  icon: const Icon(Icons.arrow_back),
                  label: const Text('Back'),
                  style: OutlinedButton.styleFrom(
                    foregroundColor: Colors.white70,
                    side: const BorderSide(color: Colors.white30),
                  ),
                ),
                const SizedBox(width: 16),
                FilledButton.icon(
                  onPressed: () {
                    setState(() {
                      _phase = _EnginePhase.initializing;
                      _errorMessage = null;
                      _tickCount = 0;
                    });
                    unawaited(_bridge.engineDestroy());
                    _bridge = widget.engineBridgeBuilder(
                      ffiLibraryPath: widget.ffiLibraryPath,
                    );
                    unawaited(_autoStart());
                  },
                  icon: const Icon(Icons.refresh),
                  label: const Text('Retry'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildOverlay() {
    return Positioned(
      right: 16,
      top: MediaQuery.of(context).padding.top + 52,
      child: Material(
        color: Colors.black87,
        borderRadius: BorderRadius.circular(12),
        elevation: 8,
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 8),
          child: IntrinsicWidth(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              mainAxisSize: MainAxisSize.min,
              children: [
                _overlayItem(
                  icon: Icons.bug_report,
                  label: _showDebug ? 'Hide Debug' : 'Show Debug',
                  onTap: _toggleDebug,
                ),
                _overlayItem(
                  icon: Icons.pause,
                  label: _isTicking ? 'Pause' : 'Resume',
                  onTap: () async {
                    if (_isTicking) {
                      _stopTickLoop();
                      await _bridge.enginePause();
                      _log('User paused');
                    } else {
                      await _bridge.engineResume();
                      _startTickLoop();
                      _log('User resumed');
                    }
                    setState(() => _showOverlay = false);
                  },
                ),
                const Divider(color: Colors.white24, height: 1),
                _overlayItem(
                  icon: Icons.exit_to_app,
                  label: 'Exit Game',
                  onTap: _exitGame,
                  destructive: true,
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _overlayItem({
    required IconData icon,
    required String label,
    required VoidCallback onTap,
    bool destructive = false,
  }) {
    final color = destructive ? Colors.redAccent : Colors.white70;
    return InkWell(
      onTap: onTap,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, color: color, size: 20),
            const SizedBox(width: 12),
            Text(label, style: TextStyle(color: color, fontSize: 14)),
          ],
        ),
      ),
    );
  }

  Widget _buildDebugPanel() {
    return Positioned(
      left: 0,
      right: 0,
      bottom: 0,
      height: 200,
      child: Material(
        color: Colors.black.withValues(alpha: 0.85),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
              color: Colors.white10,
              child: Row(
                children: [
                  Text(
                    'Debug  |  Phase: ${_phase.name}  |  '
                    'Ticks: $_tickCount  |  '
                    'Ticking: $_isTicking',
                    style: const TextStyle(
                      color: Colors.white70,
                      fontSize: 12,
                      fontFamily: 'monospace',
                    ),
                  ),
                  const Spacer(),
                  GestureDetector(
                    onTap: () => setState(() => _logs.clear()),
                    child: const Text(
                      'Clear',
                      style: TextStyle(color: Colors.white54, fontSize: 12),
                    ),
                  ),
                  const SizedBox(width: 12),
                  GestureDetector(
                    onTap: _toggleDebug,
                    child: const Icon(
                      Icons.close,
                      color: Colors.white54,
                      size: 16,
                    ),
                  ),
                ],
              ),
            ),
            Expanded(
              child: _logs.isEmpty
                  ? const Center(
                      child: Text(
                        'No logs',
                        style: TextStyle(color: Colors.white38),
                      ),
                    )
                  : ListView.builder(
                      itemCount: _logs.length,
                      padding: const EdgeInsets.symmetric(
                        horizontal: 12,
                        vertical: 4,
                      ),
                      itemBuilder: (context, index) => Text(
                        _logs[index],
                        style: const TextStyle(
                          color: Colors.white54,
                          fontSize: 11,
                          fontFamily: 'monospace',
                        ),
                      ),
                    ),
            ),
          ],
        ),
      ),
    );
  }
}

enum _EnginePhase {
  initializing,
  creating,
  opening,
  running,
  error,
}
