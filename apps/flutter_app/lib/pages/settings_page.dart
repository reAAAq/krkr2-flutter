import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../l10n/app_localizations.dart';
import '../main.dart';
import 'home_page.dart';

/// Standalone settings page with MD3 styling and i18n support.
class SettingsPage extends StatefulWidget {
  const SettingsPage({
    super.key,
    required this.engineMode,
    required this.customDylibPath,
    required this.builtInDylibPath,
    required this.builtInAvailable,
    required this.perfOverlay,
    required this.targetFps,
    required this.renderer,
  });

  final EngineMode engineMode;
  final String? customDylibPath;
  final String? builtInDylibPath;
  final bool builtInAvailable;
  final bool perfOverlay;
  final int targetFps;
  final String renderer;

  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

/// Return value from the settings page.
class SettingsResult {
  const SettingsResult({
    required this.engineMode,
    required this.customDylibPath,
    required this.perfOverlay,
    required this.targetFps,
    required this.renderer,
  });

  final EngineMode engineMode;
  final String? customDylibPath;
  final bool perfOverlay;
  final int targetFps;
  final String renderer;
}

class _SettingsPageState extends State<SettingsPage> {
  static const String _dylibPathKey = 'krkr2_dylib_path';
  static const String _engineModeKey = 'krkr2_engine_mode';
  static const String _perfOverlayKey = 'krkr2_perf_overlay';
  static const String _targetFpsKey = 'krkr2_target_fps';
  static const String _rendererKey = 'krkr2_renderer';
  static const String _localeKey = 'krkr2_locale';
  static const List<int> _fpsOptions = [30, 60, 120];

  late EngineMode _engineMode;
  late String? _customDylibPath;
  late bool _perfOverlay;
  late int _targetFps;
  late String _renderer;
  String _localeCode = 'system';
  bool _dirty = false;

  @override
  void initState() {
    super.initState();
    _engineMode = widget.engineMode;
    _customDylibPath = widget.customDylibPath;
    _perfOverlay = widget.perfOverlay;
    _targetFps = widget.targetFps;
    _renderer = widget.renderer;
    _loadLocale();
  }

  Future<void> _loadLocale() async {
    final prefs = await SharedPreferences.getInstance();
    if (mounted) {
      setState(() {
        _localeCode = prefs.getString(_localeKey) ?? 'system';
      });
    }
  }

  Future<void> _save() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(
      _engineModeKey,
      _engineMode == EngineMode.custom ? 'custom' : 'builtIn',
    );
    if (_customDylibPath != null) {
      await prefs.setString(_dylibPathKey, _customDylibPath!);
    } else {
      await prefs.remove(_dylibPathKey);
    }
    await prefs.setBool(_perfOverlayKey, _perfOverlay);
    await prefs.setInt(_targetFpsKey, _targetFps);
    await prefs.setString(_rendererKey, _renderer);

    if (mounted) {
      Navigator.pop(
        context,
        SettingsResult(
          engineMode: _engineMode,
          customDylibPath: _customDylibPath,
          perfOverlay: _perfOverlay,
          targetFps: _targetFps,
          renderer: _renderer,
        ),
      );
    }
  }

  void _markDirty() {
    if (!_dirty) setState(() => _dirty = true);
  }

  Future<void> _changeLocale(String code) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_localeKey, code);
    if (!mounted) return;
    setState(() => _localeCode = code);

    // Apply locale change in real-time
    if (code == 'system') {
      Krkr2App.setLocale(context, null);
    } else {
      Krkr2App.setLocale(context, Locale(code));
    }
  }

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context)!;
    final colorScheme = Theme.of(context).colorScheme;

    return PopScope(
      canPop: !_dirty,
      onPopInvokedWithResult: (didPop, _) async {
        if (didPop) return;
        final discard = await showDialog<bool>(
          context: context,
          builder: (ctx) => AlertDialog(
            title: Text(l10n.settings),
            content: const Text('Discard unsaved changes?'),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(ctx, false),
                child: Text(l10n.cancel),
              ),
              FilledButton(
                onPressed: () => Navigator.pop(ctx, true),
                child: const Text('Discard'),
              ),
            ],
          ),
        );
        if (discard == true && context.mounted) {
          Navigator.pop(context);
        }
      },
      child: Scaffold(
        appBar: AppBar(
          title: Text(l10n.settings),
          actions: [
            Padding(
              padding: const EdgeInsets.only(right: 8),
              child: FilledButton.icon(
                onPressed: _dirty ? _save : null,
                icon: const Icon(Icons.save, size: 18),
                label: Text(l10n.save),
              ),
            ),
          ],
        ),
        body: ListView(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          children: [
            // ── Engine section ──
            _SectionHeader(
              icon: Icons.settings_applications,
              label: l10n.settingsEngine,
            ),
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(l10n.engineMode,
                        style: Theme.of(context).textTheme.titleSmall),
                    const SizedBox(height: 8),
                    SizedBox(
                      width: double.infinity,
                      child: SegmentedButton<EngineMode>(
                        segments: [
                          ButtonSegment<EngineMode>(
                            value: EngineMode.builtIn,
                            label: Text(l10n.builtIn),
                            icon: const Icon(Icons.inventory_2),
                          ),
                          ButtonSegment<EngineMode>(
                            value: EngineMode.custom,
                            label: Text(l10n.custom),
                            icon: const Icon(Icons.folder_open),
                          ),
                        ],
                        selected: {_engineMode},
                        onSelectionChanged: (Set<EngineMode> selected) {
                          setState(() => _engineMode = selected.first);
                          _markDirty();
                        },
                      ),
                    ),
                    const SizedBox(height: 12),
                    if (_engineMode == EngineMode.builtIn)
                      _buildBuiltInStatus(context, l10n, colorScheme),
                    if (_engineMode == EngineMode.custom)
                      _buildCustomDylibPicker(context, l10n, colorScheme),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 16),

            // ── Rendering section ──
            _SectionHeader(
              icon: Icons.brush,
              label: l10n.settingsRendering,
            ),
            Card(
              child: Column(
                children: [
                  Padding(
                    padding: const EdgeInsets.fromLTRB(16, 16, 16, 0),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(l10n.renderPipeline,
                            style: Theme.of(context).textTheme.titleSmall),
                        const SizedBox(height: 4),
                        Text(
                          l10n.renderPipelineHint,
                          style: Theme.of(context).textTheme.bodySmall?.copyWith(
                                color: colorScheme.onSurface
                                    .withValues(alpha: 0.6),
                              ),
                        ),
                        const SizedBox(height: 8),
                        SizedBox(
                          width: double.infinity,
                          child: SegmentedButton<String>(
                            segments: [
                              ButtonSegment<String>(
                                value: 'opengl',
                                label: Text(l10n.opengl),
                                icon: const Icon(Icons.memory),
                              ),
                              ButtonSegment<String>(
                                value: 'software',
                                label: Text(l10n.software),
                                icon: const Icon(Icons.computer),
                              ),
                            ],
                            selected: {_renderer},
                            onSelectionChanged: (Set<String> selected) {
                              setState(() => _renderer = selected.first);
                              _markDirty();
                            },
                          ),
                        ),
                      ],
                    ),
                  ),
                  const Divider(height: 24),
                  SwitchListTile(
                    title: Text(l10n.performanceOverlay),
                    subtitle: Text(l10n.performanceOverlayDesc),
                    value: _perfOverlay,
                    onChanged: (value) {
                      setState(() => _perfOverlay = value);
                      _markDirty();
                    },
                  ),
                  const Divider(height: 1),
                  ListTile(
                    title: Text(l10n.targetFrameRate),
                    subtitle: Text(l10n.targetFrameRateDesc),
                    trailing: DropdownButton<int>(
                      value: _targetFps,
                      items: _fpsOptions
                          .map((fps) => DropdownMenuItem<int>(
                                value: fps,
                                child: Text(l10n.fpsLabel(fps)),
                              ))
                          .toList(),
                      onChanged: (value) {
                        if (value != null) {
                          setState(() => _targetFps = value);
                          _markDirty();
                        }
                      },
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 16),

            // ── General section ──
            _SectionHeader(
              icon: Icons.language,
              label: l10n.settingsGeneral,
            ),
            Card(
              child: Column(
                children: [
                  ListTile(
                    title: Text(l10n.language),
                    trailing: DropdownButton<String>(
                      value: _localeCode,
                      items: [
                        DropdownMenuItem(
                          value: 'system',
                          child: Text(l10n.languageSystem),
                        ),
                        DropdownMenuItem(
                          value: 'en',
                          child: Text(l10n.languageEn),
                        ),
                        DropdownMenuItem(
                          value: 'zh',
                          child: Text(l10n.languageZh),
                        ),
                        DropdownMenuItem(
                          value: 'ja',
                          child: Text(l10n.languageJa),
                        ),
                      ],
                      onChanged: (value) {
                        if (value != null) {
                          _changeLocale(value);
                        }
                      },
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 16),

            // ── About section ──
            _SectionHeader(
              icon: Icons.info_outline,
              label: l10n.settingsAbout,
            ),
            Card(
              child: ListTile(
                title: Text(l10n.version),
                trailing: const Text('1.0.0'),
              ),
            ),
            const SizedBox(height: 32),
          ],
        ),
      ),
    );
  }

  Widget _buildBuiltInStatus(
      BuildContext context, AppLocalizations l10n, ColorScheme colorScheme) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: widget.builtInAvailable
            ? Colors.green.withValues(alpha: 0.1)
            : colorScheme.errorContainer.withValues(alpha: 0.3),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(
          color: widget.builtInAvailable
              ? Colors.green.withValues(alpha: 0.3)
              : colorScheme.error.withValues(alpha: 0.3),
        ),
      ),
      child: Row(
        children: [
          Icon(
            widget.builtInAvailable ? Icons.check_circle : Icons.warning_amber,
            color: widget.builtInAvailable
                ? Colors.green
                : colorScheme.error,
            size: 20,
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  widget.builtInAvailable
                      ? l10n.builtInEngineAvailable
                      : l10n.builtInEngineNotFound,
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                    color: widget.builtInAvailable
                        ? Colors.green
                        : colorScheme.error,
                  ),
                ),
                if (!widget.builtInAvailable)
                  Padding(
                    padding: const EdgeInsets.only(top: 4),
                    child: Text(
                      l10n.builtInEngineHint,
                      style: TextStyle(
                        fontSize: 11,
                        color: colorScheme.onSurface.withValues(alpha: 0.6),
                      ),
                    ),
                  ),
                if (widget.builtInAvailable && widget.builtInDylibPath != null)
                  Padding(
                    padding: const EdgeInsets.only(top: 4),
                    child: Text(
                      widget.builtInDylibPath!,
                      style: TextStyle(
                        fontSize: 11,
                        fontFamily: 'monospace',
                        color: colorScheme.onSurface.withValues(alpha: 0.5),
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
    );
  }

  Widget _buildCustomDylibPicker(
      BuildContext context, AppLocalizations l10n, ColorScheme colorScheme) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(l10n.engineDylibPath,
            style: Theme.of(context).textTheme.titleSmall),
        const SizedBox(height: 8),
        Container(
          padding: const EdgeInsets.all(12),
          decoration: BoxDecoration(
            color: colorScheme.surfaceContainerHighest,
            borderRadius: BorderRadius.circular(8),
          ),
          child: Row(
            children: [
              Expanded(
                child: Text(
                  _customDylibPath ?? l10n.notSetRequired,
                  style: TextStyle(
                    fontSize: 13,
                    fontFamily: 'monospace',
                    color: _customDylibPath != null
                        ? null
                        : colorScheme.error.withValues(alpha: 0.7),
                  ),
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                ),
              ),
              if (_customDylibPath != null)
                IconButton(
                  icon: const Icon(Icons.clear, size: 18),
                  tooltip: l10n.clearPath,
                  onPressed: () {
                    setState(() => _customDylibPath = null);
                    _markDirty();
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
                dialogTitle: l10n.selectEngineDylib,
                type: FileType.any,
              );
              if (result != null && result.files.single.path != null) {
                setState(() => _customDylibPath = result.files.single.path);
                _markDirty();
              }
            },
            icon: const Icon(Icons.folder_open),
            label: Text(l10n.browse),
          ),
        ),
      ],
    );
  }
}

/// Section header widget for settings groups.
class _SectionHeader extends StatelessWidget {
  const _SectionHeader({required this.icon, required this.label});

  final IconData icon;
  final String label;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(left: 4, bottom: 8, top: 4),
      child: Row(
        children: [
          Icon(icon, size: 18, color: Theme.of(context).colorScheme.primary),
          const SizedBox(width: 8),
          Text(
            label,
            style: Theme.of(context).textTheme.titleSmall?.copyWith(
                  color: Theme.of(context).colorScheme.primary,
                  fontWeight: FontWeight.w600,
                ),
          ),
        ],
      ),
    );
  }
}
