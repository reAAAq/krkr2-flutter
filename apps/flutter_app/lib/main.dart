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
  static const String _loading = 'Loading platform info...';

  final FlutterEngineBridge _bridge = FlutterEngineBridge();
  String _platformInfo = _loading;

  @override
  void initState() {
    super.initState();
    _loadBridgeInfo();
  }

  Future<void> _loadBridgeInfo() async {
    try {
      final String? platform = await _bridge.getPlatformVersion();
      if (!mounted) return;
      setState(() {
        _platformInfo = platform ?? 'No platform info returned from plugin.';
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _platformInfo = 'Plugin call failed: $e';
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Krkr2 Flutter Shell')),
      body: Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              const Text(
                'Flutter skeleton is ready.\nPlugin bridge status:',
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: 16),
              Text(
                _platformInfo,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.titleMedium,
              ),
              const SizedBox(height: 24),
              ElevatedButton(
                onPressed: _loadBridgeInfo,
                child: const Text('Refresh plugin call'),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
