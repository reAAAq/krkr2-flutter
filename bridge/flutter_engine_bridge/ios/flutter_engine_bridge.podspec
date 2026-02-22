#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint flutter_engine_bridge.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'flutter_engine_bridge'
  s.version          = '0.0.1'
  s.summary          = 'KrKr2 engine bridge for Flutter iOS.'
  s.description      = <<-DESC
    Bridges the KrKr2 C++ engine to Flutter iOS via FFI and texture.
  DESC
  s.homepage         = 'http://example.com'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'Your Company' => 'email@example.com' }
  s.source           = { :path => '.' }
  s.source_files     = 'Classes/**/*'
  s.dependency 'Flutter'
  s.platform         = :ios, '15.0'

  # Pre-built engine_api static library
  s.vendored_libraries = 'Libs/libengine_api.a'

  # System framework dependencies
  s.frameworks = 'IOSurface', 'CoreVideo', 'Metal', 'QuartzCore', 'Accelerate',
                 'AudioToolbox', 'AVFoundation', 'OpenAL'

  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386',
    # Ensure static library symbols are not stripped
    'OTHER_LDFLAGS' => '-ObjC -all_load',
  }
  s.swift_version = '5.0'

  # If your plugin requires a privacy manifest, for example if it uses any
  # required reason APIs, update the PrivacyInfo.xcprivacy file to describe your
  # plugin's privacy impact, and then uncomment this line. For more information,
  # see https://developer.apple.com/documentation/bundleresources/privacy_manifest_files
  # s.resource_bundles = {'flutter_engine_bridge_privacy' => ['Resources/PrivacyInfo.xcprivacy']}
end
