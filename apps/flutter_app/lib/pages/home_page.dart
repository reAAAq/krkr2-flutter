import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:image_picker/image_picker.dart';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../l10n/app_localizations.dart';
import '../models/game_info.dart';
import '../services/game_manager.dart';
import 'game_page.dart';
import 'settings_page.dart';

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
  String? _iosGamesDir;
  EngineMode _engineMode = EngineMode.builtIn;
  String? _customDylibPath;
  String? _builtInDylibPath;
  bool _builtInAvailable = false;
  bool _perfOverlay = false;
  int _targetFps = _defaultFps;
  String _renderer = 'opengl';

  String? _resolveBuiltInDylibPath() {
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

  String? get _effectiveDylibPath {
    if (_engineMode == EngineMode.builtIn) {
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

    if (Platform.isIOS) {
      await _initIosGamesDir();
    }

    if (mounted) setState(() => _loading = false);
  }

  Future<void> _initIosGamesDir() async {
    final docDir = await getApplicationDocumentsDirectory();
    final gamesDir = Directory('${docDir.path}/Games');
    if (!await gamesDir.exists()) {
      await gamesDir.create(recursive: true);
    }
    _iosGamesDir = gamesDir.path;
    await _scanIosGamesDir();
  }

  Future<void> _scanIosGamesDir() async {
    if (_iosGamesDir == null) return;
    final gamesDir = Directory(_iosGamesDir!);
    if (!await gamesDir.exists()) return;

    final existingPaths = _gameManager.games.map((g) => g.path).toSet();
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

    final toRemove = existingPaths
        .where((p) => p.startsWith(_iosGamesDir!) && !scannedPaths.contains(p))
        .toList();
    for (final path in toRemove) {
      await _gameManager.removeGame(path);
    }
  }

  Future<void> _addGame() async {
    final l10n = AppLocalizations.of(context)!;
    if (Platform.isIOS) {
      await _scanIosGamesDir();
      if (mounted) {
        setState(() {});
        _showIosImportGuide();
      }
      return;
    }

    final String? selectedDirectory =
        await FilePicker.platform.getDirectoryPath(
      dialogTitle: l10n.selectGameDirectory,
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
            content: Text(l10n.gameAlreadyExists(game.displayTitle)),
            behavior: SnackBarBehavior.floating,
          ),
        );
      }
    }
  }

  void _showIosImportGuide() {
    final l10n = AppLocalizations.of(context)!;
    showDialog<void>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(l10n.importGames),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(l10n.importGamesDesc, style: const TextStyle(fontSize: 14)),
            const SizedBox(height: 16),
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Theme.of(ctx).colorScheme.surfaceContainerHighest,
                borderRadius: BorderRadius.circular(8),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(l10n.importStep1, style: const TextStyle(fontSize: 13)),
                  const SizedBox(height: 6),
                  Text(l10n.importStep2,
                      style: const TextStyle(
                          fontSize: 13, fontWeight: FontWeight.w600)),
                  const SizedBox(height: 6),
                  Text(l10n.importStep3,
                      style: const TextStyle(fontSize: 13)),
                  const SizedBox(height: 6),
                  Text(l10n.importStep4,
                      style: const TextStyle(fontSize: 13)),
                ],
              ),
            ),
            const SizedBox(height: 12),
            Text(
              l10n.gamesDirectory,
              style: TextStyle(
                fontSize: 12,
                fontFamily: 'monospace',
                color: Theme.of(ctx)
                    .colorScheme
                    .onSurface
                    .withValues(alpha: 0.5),
              ),
            ),
          ],
        ),
        actions: [
          FilledButton(
            onPressed: () => Navigator.pop(ctx),
            child: Text(l10n.gotIt),
          ),
        ],
      ),
    );
  }

  Future<void> _removeGame(GameInfo game) async {
    final l10n = AppLocalizations.of(context)!;
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(l10n.removeGame),
        content: Text(l10n.removeGameConfirm(game.displayTitle)),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: Text(l10n.cancel),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: Text(l10n.remove),
          ),
        ],
      ),
    );
    if (confirmed == true && mounted) {
      await _gameManager.removeGame(game.path);
      setState(() {});
    }
  }

  Future<void> _setCoverImage(GameInfo game) async {
    final l10n = AppLocalizations.of(context)!;
    final source = await showModalBottomSheet<String>(
      context: context,
      builder: (ctx) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              leading: const Icon(Icons.photo_library),
              title: Text(l10n.coverFromGallery),
              onTap: () => Navigator.pop(ctx, 'gallery'),
            ),
            ListTile(
              leading: const Icon(Icons.camera_alt),
              title: Text(l10n.coverFromCamera),
              onTap: () => Navigator.pop(ctx, 'camera'),
            ),
            if (game.coverPath != null)
              ListTile(
                leading: const Icon(Icons.delete_outline, color: Colors.redAccent),
                title: Text(l10n.coverRemove, style: const TextStyle(color: Colors.redAccent)),
                onTap: () => Navigator.pop(ctx, 'remove'),
              ),
          ],
        ),
      ),
    );
    if (source == null || !mounted) return;

    if (source == 'remove') {
      await _gameManager.setCoverImage(game.path, null);
      if (mounted) setState(() {});
      return;
    }

    final picker = ImagePicker();
    final XFile? image = await picker.pickImage(
      source: source == 'camera' ? ImageSource.camera : ImageSource.gallery,
      maxWidth: 512,
      maxHeight: 512,
      imageQuality: 85,
    );
    if (image == null || !mounted) return;

    // Copy image to app's persistent storage
    final docDir = await getApplicationDocumentsDirectory();
    final coversDir = Directory('${docDir.path}/covers');
    if (!await coversDir.exists()) {
      await coversDir.create(recursive: true);
    }
    final ext = image.path.split('.').last;
    final fileName = '${game.path.hashCode}_${DateTime.now().millisecondsSinceEpoch}.$ext';
    final destPath = '${coversDir.path}/$fileName';
    await File(image.path).copy(destPath);

    // Remove old cover file if exists
    if (game.coverPath != null) {
      try {
        final oldFile = File(game.coverPath!);
        if (await oldFile.exists()) await oldFile.delete();
      } catch (_) {}
    }

    await _gameManager.setCoverImage(game.path, destPath);
    if (mounted) setState(() {});
  }

  Future<void> _renameGame(GameInfo game) async {
    final l10n = AppLocalizations.of(context)!;
    final controller = TextEditingController(text: game.displayTitle);
    final newName = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(l10n.renameGame),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: InputDecoration(
            border: const OutlineInputBorder(),
            labelText: l10n.displayTitle,
          ),
          onSubmitted: (value) => Navigator.pop(ctx, value),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: Text(l10n.cancel),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, controller.text),
            child: Text(l10n.save),
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
    final l10n = AppLocalizations.of(context)!;
    final dylibPath = _effectiveDylibPath;
    final isStaticLinkedBuiltIn =
        Platform.isIOS && _engineMode == EngineMode.builtIn;
    if (dylibPath == null && !isStaticLinkedBuiltIn) {
      final msg = _engineMode == EngineMode.builtIn
          ? l10n.engineNotFoundBuiltIn
          : l10n.engineNotFoundCustom;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(msg),
          behavior: SnackBarBehavior.floating,
          action: SnackBarAction(
            label: l10n.settings,
            onPressed: _openSettings,
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

  Future<void> _openSettings() async {
    final result = await Navigator.of(context).push<SettingsResult>(
      MaterialPageRoute<SettingsResult>(
        builder: (_) => SettingsPage(
          engineMode: _engineMode,
          customDylibPath: _customDylibPath,
          builtInDylibPath: _builtInDylibPath,
          builtInAvailable: _builtInAvailable,
          perfOverlay: _perfOverlay,
          targetFps: _targetFps,
          renderer: _renderer,
        ),
      ),
    );
    if (result != null && mounted) {
      setState(() {
        _engineMode = result.engineMode;
        _customDylibPath = result.customDylibPath;
        _perfOverlay = result.perfOverlay;
        _targetFps = result.targetFps;
        _renderer = result.renderer;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context)!;
    final games = _gameManager.games;
    final colorScheme = Theme.of(context).colorScheme;

    return Scaffold(
      appBar: AppBar(
        title: Text(l10n.appTitle),
        actions: [
          Tooltip(
            message: _engineMode == EngineMode.builtIn
                ? (_builtInAvailable
                    ? l10n.builtInReady
                    : l10n.builtInNotReady)
                : (_customDylibPath != null
                    ? _customDylibPath!.split('/').last
                    : l10n.customNotSet),
            child: Icon(
              _engineMode == EngineMode.builtIn
                  ? Icons.inventory_2
                  : Icons.extension,
              color: _effectiveDylibPath != null
                  ? colorScheme.primary
                  : colorScheme.error,
              size: 22,
            ),
          ),
          IconButton(
            icon: const Icon(Icons.settings),
            tooltip: l10n.settings,
            onPressed: _openSettings,
          ),
        ],
      ),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : games.isEmpty
              ? _buildEmptyState(colorScheme, l10n)
              : _buildGameList(games, colorScheme, l10n),
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
                  label: Text(l10n.refresh),
                ),
                const SizedBox(width: 12),
                FloatingActionButton.extended(
                  heroTag: 'guide',
                  onPressed: _showIosImportGuide,
                  icon: const Icon(Icons.help_outline),
                  label: Text(l10n.howToImport),
                ),
              ],
            )
          : FloatingActionButton.extended(
              onPressed: _addGame,
              icon: const Icon(Icons.add),
              label: Text(l10n.addGame),
            ),
    );
  }

  Widget _buildEmptyState(ColorScheme colorScheme, AppLocalizations l10n) {
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
            l10n.noGamesYet,
            style: TextStyle(
              fontSize: 20,
              color: colorScheme.onSurface.withValues(alpha: 0.6),
            ),
          ),
          const SizedBox(height: 8),
          Text(
            Platform.isIOS ? l10n.noGamesHintIos : l10n.noGamesHintDesktop,
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

  Widget _buildGameList(
      List<GameInfo> games, ColorScheme colorScheme, AppLocalizations l10n) {
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
                l10n: l10n,
                onTap: () => _launchGame(game),
                onRename: () => _renameGame(game),
                onRemove: () => _removeGame(game),
                onSetCover: () => _setCoverImage(game),
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
    required this.l10n,
    required this.onTap,
    required this.onRename,
    required this.onRemove,
    required this.onSetCover,
  });

  final GameInfo game;
  final AppLocalizations l10n;
  final VoidCallback onTap;
  final VoidCallback onRename;
  final VoidCallback onRemove;
  final VoidCallback onSetCover;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final lastPlayed = game.lastPlayed;
    final String subtitle = lastPlayed != null
        ? '${game.path}\n${l10n.lastPlayed(_formatDate(lastPlayed))}'
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
                  image: game.coverPath != null && File(game.coverPath!).existsSync()
                      ? DecorationImage(
                          image: FileImage(File(game.coverPath!)),
                          fit: BoxFit.cover,
                        )
                      : null,
                ),
                child: game.coverPath != null && File(game.coverPath!).existsSync()
                    ? null
                    : Icon(
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
                    case 'cover':
                      onSetCover();
                      break;
                    case 'rename':
                      onRename();
                      break;
                    case 'remove':
                      onRemove();
                      break;
                  }
                },
                itemBuilder: (context) => [
                  PopupMenuItem(
                    value: 'cover',
                    child: ListTile(
                      leading: const Icon(Icons.image),
                      title: Text(l10n.setCover),
                      contentPadding: EdgeInsets.zero,
                      dense: true,
                    ),
                  ),
                  PopupMenuItem(
                    value: 'rename',
                    child: ListTile(
                      leading: const Icon(Icons.edit),
                      title: Text(l10n.rename),
                      contentPadding: EdgeInsets.zero,
                      dense: true,
                    ),
                  ),
                  PopupMenuItem(
                    value: 'remove',
                    child: ListTile(
                      leading:
                          const Icon(Icons.delete, color: Colors.redAccent),
                      title: Text(l10n.remove,
                          style: const TextStyle(color: Colors.redAccent)),
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
    if (diff.inMinutes < 1) return l10n.justNow;
    if (diff.inHours < 1) return l10n.minutesAgo(diff.inMinutes);
    if (diff.inDays < 1) return l10n.hoursAgo(diff.inHours);
    if (diff.inDays < 7) return l10n.daysAgo(diff.inDays);
    return '${date.year}-${date.month.toString().padLeft(2, '0')}-${date.day.toString().padLeft(2, '0')}';
  }
}
