import 'dart:convert';

/// Represents a game entry in the launcher.
class GameInfo {
  GameInfo({
    required this.path,
    this.title,
    this.developer,
    this.lastPlayed,
    this.coverPath,
    this.playDurationSeconds,
  });

  /// The root directory of the game.
  String path;

  /// Display title. Falls back to the directory name if null.
  String? title;

  /// Developer / producer name (e.g. from scraping).
  String? developer;

  /// Last time the game was launched.
  DateTime? lastPlayed;

  /// Custom cover image path. Null means use default icon.
  String? coverPath;

  /// Total play time in seconds. Updated when leaving the game page.
  int? playDurationSeconds;

  /// Display name: user-set title or the last directory component.
  String get displayTitle {
    if (title != null && title!.isNotEmpty) return title!;
    final parts = path.split(RegExp(r'[/\\]'));
    return parts.lastWhere((p) => p.isNotEmpty, orElse: () => path);
  }

  /// Format total play seconds as "45m" or "2h 30m".
  static String formatPlayDuration(int seconds) {
    if (seconds < 60) return '0m';
    final minutes = seconds ~/ 60;
    if (minutes < 60) return '${minutes}m';
    final hours = minutes ~/ 60;
    final mins = minutes % 60;
    if (mins == 0) return '${hours}h';
    return '${hours}h ${mins}m';
  }

  Map<String, dynamic> toJson() => {
        'path': path,
        'title': title,
        'developer': developer,
        'lastPlayed': lastPlayed?.toIso8601String(),
        'coverPath': coverPath,
        'playDurationSeconds': playDurationSeconds,
      };

  factory GameInfo.fromJson(Map<String, dynamic> json) => GameInfo(
        path: json['path'] as String,
        title: json['title'] as String?,
        developer: json['developer'] as String?,
        lastPlayed: json['lastPlayed'] != null
            ? DateTime.tryParse(json['lastPlayed'] as String)
            : null,
        coverPath: json['coverPath'] as String?,
        playDurationSeconds: json['playDurationSeconds'] as int?,
      );

  static List<GameInfo> listFromJsonString(String jsonString) {
    try {
      final List<dynamic> list = jsonDecode(jsonString) as List<dynamic>;
      return list
          .map((e) => GameInfo.fromJson(e as Map<String, dynamic>))
          .toList();
    } catch (_) {
      return [];
    }
  }

  static String listToJsonString(List<GameInfo> games) {
    return jsonEncode(games.map((g) => g.toJson()).toList());
  }
}
