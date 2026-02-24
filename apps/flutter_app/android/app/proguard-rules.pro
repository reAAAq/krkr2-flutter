# Keep Flutter plugin classes referenced by generated registrant.
-keep class org.github.krkr2.flutter_engine_bridge.** { *; }

# Keep the custom activity entrypoint and native methods.
-keep class org.github.krkr2.flutter_app.MainActivity { *; }
-keep class org.tvp.kirikiri2.KR2Activity { *; }
-keepclassmembers class * {
    native <methods>;
}
