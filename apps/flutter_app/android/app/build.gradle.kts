plugins {
    id("com.android.application")
    id("kotlin-android")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

android {
    namespace = "org.github.krkr2.flutter_app"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = JavaVersion.VERSION_17.toString()
    }

    defaultConfig {
        // TODO: Specify your own unique Application ID (https://developer.android.com/studio/build/application-id.html).
        applicationId = "org.github.krkr2.flutter_app"
        // You can update the following values to match your application needs.
        // For more information, see: https://flutter.dev/to/review-gradle-config.
        minSdk = flutter.minSdkVersion
        targetSdk = flutter.targetSdkVersion
        versionCode = flutter.versionCode
        versionName = flutter.versionName

        ndk {
            // Supported ABIs for ANGLE + vcpkg
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
                // Only build for arm64-v8a to avoid unnecessary compilation
                abiFilters += listOf("arm64-v8a")
                // Project root is ../../../../ relative to this build.gradle.kts
                val projectRoot = file("../../../../").absolutePath
                val vcpkgRoot = "${projectRoot}/.devtools/vcpkg"
                val ndkDir = android.ndkDirectory.absolutePath
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DVCPKG_TARGET_ANDROID=ON",
                    "-DBUILD_ENGINE_API=ON",
                    "-DENABLE_TESTS=OFF",
                    "-DBUILD_TOOLS=OFF",
                    "-DVCPKG_ROOT=${vcpkgRoot}",
                    // Use vcpkg toolchain as primary, chainload Android NDK toolchain
                    "-DCMAKE_TOOLCHAIN_FILE=${vcpkgRoot}/scripts/buildsystems/vcpkg.cmake",
                    "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=${ndkDir}/build/cmake/android.toolchain.cmake"
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            // Point to the project root CMakeLists.txt which builds both krkr2.so and engine_api.so
            path = file("../../../../CMakeLists.txt")
            version = "3.28.0+"
        }
    }

    buildTypes {
        release {
            // TODO: Add your own signing config for the release build.
            // Signing with the debug keys for now, so `flutter run --release` works.
            signingConfig = signingConfigs.getByName("debug")
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }
}

flutter {
    source = "../.."
}
