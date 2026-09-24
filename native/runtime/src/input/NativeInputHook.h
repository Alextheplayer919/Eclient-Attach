#pragma once

namespace eclient_runtime::input {

// Installs the version-independent Minecraft JNI key callback hook exported by
// libminecraftpe.so. The hook only handles the E-Client GUI hotkey and then
// forwards the event to Minecraft's original callback.
bool initialize();
void shutdown();

// Install status, surfaced by the in-game menu so a dead input path is
// diagnosable on-device instead of looking like an unresponsive GUI.
bool keyHookInstalled();
bool touchHookInstalled();

} // namespace eclient_runtime::input
