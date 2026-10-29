#pragma once

namespace eclient_runtime::input {

// Installs the version-independent Minecraft JNI key callback hook exported by
// libminecraftpe.so. The hook only handles the E-Client GUI hotkey and then
// forwards the event to Minecraft's original callback.
bool initialize();
void shutdown();

} // namespace eclient_runtime::input
