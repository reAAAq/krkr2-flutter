import 'package:shared_preferences/shared_preferences.dart';

import '../models/game_info.dart';

/// Manages persisted game list using SharedPreferences.
class GameManager {
  static const String _storageKey = 'krkr2_game_list';

  List<GameInfo> _games = [];

  List<GameInfo> get games => List.unmodifiable(_games);

  /// Load the game list from persistent storage.
  Future<void> load() async {
    final prefs = await SharedPreferences.getInstance();
    final String? raw = prefs.getString(_storageKey);
    if (raw != null && raw.isNotEmpty) {
      _games = GameInfo.listFromJsonString(raw);
    }
  }

  /// Save the current game list to persistent storage.
  Future<void> _save() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_storageKey, GameInfo.listToJsonString(_games));
  }

  /// Add a game. Returns true if added (not a duplicate).
  Future<bool> addGame(GameInfo game) async {
    // Deduplicate by path
    if (_games.any((g) => g.path == game.path)) {
      return false;
    }
    _games.add(game);
    await _save();
    return true;
  }

  /// Remove a game by path.
  Future<void> removeGame(String path) async {
    _games.removeWhere((g) => g.path == path);
    await _save();
  }

  /// Update the lastPlayed timestamp for a game.
  Future<void> markPlayed(String path) async {
    final index = _games.indexWhere((g) => g.path == path);
    if (index >= 0) {
      _games[index].lastPlayed = DateTime.now();
      await _save();
    }
  }

  /// Rename a game's title.
  Future<void> renameGame(String path, String newTitle) async {
    final index = _games.indexWhere((g) => g.path == path);
    if (index >= 0) {
      _games[index].title = newTitle;
      await _save();
    }
  }

  /// Set a custom cover image path for a game.
  Future<void> setCoverImage(String path, String? coverPath) async {
    final index = _games.indexWhere((g) => g.path == path);
    if (index >= 0) {
      _games[index].coverPath = coverPath;
      await _save();
    }
  }
}
