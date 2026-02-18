import 'package:flutter_test/flutter_test.dart';

import 'package:flutter_app/main.dart';

void main() {
  testWidgets('Flutter shell smoke test', (WidgetTester tester) async {
    await tester.pumpWidget(const FlutterShellApp());

    expect(find.text('Krkr2 Flutter Shell'), findsWidgets);
    expect(find.textContaining('Plugin bridge status'), findsOneWidget);
  });
}
