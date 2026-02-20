import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../models/game_info.dart';
import '../services/game_manager.dart';
import 'game_page.dart';

/// The home / launcher page — manage and launch games.
class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  static const String _dylibPathKey = 'krkr2_dylib_path';

  final GameManager _gameManager = GameManager();
  bool _loading = true;
  String? _dylibPath;

  @override
  void initState() {
    super.initState();
    _loadGames();
  }

  Future<void> _loadGames() async {
    final prefs = await SharedPreferences.getInstance();
    _dylibPath = prefs.getString(_dylibPathKey);
    await _gameManager.load();
    if (mounted) setState(() => _loading = false);
  }

  Future<void> _addGame() async {
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
    if (_dylibPath == null) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: const Text(
            'Engine dylib not set. Please configure it in Settings first.',
          ),
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
          ffiLibraryPath: _dylibPath,
        ),
      ),
    );
  }

  Future<void> _showSettingsDialog() async {
    String? tempPath = _dylibPath;
    await showDialog<void>(
      context: context,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setDialogState) => AlertDialog(
          title: const Text('Settings'),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
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
                        tooltip: 'Reset to default',
                        onPressed: () {
                          setDialogState(() => tempPath = null);
                        },
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
                      setDialogState(
                        () => tempPath = result.files.single.path,
                      );
                    }
                  },
                  icon: const Icon(Icons.folder_open),
                  label: const Text('Browse...'),
                ),
              ),
            ],
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: const Text('Cancel'),
            ),
            FilledButton(
              onPressed: () async {
                final prefs = await SharedPreferences.getInstance();
                if (tempPath != null) {
                  await prefs.setString(_dylibPathKey, tempPath!);
                } else {
                  await prefs.remove(_dylibPathKey);
                }
                if (mounted) {
                  setState(() => _dylibPath = tempPath);
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

  @override
  Widget build(BuildContext context) {
    final games = _gameManager.games;
    final colorScheme = Theme.of(context).colorScheme;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Krkr2 Launcher'),
        actions: [
          if (_dylibPath != null)
            Padding(
              padding: const EdgeInsets.only(right: 8),
              child: Chip(
                avatar: const Icon(Icons.extension, size: 16),
                label: Text(
                  _dylibPath!.split('/').last,
                  style: const TextStyle(fontSize: 12),
                ),
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
      floatingActionButton: FloatingActionButton.extended(
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
            'Click "Add Game" to select a game directory',
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
