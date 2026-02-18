import 'dart:async';

import 'package:flutter/material.dart';

import 'engine/engine_bridge.dart';
import 'engine/flutter_engine_bridge_adapter.dart';
import 'widgets/engine_surface.dart';

void main() {
  runApp(const FlutterShellApp());
}

class FlutterShellApp extends StatelessWidget {
  const FlutterShellApp({
    super.key,
    this.engineBridgeBuilder = createEngineBridge,
  });

  final EngineBridgeBuilder engineBridgeBuilder;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Krkr2 Flutter Shell',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.teal),
      ),
      home: EngineBridgeHomePage(engineBridgeBuilder: engineBridgeBuilder),
    );
  }
}

class EngineBridgeHomePage extends StatefulWidget {
  const EngineBridgeHomePage({
    super.key,
    this.engineBridgeBuilder = createEngineBridge,
  });

  final EngineBridgeBuilder engineBridgeBuilder;

  @override
  State<EngineBridgeHomePage> createState() => _EngineBridgeHomePageState();
}

class _EngineBridgeHomePageState extends State<EngineBridgeHomePage> {
  static const int _engineResultOk = 0;
  static const int _maxEventLogSize = 120;
  static const int _smokeTickIterations = 3;
  static const String _loading = 'Loading bridge info...';
  static const String _defaultEngineLibraryPath = String.fromEnvironment(
    'KRKR2_ENGINE_LIB',
    defaultValue: '',
  );

  late EngineBridge _bridge;
  final TextEditingController _engineLibraryPathController =
      TextEditingController(text: _defaultEngineLibraryPath);
  final TextEditingController _gamePathController = TextEditingController(
    text: '.',
  );
  final TextEditingController _optionKeyController = TextEditingController(
    text: 'fps_limit',
  );
  final TextEditingController _optionValueController = TextEditingController(
    text: '60',
  );

  Timer? _tickTimer;
  bool _busy = false;
  bool _tickInFlight = false;
  bool _isTicking = false;

  String _backendInfo = _loading;
  String _platformInfo = _loading;
  String _engineStatus = 'Not created';
  String _lastResult = 'N/A';
  String _lastError = 'Last error: <empty>';
  String _libraryPathInfo = 'Engine library path: <auto>';
  String _ffiInitInfo = 'FFI init: <none>';
  final List<String> _eventLogs = <String>[];

  int _tickCount = 0;

  @override
  void initState() {
    super.initState();
    _recreateBridge();
    _appendLog('App started');
    _loadBridgeInfo();
  }

  @override
  void dispose() {
    _stopTickLoop(updateStatus: false, notify: false);
    _engineLibraryPathController.dispose();
    _gamePathController.dispose();
    _optionKeyController.dispose();
    _optionValueController.dispose();
    super.dispose();
  }

  String? _currentEngineLibraryPathOrNull() {
    final String value = _engineLibraryPathController.text.trim();
    return value.isEmpty ? null : value;
  }

  void _recreateBridge() {
    final String? path = _currentEngineLibraryPathOrNull();
    _bridge = widget.engineBridgeBuilder(ffiLibraryPath: path);
    _libraryPathInfo = 'Engine library path: ${path ?? "<auto>"}';
    final String ffiInitError = _bridge.ffiInitializationError;
    _ffiInitInfo = ffiInitError.isEmpty
        ? 'FFI init: <none>'
        : 'FFI init: $ffiInitError';
  }

  Future<void> _applyBridgePath() async {
    if (_busy) return;
    if (_isTicking) {
      _stopTickLoop(updateStatus: false);
    }
    if (mounted) {
      setState(() {
        _recreateBridge();
        _lastResult = 'bridge_reload => done';
      });
    } else {
      _recreateBridge();
    }
    _appendLog(
      _libraryPathInfo.replaceFirst(
        'Engine library path: ',
        'Bridge reloaded with path: ',
      ),
    );
    await _loadBridgeInfo();
  }

  Future<void> _loadBridgeInfo() async {
    _setBusy(true);
    try {
      final String backend = await _bridge.getBackendDescription();
      final String? platform = await _bridge.getPlatformVersion();
      final String error = _bridge.engineGetLastError();
      if (!mounted) return;
      setState(() {
        _backendInfo = backend;
        _platformInfo = platform ?? 'No platform info returned from plugin.';
        _lastError = error.isEmpty
            ? 'Last error: <empty>'
            : 'Last error: $error';
        final String ffiInitError = _bridge.ffiInitializationError;
        _ffiInitInfo = ffiInitError.isEmpty
            ? 'FFI init: <none>'
            : 'FFI init: $ffiInitError';
      });
      _appendLog(
        'Bridge ready: backend=$backend, platform=$_platformInfo, ffi=${_bridge.isFfiAvailable}',
      );
      if (_bridge.ffiInitializationError.isNotEmpty) {
        _appendLog(
          'FFI initialization error: ${_bridge.ffiInitializationError}',
        );
      }
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _backendInfo = 'Bridge detect failed: $e';
        _platformInfo = 'Plugin call failed: $e';
      });
      _appendLog('Bridge detect failed: $e');
    } finally {
      _setBusy(false);
    }
  }

  Future<void> _engineCreate() async {
    _setBusy(true);
    try {
      final int result = await _bridge.engineCreate();
      _syncResult(
        apiName: 'engine_create',
        result: result,
        successStatus: 'Created',
        failureStatus: 'Create failed',
      );
      if (result == _engineResultOk && mounted) {
        setState(() {
          _tickCount = 0;
        });
      }
    } finally {
      _setBusy(false);
    }
  }

  Future<void> _engineOpenGame() async {
    final String gamePath = _gamePathController.text.trim();
    if (gamePath.isEmpty) {
      if (!mounted) return;
      setState(() {
        _lastResult = 'engine_open_game => skipped';
        _lastError = 'Last error: game path is empty';
        _engineStatus = 'Open failed';
      });
      _appendLog('engine_open_game skipped: game path is empty');
      return;
    }

    _setBusy(true);
    try {
      final int result = await _bridge.engineOpenGame(gamePath);
      _syncResult(
        apiName: 'engine_open_game',
        result: result,
        successStatus: 'Opened',
        failureStatus: 'Open failed',
      );
    } finally {
      _setBusy(false);
    }
  }

  Future<void> _engineDestroy() async {
    _stopTickLoop();
    _setBusy(true);
    try {
      final int result = await _bridge.engineDestroy();
      _syncResult(
        apiName: 'engine_destroy',
        result: result,
        successStatus: 'Not created',
        failureStatus: 'Destroy failed',
      );
      if (result == _engineResultOk && mounted) {
        setState(() {
          _tickCount = 0;
        });
      }
    } finally {
      _setBusy(false);
    }
  }

  Future<void> _enginePause() async {
    if (_busy) return;
    _setBusy(true);
    try {
      final int result = await _bridge.enginePause();
      _syncResult(
        apiName: 'engine_pause',
        result: result,
        successStatus: 'Paused',
        failureStatus: 'Pause failed',
      );
      if (result == _engineResultOk) {
        _stopTickLoop(updateStatus: false);
      }
    } finally {
      _setBusy(false);
    }
  }

  Future<void> _engineResume() async {
    if (_busy) return;
    _setBusy(true);
    try {
      final int result = await _bridge.engineResume();
      _syncResult(
        apiName: 'engine_resume',
        result: result,
        successStatus: 'Opened',
        failureStatus: 'Resume failed',
      );
    } finally {
      _setBusy(false);
    }
  }

  Future<void> _engineSetOption() async {
    final String key = _optionKeyController.text.trim();
    final String value = _optionValueController.text.trim();
    if (key.isEmpty) {
      _appendLog('engine_set_option skipped: key is empty');
      if (!mounted) return;
      setState(() {
        _lastResult = 'engine_set_option => skipped';
        _lastError = 'Last error: option key is empty';
      });
      return;
    }

    _setBusy(true);
    try {
      final int result = await _bridge.engineSetOption(key: key, value: value);
      final String error = _bridge.engineGetLastError();
      if (!mounted) return;
      setState(() {
        _lastResult = 'engine_set_option($key=$value) => $result';
        _lastError = error.isEmpty
            ? 'Last error: <empty>'
            : 'Last error: $error';
      });
      _appendLog(
        'engine_set_option($key=$value) => $result, '
        'error=${error.isEmpty ? "<empty>" : error}',
      );
    } finally {
      _setBusy(false);
    }
  }

  Future<void> _runOpenTickSmoke() async {
    if (_busy || _isTicking) return;

    final String gamePath = _gamePathController.text.trim();
    if (gamePath.isEmpty) {
      if (!mounted) return;
      setState(() {
        _lastResult = 'open_tick_smoke => skipped';
        _lastError = 'Last error: game path is empty';
        _engineStatus = 'Open+Tick failed';
      });
      _appendLog('open_tick_smoke skipped: game path is empty');
      return;
    }

    _setBusy(true);
    _appendLog('open_tick_smoke started: path=$gamePath');
    try {
      final int createResult = await _bridge.engineCreate();
      if (createResult != _engineResultOk) {
        final String error = _bridge.engineGetLastError();
        if (!mounted) return;
        setState(() {
          _lastResult = 'open_tick_smoke/create => $createResult';
          _lastError = error.isEmpty
              ? 'Last error: <empty>'
              : 'Last error: $error';
          _engineStatus = 'Open+Tick failed';
        });
        _appendLog(
          'open_tick_smoke create failed: result=$createResult, '
          'error=${error.isEmpty ? "<empty>" : error}',
        );
        return;
      }

      final int openResult = await _bridge.engineOpenGame(gamePath);
      if (openResult != _engineResultOk) {
        final String error = _bridge.engineGetLastError();
        if (!mounted) return;
        setState(() {
          _lastResult = 'open_tick_smoke/open => $openResult';
          _lastError = error.isEmpty
              ? 'Last error: <empty>'
              : 'Last error: $error';
          _engineStatus = 'Open+Tick failed';
        });
        _appendLog(
          'open_tick_smoke open failed: result=$openResult, '
          'error=${error.isEmpty ? "<empty>" : error}',
        );
        return;
      }

      for (int i = 0; i < _smokeTickIterations; i++) {
        final int tickResult = await _bridge.engineTick(deltaMs: 16);
        if (tickResult != _engineResultOk) {
          final String error = _bridge.engineGetLastError();
          if (!mounted) return;
          setState(() {
            _lastResult = 'open_tick_smoke/tick#$i => $tickResult';
            _lastError = error.isEmpty
                ? 'Last error: <empty>'
                : 'Last error: $error';
            _engineStatus = 'Open+Tick failed';
          });
          _appendLog(
            'open_tick_smoke tick failed: step=$i, result=$tickResult, '
            'error=${error.isEmpty ? "<empty>" : error}',
          );
          return;
        }
      }

      final String error = _bridge.engineGetLastError();
      if (!mounted) return;
      setState(() {
        _tickCount += _smokeTickIterations;
        _lastResult = 'open_tick_smoke => 0';
        _lastError = error.isEmpty
            ? 'Last error: <empty>'
            : 'Last error: $error';
        _engineStatus = 'Opened';
      });
      _appendLog('open_tick_smoke passed: ticks=$_smokeTickIterations');
    } finally {
      _setBusy(false);
    }
  }

  Future<void> _startTickLoop() async {
    if (_isTicking || _busy) return;
    if (_engineStatus != 'Opened') {
      if (!mounted) return;
      setState(() {
        _lastResult = 'engine_tick => skipped';
        _lastError = 'Last error: open game first';
      });
      _appendLog('Start Tick blocked: open game first');
      return;
    }

    setState(() {
      _isTicking = true;
      _engineStatus = 'Ticking';
    });
    _appendLog('Tick loop started (16ms interval)');

    _tickTimer = Timer.periodic(const Duration(milliseconds: 16), (
      timer,
    ) async {
      if (_tickInFlight) {
        return;
      }
      _tickInFlight = true;
      try {
        final int result = await _bridge.engineTick(deltaMs: 16);
        if (!mounted) return;
        final String error = _bridge.engineGetLastError();
        if (result != _engineResultOk) {
          timer.cancel();
          setState(() {
            _isTicking = false;
            _engineStatus = 'Faulted';
            _lastResult = 'engine_tick => $result';
            _lastError = error.isEmpty
                ? 'Last error: <empty>'
                : 'Last error: $error';
          });
          _appendLog(
            'Tick faulted: result=$result, error=${error.isEmpty ? "<empty>" : error}',
          );
          return;
        }

        setState(() {
          _tickCount += 1;
          _lastResult = 'engine_tick => $result';
          _lastError = error.isEmpty
              ? 'Last error: <empty>'
              : 'Last error: $error';
        });
        if (_tickCount % 120 == 0) {
          _appendLog('Tick alive: count=$_tickCount');
        }
      } finally {
        _tickInFlight = false;
      }
    });
  }

  void _stopTickLoop({bool updateStatus = true, bool notify = true}) {
    _tickTimer?.cancel();
    _tickTimer = null;

    void updateFields() {
      _isTicking = false;
      if (updateStatus && _engineStatus == 'Ticking') {
        _engineStatus = 'Opened';
      }
      if (updateStatus) {
        _lastResult = 'tick loop stopped';
      }
    }

    if (notify && mounted) {
      setState(updateFields);
      return;
    }
    updateFields();
    if (updateStatus) {
      _appendLog('Tick loop stopped');
    }
  }

  void _setBusy(bool value) {
    if (!mounted) return;
    setState(() {
      _busy = value;
    });
  }

  void _syncResult({
    required String apiName,
    required int result,
    required String successStatus,
    required String failureStatus,
  }) {
    final String error = _bridge.engineGetLastError();
    if (!mounted) return;
    setState(() {
      _lastResult = '$apiName => $result';
      _engineStatus = result == _engineResultOk ? successStatus : failureStatus;
      _lastError = error.isEmpty ? 'Last error: <empty>' : 'Last error: $error';
    });
    _appendLog(
      '$apiName => $result, status=$_engineStatus, error=${error.isEmpty ? "<empty>" : error}',
    );
  }

  void _clearLogs() {
    if (!mounted) return;
    setState(() {
      _eventLogs.clear();
    });
    _appendLog('Logs cleared');
  }

  void _appendLog(String message, {bool notify = true}) {
    final DateTime now = DateTime.now();
    final String time =
        '${now.hour.toString().padLeft(2, '0')}:${now.minute.toString().padLeft(2, '0')}:${now.second.toString().padLeft(2, '0')}';
    final String line = '[$time] $message';
    void updateLogs() {
      _eventLogs.insert(0, line);
      if (_eventLogs.length > _maxEventLogSize) {
        _eventLogs.removeRange(_maxEventLogSize, _eventLogs.length);
      }
    }

    if (notify && mounted) {
      setState(updateLogs);
      return;
    }
    updateLogs();
  }

  @override
  Widget build(BuildContext context) {
    final bool isSurfaceActive =
        _engineStatus == 'Opened' ||
        _engineStatus == 'Ticking' ||
        _engineStatus == 'Paused';

    return Scaffold(
      appBar: AppBar(title: const Text('Krkr2 Flutter Shell')),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(24),
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 680),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                const Text(
                  'Flutter shell ready.\nEngine bridge status:',
                  textAlign: TextAlign.center,
                ),
                const SizedBox(height: 16),
                Text(
                  'Backend: $_backendInfo',
                  textAlign: TextAlign.center,
                  style: Theme.of(context).textTheme.titleMedium,
                ),
                const SizedBox(height: 8),
                Text('Platform: $_platformInfo', textAlign: TextAlign.center),
                const SizedBox(height: 8),
                Text(_libraryPathInfo, textAlign: TextAlign.center),
                const SizedBox(height: 8),
                Text(_ffiInitInfo, textAlign: TextAlign.center),
                const SizedBox(height: 16),
                Text(
                  'Engine status: $_engineStatus',
                  textAlign: TextAlign.center,
                  style: Theme.of(context).textTheme.titleMedium,
                ),
                const SizedBox(height: 8),
                Text('Tick count: $_tickCount', textAlign: TextAlign.center),
                const SizedBox(height: 8),
                Text('Last result: $_lastResult', textAlign: TextAlign.center),
                const SizedBox(height: 8),
                Text(_lastError, textAlign: TextAlign.center),
                const SizedBox(height: 20),
                EngineSurface(
                  bridge: _bridge,
                  active: isSurfaceActive,
                  onLog: (String message) {
                    _appendLog('surface: $message');
                  },
                  onError: (String message) {
                    if (!mounted) return;
                    setState(() {
                      _lastError = 'Last error: $message';
                    });
                    _appendLog('surface error: $message');
                  },
                ),
                const SizedBox(height: 8),
                const Text(
                  'Tip: Click the surface to focus, then keyboard input will be forwarded.',
                  textAlign: TextAlign.center,
                ),
                const SizedBox(height: 20),
                TextField(
                  controller: _engineLibraryPathController,
                  decoration: const InputDecoration(
                    border: OutlineInputBorder(),
                    labelText: 'Engine library path (optional)',
                    hintText: '/tmp/engine_api_build/libengine_api.dylib',
                  ),
                ),
                const SizedBox(height: 8),
                Align(
                  alignment: Alignment.centerRight,
                  child: OutlinedButton(
                    onPressed: (_busy || _isTicking) ? null : _applyBridgePath,
                    child: const Text('Apply bridge path'),
                  ),
                ),
                const SizedBox(height: 8),
                TextField(
                  controller: _gamePathController,
                  decoration: const InputDecoration(
                    border: OutlineInputBorder(),
                    labelText: 'Game path',
                    hintText: 'Enter game root path, e.g. .',
                  ),
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    Expanded(
                      flex: 2,
                      child: TextField(
                        controller: _optionKeyController,
                        decoration: const InputDecoration(
                          border: OutlineInputBorder(),
                          labelText: 'Option key',
                          hintText: 'fps_limit',
                        ),
                      ),
                    ),
                    const SizedBox(width: 8),
                    Expanded(
                      child: TextField(
                        controller: _optionValueController,
                        decoration: const InputDecoration(
                          border: OutlineInputBorder(),
                          labelText: 'Option value',
                          hintText: '60',
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                if (_busy) const Center(child: CircularProgressIndicator()),
                if (_busy) const SizedBox(height: 16),
                Wrap(
                  spacing: 12,
                  runSpacing: 12,
                  alignment: WrapAlignment.center,
                  children: [
                    ElevatedButton(
                      onPressed: _busy ? null : _loadBridgeInfo,
                      child: const Text('Refresh bridge info'),
                    ),
                    FilledButton(
                      onPressed: (_busy || _isTicking) ? null : _engineCreate,
                      child: const Text('engine_create'),
                    ),
                    OutlinedButton(
                      onPressed: (_busy || _isTicking) ? null : _engineOpenGame,
                      child: const Text('engine_open_game'),
                    ),
                    ElevatedButton(
                      onPressed: (_busy || _isTicking)
                          ? null
                          : _runOpenTickSmoke,
                      child: const Text('Run Open+Tick Smoke'),
                    ),
                    FilledButton.tonal(
                      onPressed: (_busy || _isTicking)
                          ? _stopTickLoop
                          : _startTickLoop,
                      child: Text(_isTicking ? 'Stop Tick' : 'Start Tick'),
                    ),
                    OutlinedButton(
                      onPressed: _busy ? null : _engineDestroy,
                      child: const Text('engine_destroy'),
                    ),
                    OutlinedButton(
                      onPressed: (_busy || _isTicking) ? null : _enginePause,
                      child: const Text('engine_pause'),
                    ),
                    OutlinedButton(
                      onPressed: (_busy || _isTicking) ? null : _engineResume,
                      child: const Text('engine_resume'),
                    ),
                    ElevatedButton(
                      onPressed: (_busy || _isTicking)
                          ? null
                          : _engineSetOption,
                      child: const Text('engine_set_option'),
                    ),
                  ],
                ),
                const SizedBox(height: 20),
                const Divider(),
                const SizedBox(height: 8),
                Row(
                  children: [
                    Text(
                      'Event logs',
                      style: Theme.of(context).textTheme.titleMedium,
                    ),
                    const Spacer(),
                    TextButton(
                      onPressed: _eventLogs.isEmpty ? null : _clearLogs,
                      child: const Text('Clear logs'),
                    ),
                  ],
                ),
                const SizedBox(height: 8),
                Container(
                  height: 220,
                  decoration: BoxDecoration(
                    border: Border.all(color: Theme.of(context).dividerColor),
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: _eventLogs.isEmpty
                      ? const Center(child: Text('No logs yet'))
                      : ListView.builder(
                          itemCount: _eventLogs.length,
                          itemBuilder: (context, index) {
                            return Padding(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 12,
                                vertical: 6,
                              ),
                              child: Text(
                                _eventLogs[index],
                                style: Theme.of(context).textTheme.bodySmall,
                              ),
                            );
                          },
                        ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
