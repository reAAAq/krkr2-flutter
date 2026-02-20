import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:flutter_app/main.dart';

void main() {
  testWidgets('Krkr2App smoke test - HomePage renders', (
    WidgetTester tester,
  ) async {
    await tester.pumpWidget(const Krkr2App());
    await tester.pumpAndSettle();

    // HomePage should show the app title
    expect(find.text('Krkr2'), findsWidgets);
  });

  testWidgets('HomePage shows empty state when no games added', (
    WidgetTester tester,
  ) async {
    await tester.pumpWidget(const Krkr2App());
    await tester.pumpAndSettle();

    // Should show the add game FAB
    expect(find.byType(FloatingActionButton), findsOneWidget);
  });

  testWidgets('Settings button is present', (WidgetTester tester) async {
    await tester.pumpWidget(const Krkr2App());
    await tester.pumpAndSettle();

    // Settings icon should be in the app bar
    expect(find.byIcon(Icons.settings), findsOneWidget);
  });
}
