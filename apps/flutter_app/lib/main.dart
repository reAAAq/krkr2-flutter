import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'l10n/app_localizations.dart';
import 'pages/home_page.dart';

void main() {
  runApp(const Krkr2App());
}

class Krkr2App extends StatefulWidget {
  const Krkr2App({super.key});

  /// Change the app locale at runtime. Pass null to follow system default.
  static void setLocale(BuildContext context, Locale? locale) {
    final state = context.findAncestorStateOfType<_Krkr2AppState>();
    state?._setLocale(locale);
  }

  @override
  State<Krkr2App> createState() => _Krkr2AppState();
}

class _Krkr2AppState extends State<Krkr2App> {
  static const String _localeKey = 'krkr2_locale';
  Locale? _locale; // null = follow system

  @override
  void initState() {
    super.initState();
    _loadLocale();
  }

  Future<void> _loadLocale() async {
    final prefs = await SharedPreferences.getInstance();
    final code = prefs.getString(_localeKey);
    if (code != null && code != 'system' && mounted) {
      setState(() => _locale = Locale(code));
    }
  }

  void _setLocale(Locale? locale) {
    setState(() => _locale = locale);
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'KrKr2 Next',
      debugShowCheckedModeBanner: false,
      locale: _locale,
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.teal,
          brightness: Brightness.light,
        ),
        useMaterial3: true,
        cardTheme: CardThemeData(
          elevation: 1,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(12),
          ),
        ),
      ),
      darkTheme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.teal,
          brightness: Brightness.dark,
        ),
        useMaterial3: true,
        cardTheme: CardThemeData(
          elevation: 1,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(12),
          ),
        ),
      ),
      home: const HomePage(),
    );
  }
}
