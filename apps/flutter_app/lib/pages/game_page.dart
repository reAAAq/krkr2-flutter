import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import '../engine/engine_bridge.dart';
import '../engine/flutter_engine_bridge_adapter.dart';
import '../widgets/engine_surface.dart';

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
    with WidgetsBindingObserver, SingleTickerProviderStateMixin {
  static const int _engineResultOk = 0;

  late EngineBridge _bridge;
  final GlobalKey<EngineSurfaceState> _surfaceKey =
      GlobalKey<EngineSurfaceState>();

  Ticker? _ticker;
  bool _tickInFlight = false;
  bool _isTicking = false;
  bool _autoPausedByLifecycle = false;
  bool _resumeTickAfterLifecycle = false;
  bool _lifecycleTransitionInFlight = false;

  // State
  _EnginePhase _phase = _EnginePhase.initializing;
  String? _errorMessage;
  bool _showOverlay = false;
  bool _showDebug = false;
  int _tickCount = 0;
  final List<String> _logs = [];
  static const int _maxLogs = 80;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _bridge = widget.engineBridgeBuilder(ffiLibraryPath: widget.ffiLibraryPath);
    _log('Initializing engine for: ${widget.gamePath}');
    if (widget.ffiLibraryPath != null) {
      _log('Using custom dylib: ${widget.ffiLibraryPath}');
    }
    unawaited(_autoStart());
  }

  @override
  void dispose() {
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
      case AppLifecycleState.inactive:
      case AppLifecycleState.hidden:
      case AppLifecycleState.paused:
        unawaited(_pauseForLifecycle());
        break;
      case AppLifecycleState.detached:
        break;
    }
  }

  // --- Auto-start flow: create → open → tick ---

  Future<void> _autoStart() async {
    setState(() => _phase = _EnginePhase.creating);
    _log('engine_create...');

    final int createResult = await _bridge.engineCreate();
    if (createResult != _engineResultOk) {
      _fail('engine_create failed: result=$createResult, '
          'error=${_bridge.engineGetLastError()}');
      return;
    }
    _log('engine_create => OK');

    if (!mounted) return;
    setState(() => _phase = _EnginePhase.opening);
    _log('engine_open_game(${widget.gamePath})...');

    final int openResult = await _bridge.engineOpenGame(widget.gamePath);
    if (openResult != _engineResultOk) {
      _fail('engine_open_game failed: result=$openResult, '
          'error=${_bridge.engineGetLastError()}');
      return;
    }
    _log('engine_open_game => OK');

    if (!mounted) return;
    setState(() => _phase = _EnginePhase.running);
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
    _ticker = createTicker((Duration elapsed) async {
      if (_tickInFlight) return;

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
    if (!mounted ||
        _lifecycleTransitionInFlight ||
        _autoPausedByLifecycle ||
        _phase != _EnginePhase.running) {
      return;
    }
    _lifecycleTransitionInFlight = true;
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
  }

  Future<void> _resumeForLifecycle() async {
    if (!mounted ||
        _lifecycleTransitionInFlight ||
        !_autoPausedByLifecycle) {
      return;
    }
    _lifecycleTransitionInFlight = true;

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

  // --- Logging ---

  void _log(String message) {
    final now = DateTime.now();
    final time =
        '${now.hour.toString().padLeft(2, '0')}:${now.minute.toString().padLeft(2, '0')}:${now.second.toString().padLeft(2, '0')}';
    _logs.insert(0, '[$time] $message');
    if (_logs.length > _maxLogs) {
      _logs.removeRange(_maxLogs, _logs.length);
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
                    : _buildLoadingView(),
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

  Widget _buildLoadingView() {
    String message;
    switch (_phase) {
      case _EnginePhase.initializing:
        message = 'Initializing...';
        break;
      case _EnginePhase.creating:
        message = 'Creating engine...';
        break;
      case _EnginePhase.opening:
        message = 'Opening game...';
        break;
      default:
        message = 'Loading...';
    }
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const CircularProgressIndicator(color: Colors.white70),
          const SizedBox(height: 24),
          Text(
            message,
            style: const TextStyle(color: Colors.white70, fontSize: 16),
          ),
          const SizedBox(height: 8),
          Text(
            widget.gamePath,
            style: const TextStyle(color: Colors.white38, fontSize: 13),
          ),
        ],
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
