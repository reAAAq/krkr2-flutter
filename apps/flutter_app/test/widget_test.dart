import 'package:flutter_test/flutter_test.dart';

import 'package:flutter_app/main.dart';

void main() {
  testWidgets('Flutter shell smoke test', (WidgetTester tester) async {
    await tester.pumpWidget(const FlutterShellApp());
    await tester.pump(const Duration(milliseconds: 200));

    expect(find.text('Krkr2 Flutter Shell'), findsWidgets);
    expect(find.textContaining('Engine bridge status'), findsOneWidget);
    expect(find.text('engine_create'), findsOneWidget);
    expect(find.text('engine_open_game'), findsOneWidget);
    expect(find.text('Start Tick'), findsOneWidget);
    expect(find.text('engine_destroy'), findsOneWidget);
  });
}
