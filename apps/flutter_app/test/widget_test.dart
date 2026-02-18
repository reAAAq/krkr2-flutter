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
    expect(find.text('engine_pause'), findsOneWidget);
    expect(find.text('engine_resume'), findsOneWidget);
    expect(find.text('engine_set_option'), findsOneWidget);
    expect(find.text('Apply bridge path'), findsOneWidget);
    expect(find.text('Event logs'), findsOneWidget);
    expect(find.text('Clear logs'), findsOneWidget);
  });
}
