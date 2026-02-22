import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../models/game_info.dart';
import '../services/game_manager.dart';
import 'game_page.dart';

/// Engine loading mode: built-in (bundled in .app) or custom (user-specified).
enum EngineMode { builtIn, custom }

/// The home / launcher page — manage and launch games.
class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  static const String _dylibPathKey = 'krkr2_dylib_path';
  static const String _engineModeKey = 'krkr2_engine_mode';
  static const String _perfOverlayKey = 'krkr2_perf_overlay';
  static const String _targetFpsKey = 'krkr2_target_fps';
  static const String _rendererKey = 'krkr2_renderer';
  static const List<int> _fpsOptions = [30, 60, 120];
  static const int _defaultFps = 60;

  final GameManager _gameManager = GameManager();
  bool _loading = true;
  String? _iosGamesDir; // iOS: Documents/Games directory path
  EngineMode _engineMode = EngineMode.builtIn;
  String? _customDylibPath;
  String? _builtInDylibPath;
  bool _builtInAvailable = false;
  bool _perfOverlay = false;
  int _targetFps = _defaultFps;
  String _renderer = 'opengl'; // 'opengl' or 'software'

  /// Resolve the built-in engine dylib path from the app bundle.
  /// On macOS: `<executable>/../Frameworks/libengine_api.dylib`
  /// On iOS: engine is statically linked, so no dylib file needed.
  String? _resolveBuiltInDylibPath() {
    // On iOS the engine is statically linked into the app binary.
    // DynamicLibrary.process() is used at FFI level, no file path needed.
    if (Platform.isIOS) {
      return '__static_linked__';
    }
    try {
      final executablePath = Platform.resolvedExecutable;
      final execDir = File(executablePath).parent.path;
      final frameworksPath = '$execDir/../Frameworks/libengine_api.dylib';
      final resolved = File(frameworksPath);
      if (resolved.existsSync()) return resolved.path;
    } catch (_) {}
    return null;
  }

  /// The effective dylib path based on current engine mode.
  /// On iOS with built-in mode, returns null (engine is statically linked
  /// and will be loaded via DynamicLibrary.process()).
  String? get _effectiveDylibPath {
    if (_engineMode == EngineMode.builtIn) {
      // On iOS the engine is statically linked; return null so that
      // EngineFfiBridge falls back to DynamicLibrary.process().
      if (Platform.isIOS) return null;
      return _builtInDylibPath;
    }
    return _customDylibPath;
  }

  @override
  void initState() {
    super.initState();
    _loadGames();
  }

  Future<void> _loadGames() async {
    final prefs = await SharedPreferences.getInstance();
    final modeStr = prefs.getString(_engineModeKey);
    _engineMode = modeStr == 'custom' ? EngineMode.custom : EngineMode.builtIn;
    _customDylibPath = prefs.getString(_dylibPathKey);
    _builtInDylibPath = _resolveBuiltInDylibPath();
    _builtInAvailable = _builtInDylibPath != null;
    _perfOverlay = prefs.getBool(_perfOverlayKey) ?? false;
    _targetFps = prefs.getInt(_targetFpsKey) ?? _defaultFps;
    if (!_fpsOptions.contains(_targetFps)) _targetFps = _defaultFps;
    _renderer = prefs.getString(_rendererKey) ?? 'opengl';
    await _gameManager.load();

    // iOS: ensure Documents/Games directory exists and auto-scan
    if (Platform.isIOS) {
      await _initIosGamesDir();
    }

    if (mounted) setState(() => _loading = false);
  }

  /// iOS: initialize the Documents/Games directory and auto-scan for games.
  Future<void> _initIosGamesDir() async {
    final docDir = await getApplicationDocumentsDirectory();
    final gamesDir = Directory('${docDir.path}/Games');
    if (!await gamesDir.exists()) {
      await gamesDir.create(recursive: true);
    }
    _iosGamesDir = gamesDir.path;
    await _scanIosGamesDir();
  }

  /// iOS: scan Documents/Games for sub-directories and add them as games.
  /// Skips directories already in the game list (by path) and removes
  /// entries whose directories no longer exist on disk.
  Future<void> _scanIosGamesDir() async {
    if (_iosGamesDir == null) return;
    final gamesDir = Directory(_iosGamesDir!);
    if (!await gamesDir.exists()) return;

    // Collect existing game paths for quick lookup
    final existingPaths = _gameManager.games.map((g) => g.path).toSet();

    // Scan filesystem and add only new directories
    final entries = await gamesDir.list().toList();
    final scannedPaths = <String>{};
    for (final entry in entries) {
      if (entry is Directory) {
        scannedPaths.add(entry.path);
        if (!existingPaths.contains(entry.path)) {
          final game = GameInfo(path: entry.path);
          await _gameManager.addGame(game);
        }
      }
    }

    // Remove games whose directories have been deleted from disk
    final toRemove = existingPaths
        .where((p) => p.startsWith(_iosGamesDir!) && !scannedPaths.contains(p))
        .toList();
    for (final path in toRemove) {
      await _gameManager.removeGame(path);
    }
  }

  Future<void> _addGame() async {
    if (Platform.isIOS) {
      // iOS: re-scan the Documents/Games directory and show guidance
      await _scanIosGamesDir();
      if (mounted) {
        setState(() {});
        _showIosImportGuide();
      }
      return;
    }

    final String? selectedDirectory =
        await FilePicker.platform.getDirectoryPath(
      dialogTitle: 'Select Game Directory',
    );
    if (selectedDirectory == null || !mounted) return;

    final game = GameInfo(path: selectedDirectory);
    final added = await _gameManager.addGame(game);
    if (mounted) {
      if (added) {
        setState(() {});
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Game already exists: ${game.displayTitle}'),
            behavior: SnackBarBehavior.floating,
          ),
        );
      }
    }
  }

  /// iOS: show a dialog guiding users to import games via the Files app.
  void _showIosImportGuide() {
    showDialog<void>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Import Games'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'Please copy your game folders to this app\'s directory using the Files app:',
              style: TextStyle(fontSize: 14),
            ),
            const SizedBox(height: 16),
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Theme.of(ctx).colorScheme.surfaceContainerHighest,
                borderRadius: BorderRadius.circular(8),
              ),
              child: const Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('1. Open the "Files" app on your iPhone',
                      style: TextStyle(fontSize: 13)),
                  SizedBox(height: 6),
                  Text('2. Go to: On My iPhone > Krkr2 > Games',
                      style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600)),
                  SizedBox(height: 6),
                  Text('3. Copy your game folder into the Games directory',
                      style: TextStyle(fontSize: 13)),
                  SizedBox(height: 6),
                  Text('4. Come back and tap "Refresh" to detect new games',
                      style: TextStyle(fontSize: 13)),
                ],
              ),
            ),
            const SizedBox(height: 12),
            Text(
              'Games directory: Games/',
              style: TextStyle(
                fontSize: 12,
                fontFamily: 'monospace',
                color: Theme.of(ctx).colorScheme.onSurface.withValues(alpha: 0.5),
              ),
            ),
          ],
        ),
        actions: [
          FilledButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Got it'),
          ),
        ],
      ),
    );
  }

  Future<void> _removeGame(GameInfo game) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Remove Game'),
        content: Text(
          'Remove "${game.displayTitle}" from the list?\n'
          'This will NOT delete the game files.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Remove'),
          ),
        ],
      ),
    );
    if (confirmed == true && mounted) {
      await _gameManager.removeGame(game.path);
      setState(() {});
    }
  }

  Future<void> _renameGame(GameInfo game) async {
    final controller = TextEditingController(text: game.displayTitle);
    final newName = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Rename Game'),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: const InputDecoration(
            border: OutlineInputBorder(),
            labelText: 'Display Title',
          ),
          onSubmitted: (value) => Navigator.pop(ctx, value),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, controller.text),
            child: const Text('Save'),
          ),
        ],
      ),
    );
    controller.dispose();
    if (newName != null && newName.isNotEmpty && mounted) {
      await _gameManager.renameGame(game.path, newName);
      setState(() {});
    }
  }

  void _launchGame(GameInfo game) {
    final dylibPath = _effectiveDylibPath;
    // On iOS with built-in mode, dylibPath is null because the engine is
    // statically linked. This is expected — EngineFfiBridge will use
    // DynamicLibrary.process() to resolve symbols.
    final isStaticLinkedBuiltIn =
        Platform.isIOS && _engineMode == EngineMode.builtIn;
    if (dylibPath == null && !isStaticLinkedBuiltIn) {
      final msg = _engineMode == EngineMode.builtIn
          ? 'Built-in engine not found. Please use the build script to bundle the engine, or switch to Custom mode in Settings.'
          : 'Engine dylib not set. Please configure it in Settings first.';
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(msg),
          behavior: SnackBarBehavior.floating,
          action: SnackBarAction(
            label: 'Settings',
            onPressed: _showSettingsDialog,
          ),
        ),
      );
      return;
    }
    _gameManager.markPlayed(game.path);
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => GamePage(
          gamePath: game.path,
          ffiLibraryPath: dylibPath,
        ),
      ),
    );
  }

  Future<void> _showSettingsDialog() async {
    EngineMode tempMode = _engineMode;
    String? tempCustomPath = _customDylibPath;
    bool tempPerfOverlay = _perfOverlay;
    int tempTargetFps = _targetFps;
    String tempRenderer = _renderer;
    await showDialog<void>(
      context: context,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setDialogState) => AlertDialog(
          title: const Text('Settings'),
          content: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Engine Mode',
                  style: Theme.of(ctx).textTheme.titleSmall,
                ),
                const SizedBox(height: 8),
                SizedBox(
                  width: double.infinity,
                  child: SegmentedButton<EngineMode>(
                    segments: const [
                      ButtonSegment<EngineMode>(
                        value: EngineMode.builtIn,
                        label: Text('Built-in'),
                        icon: Icon(Icons.inventory_2),
                      ),
                      ButtonSegment<EngineMode>(
                        value: EngineMode.custom,
                        label: Text('Custom'),
                        icon: Icon(Icons.folder_open),
                      ),
                    ],
                    selected: {tempMode},
                    onSelectionChanged: (Set<EngineMode> selected) {
                      setDialogState(() => tempMode = selected.first);
                    },
                  ),
                ),
                const SizedBox(height: 12),
                // Built-in engine status
                if (tempMode == EngineMode.builtIn) ..._buildBuiltInSection(ctx),
                // Custom engine path
                if (tempMode == EngineMode.custom)
                  ..._buildCustomSection(ctx, tempCustomPath, (path) {
                    setDialogState(() => tempCustomPath = path);
                  }),
                const SizedBox(height: 16),
                const Divider(),
                const SizedBox(height: 8),
                Text(
                  'Render Pipeline',
                  style: Theme.of(ctx).textTheme.titleSmall,
                ),
                const SizedBox(height: 4),
                Text(
                  'Requires restarting the game to take effect',
                  style: Theme.of(ctx).textTheme.bodySmall?.copyWith(
                        color: Theme.of(ctx)
                            .colorScheme
                            .onSurface
                            .withValues(alpha: 0.6),
                      ),
                ),
                const SizedBox(height: 8),
                SizedBox(
                  width: double.infinity,
                  child: SegmentedButton<String>(
                    segments: const [
                      ButtonSegment<String>(
                        value: 'opengl',
                        label: Text('OpenGL'),
                        icon: Icon(Icons.memory),
                      ),
                      ButtonSegment<String>(
                        value: 'software',
                        label: Text('Software'),
                        icon: Icon(Icons.computer),
                      ),
                    ],
                    selected: {tempRenderer},
                    onSelectionChanged: (Set<String> selected) {
                      setDialogState(() => tempRenderer = selected.first);
                    },
                  ),
                ),
                const SizedBox(height: 16),
                const Divider(),
                const SizedBox(height: 8),
                SwitchListTile(
                  title: const Text('Performance Overlay'),
                  subtitle: const Text('Show FPS and graphics API info'),
                  value: tempPerfOverlay,
                  contentPadding: EdgeInsets.zero,
                  onChanged: (value) {
                    setDialogState(() => tempPerfOverlay = value);
                  },
                ),
                const SizedBox(height: 8),
                const Divider(),
                const SizedBox(height: 8),
                Row(
                  children: [
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            'Target Frame Rate',
                            style: Theme.of(ctx).textTheme.titleSmall,
                          ),
                          const SizedBox(height: 4),
                          Text(
                            'Limits engine tick rate',
                            style: Theme.of(ctx).textTheme.bodySmall?.copyWith(
                                  color: Theme.of(ctx)
                                      .colorScheme
                                      .onSurface
                                      .withValues(alpha: 0.6),
                                ),
                          ),
                        ],
                      ),
                    ),
                    DropdownButton<int>(
                      value: tempTargetFps,
                      items: _fpsOptions
                          .map((fps) => DropdownMenuItem<int>(
                                value: fps,
                                child: Text('$fps FPS'),
                              ))
                          .toList(),
                      onChanged: (value) {
                        if (value != null) {
                          setDialogState(() => tempTargetFps = value);
                        }
                      },
                    ),
                  ],
                ),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: const Text('Cancel'),
            ),
            FilledButton(
              onPressed: () async {
                final prefs = await SharedPreferences.getInstance();
                await prefs.setString(
                  _engineModeKey,
                  tempMode == EngineMode.custom ? 'custom' : 'builtIn',
                );
                if (tempCustomPath != null) {
                  await prefs.setString(_dylibPathKey, tempCustomPath!);
                } else {
                  await prefs.remove(_dylibPathKey);
                }
                await prefs.setBool(_perfOverlayKey, tempPerfOverlay);
                await prefs.setInt(_targetFpsKey, tempTargetFps);
                await prefs.setString(_rendererKey, tempRenderer);
                if (mounted) {
                  setState(() {
                    _engineMode = tempMode;
                    _customDylibPath = tempCustomPath;
                    _perfOverlay = tempPerfOverlay;
                    _targetFps = tempTargetFps;
                    _renderer = tempRenderer;
                  });
                }
                if (ctx.mounted) Navigator.pop(ctx);
              },
              child: const Text('Save'),
            ),
          ],
        ),
      ),
    );
  }

  /// Build the built-in engine status section for the settings dialog.
  List<Widget> _buildBuiltInSection(BuildContext ctx) {
    return [
      Container(
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: _builtInAvailable
              ? Colors.green.withValues(alpha: 0.1)
              : Theme.of(ctx).colorScheme.errorContainer.withValues(alpha: 0.3),
          borderRadius: BorderRadius.circular(8),
          border: Border.all(
            color: _builtInAvailable
                ? Colors.green.withValues(alpha: 0.3)
                : Theme.of(ctx).colorScheme.error.withValues(alpha: 0.3),
          ),
        ),
        child: Row(
          children: [
            Icon(
              _builtInAvailable ? Icons.check_circle : Icons.warning_amber,
              color: _builtInAvailable
                  ? Colors.green
                  : Theme.of(ctx).colorScheme.error,
              size: 20,
            ),
            const SizedBox(width: 10),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    _builtInAvailable
                        ? 'Built-in engine available'
                        : 'Built-in engine not found',
                    style: TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w600,
                      color: _builtInAvailable
                          ? Colors.green
                          : Theme.of(ctx).colorScheme.error,
                    ),
                  ),
                  if (!_builtInAvailable)
                    Padding(
                      padding: const EdgeInsets.only(top: 4),
                      child: Text(
                        'Use the build script to compile and bundle the engine into the app.',
                        style: TextStyle(
                          fontSize: 11,
                          color: Theme.of(ctx)
                              .colorScheme
                              .onSurface
                              .withValues(alpha: 0.6),
                        ),
                      ),
                    ),
                  if (_builtInAvailable && _builtInDylibPath != null)
                    Padding(
                      padding: const EdgeInsets.only(top: 4),
                      child: Text(
                        _builtInDylibPath!,
                        style: TextStyle(
                          fontSize: 11,
                          fontFamily: 'monospace',
                          color: Theme.of(ctx)
                              .colorScheme
                              .onSurface
                              .withValues(alpha: 0.5),
                        ),
                        maxLines: 2,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                ],
              ),
            ),
          ],
        ),
      ),
    ];
  }

  /// Build the custom dylib path section for the settings dialog.
  List<Widget> _buildCustomSection(
    BuildContext ctx,
    String? tempPath,
    ValueChanged<String?> onChanged,
  ) {
    return [
      Text(
        'Engine dylib path',
        style: Theme.of(ctx).textTheme.titleSmall,
      ),
      const SizedBox(height: 8),
      Container(
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: Theme.of(ctx).colorScheme.surfaceContainerHighest,
          borderRadius: BorderRadius.circular(8),
        ),
        child: Row(
          children: [
            Expanded(
              child: Text(
                tempPath ?? 'Not set (required)',
                style: TextStyle(
                  fontSize: 13,
                  fontFamily: 'monospace',
                  color: tempPath != null
                      ? null
                      : Theme.of(ctx)
                          .colorScheme
                          .error
                          .withValues(alpha: 0.7),
                ),
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
            ),
            if (tempPath != null)
              IconButton(
                icon: const Icon(Icons.clear, size: 18),
                tooltip: 'Clear path',
                onPressed: () => onChanged(null),
              ),
          ],
        ),
      ),
      const SizedBox(height: 12),
      SizedBox(
        width: double.infinity,
        child: OutlinedButton.icon(
          onPressed: () async {
            final result = await FilePicker.platform.pickFiles(
              dialogTitle: 'Select Engine dylib',
              type: FileType.any,
            );
            if (result != null && result.files.single.path != null) {
              onChanged(result.files.single.path);
            }
          },
          icon: const Icon(Icons.folder_open),
          label: const Text('Browse...'),
        ),
      ),
    ];
  }

  @override
  Widget build(BuildContext context) {
    final games = _gameManager.games;
    final colorScheme = Theme.of(context).colorScheme;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Krkr2 Launcher'),
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 8),
            child: Chip(
              avatar: Icon(
                _engineMode == EngineMode.builtIn
                    ? Icons.inventory_2
                    : Icons.extension,
                size: 16,
              ),
              label: Text(
                _engineMode == EngineMode.builtIn
                    ? (_builtInAvailable ? 'Built-in ✓' : 'Built-in ✗')
                    : (_customDylibPath != null
                        ? _customDylibPath!.split('/').last
                        : 'Custom (not set)'),
                style: const TextStyle(fontSize: 12),
              ),
              backgroundColor: _effectiveDylibPath != null
                  ? Colors.green.withValues(alpha: 0.15)
                  : Colors.orange.withValues(alpha: 0.15),
              visualDensity: VisualDensity.compact,
            ),
          ),
          IconButton(
            icon: const Icon(Icons.settings),
            tooltip: 'Settings',
            onPressed: _showSettingsDialog,
          ),
        ],
      ),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : games.isEmpty
              ? _buildEmptyState(colorScheme)
              : _buildGameList(games, colorScheme),
      floatingActionButton: Platform.isIOS
          ? Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                FloatingActionButton.extended(
                  heroTag: 'refresh',
                  onPressed: () async {
                    await _scanIosGamesDir();
                    if (mounted) setState(() {});
                  },
                  icon: const Icon(Icons.refresh),
                  label: const Text('Refresh'),
                ),
                const SizedBox(width: 12),
                FloatingActionButton.extended(
                  heroTag: 'guide',
                  onPressed: _showIosImportGuide,
                  icon: const Icon(Icons.help_outline),
                  label: const Text('How to Import'),
                ),
              ],
            )
          : FloatingActionButton.extended(
              onPressed: _addGame,
              icon: const Icon(Icons.add),
              label: const Text('Add Game'),
            ),
    );
  }

  Widget _buildEmptyState(ColorScheme colorScheme) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(
            Icons.videogame_asset_off,
            size: 80,
            color: colorScheme.onSurface.withValues(alpha: 0.3),
          ),
          const SizedBox(height: 24),
          Text(
            'No games added yet',
            style: TextStyle(
              fontSize: 20,
              color: colorScheme.onSurface.withValues(alpha: 0.6),
            ),
          ),
          const SizedBox(height: 8),
          Text(
            Platform.isIOS
                ? 'Use the Files app to copy game folders to:\nOn My iPhone > Krkr2 > Games\nThen tap "Refresh"'
                : 'Click "Add Game" to select a game directory',
            textAlign: TextAlign.center,
            style: TextStyle(
              fontSize: 14,
              color: colorScheme.onSurface.withValues(alpha: 0.4),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildGameList(List<GameInfo> games, ColorScheme colorScheme) {
    // Sort by last played (most recent first), then by title
    final sorted = List<GameInfo>.from(games)
      ..sort((a, b) {
        if (a.lastPlayed != null && b.lastPlayed != null) {
          return b.lastPlayed!.compareTo(a.lastPlayed!);
        }
        if (a.lastPlayed != null) return -1;
        if (b.lastPlayed != null) return 1;
        return a.displayTitle.compareTo(b.displayTitle);
      });

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 800),
          child: ListView.builder(
            itemCount: sorted.length,
            itemBuilder: (context, index) {
              final game = sorted[index];
              return _GameCard(
                game: game,
                onTap: () => _launchGame(game),
                onRename: () => _renameGame(game),
                onRemove: () => _removeGame(game),
              );
            },
          ),
        ),
      ),
    );
  }
}

class _GameCard extends StatelessWidget {
  const _GameCard({
    required this.game,
    required this.onTap,
    required this.onRename,
    required this.onRemove,
  });

  final GameInfo game;
  final VoidCallback onTap;
  final VoidCallback onRename;
  final VoidCallback onRemove;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final lastPlayed = game.lastPlayed;
    final String subtitle = lastPlayed != null
        ? '${game.path}\nLast played: ${_formatDate(lastPlayed)}'
        : game.path;

    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      child: InkWell(
        borderRadius: BorderRadius.circular(12),
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              Container(
                width: 56,
                height: 56,
                decoration: BoxDecoration(
                  color: colorScheme.primaryContainer,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Icon(
                  Icons.videogame_asset,
                  color: colorScheme.onPrimaryContainer,
                  size: 28,
                ),
              ),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      game.displayTitle,
                      style: Theme.of(context).textTheme.titleMedium?.copyWith(
                            fontWeight: FontWeight.w600,
                          ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      subtitle,
                      style: Theme.of(context).textTheme.bodySmall?.copyWith(
                            color:
                                colorScheme.onSurface.withValues(alpha: 0.6),
                          ),
                      maxLines: 2,
                      overflow: TextOverflow.ellipsis,
                    ),
                  ],
                ),
              ),
              PopupMenuButton<String>(
                onSelected: (value) {
                  switch (value) {
                    case 'rename':
                      onRename();
                      break;
                    case 'remove':
                      onRemove();
                      break;
                  }
                },
                itemBuilder: (context) => [
                  const PopupMenuItem(
                    value: 'rename',
                    child: ListTile(
                      leading: Icon(Icons.edit),
                      title: Text('Rename'),
                      contentPadding: EdgeInsets.zero,
                      dense: true,
                    ),
                  ),
                  const PopupMenuItem(
                    value: 'remove',
                    child: ListTile(
                      leading: Icon(Icons.delete, color: Colors.redAccent),
                      title:
                          Text('Remove', style: TextStyle(color: Colors.redAccent)),
                      contentPadding: EdgeInsets.zero,
                      dense: true,
                    ),
                  ),
                ],
              ),
              const SizedBox(width: 4),
              Icon(
                Icons.play_circle_fill,
                color: colorScheme.primary,
                size: 36,
              ),
            ],
          ),
        ),
      ),
    );
  }

  String _formatDate(DateTime date) {
    final now = DateTime.now();
    final diff = now.difference(date);
    if (diff.inMinutes < 1) return 'just now';
    if (diff.inHours < 1) return '${diff.inMinutes} min ago';
    if (diff.inDays < 1) return '${diff.inHours} hours ago';
    if (diff.inDays < 7) return '${diff.inDays} days ago';
    return '${date.year}-${date.month.toString().padLeft(2, '0')}-${date.day.toString().padLeft(2, '0')}';
  }
}
