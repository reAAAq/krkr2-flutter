#include "include/flutter_engine_bridge/flutter_engine_bridge_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "flutter_engine_bridge_plugin.h"

void FlutterEngineBridgePluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  flutter_engine_bridge::FlutterEngineBridgePlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
