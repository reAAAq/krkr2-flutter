import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_engine_bridge/flutter_engine_bridge.dart';

void main() {
  runApp(const FlutterShellApp());
}

class FlutterShellApp extends StatelessWidget {
  const FlutterShellApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Krkr2 Flutter Shell',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.teal),
      ),
      home: const EngineBridgeHomePage(),
    );
  }
}

class EngineBridgeHomePage extends StatefulWidget {
  const EngineBridgeHomePage({super.key});

  @override
  State<EngineBridgeHomePage> createState() => _EngineBridgeHomePageState();
}

class _EngineBridgeHomePageState extends State<EngineBridgeHomePage> {
  static const int _engineResultOk = 0;
  static const String _loading = 'Loading bridge info...';

  final FlutterEngineBridge _bridge = FlutterEngineBridge();
  final TextEditingController _gamePathController = TextEditingController(text: '.');

  Timer? _tickTimer;
  bool _busy = false;
  bool _tickInFlight = false;
  bool _isTicking = false;

  String _backendInfo = _loading;
  String _platformInfo = _loading;
  String _engineStatus = 'Not created';
  String _lastResult = 'N/A';
  String _lastError = 'Last error: <empty>';

  int _tickCount = 0;

  @override
  void initState() {
    super.initState();
    _loadBridgeInfo();
  }

  @override
  void dispose() {
    _stopTickLoop(updateStatus: false, notify: false);
    _gamePathController.dispose();
    super.dispose();
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
        _lastError = error.isEmpty ? 'Last error: <empty>' : 'Last error: $error';
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _backendInfo = 'Bridge detect failed: $e';
        _platformInfo = 'Plugin call failed: $e';
      });
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

  Future<void> _startTickLoop() async {
    if (_isTicking || _busy) return;
    if (_engineStatus != 'Opened') {
      if (!mounted) return;
      setState(() {
        _lastResult = 'engine_tick => skipped';
        _lastError = 'Last error: open game first';
      });
      return;
    }

    setState(() {
      _isTicking = true;
      _engineStatus = 'Ticking';
    });

    _tickTimer = Timer.periodic(const Duration(milliseconds: 16), (timer) async {
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
            _lastError = error.isEmpty ? 'Last error: <empty>' : 'Last error: $error';
          });
          return;
        }

        setState(() {
          _tickCount += 1;
          _lastResult = 'engine_tick => $result';
          _lastError = error.isEmpty ? 'Last error: <empty>' : 'Last error: $error';
        });
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
  }

  @override
  Widget build(BuildContext context) {
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
                Text(
                  'Platform: $_platformInfo',
                  textAlign: TextAlign.center,
                ),
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
                TextField(
                  controller: _gamePathController,
                  decoration: const InputDecoration(
                    border: OutlineInputBorder(),
                    labelText: 'Game path',
                    hintText: 'Enter game root path, e.g. .',
                  ),
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
                    FilledButton.tonal(
                      onPressed: (_busy || _isTicking) ? _stopTickLoop : _startTickLoop,
                      child: Text(_isTicking ? 'Stop Tick' : 'Start Tick'),
                    ),
                    OutlinedButton(
                      onPressed: _busy ? null : _engineDestroy,
                      child: const Text('engine_destroy'),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
