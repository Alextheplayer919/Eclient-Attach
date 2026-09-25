#pragma once

#include <jni.h>

#include <string>

namespace eclient_runtime::kotlinbridge {

// Called from JNI_OnLoad so the bridge can attach the ticking game thread.
void setJavaVm(JavaVM* vm);

// Called from nativeTouchReady (a Java-initiated call, so FindClass uses the
// application classloader) to cache the Kotlin ModuleRegistry class/methods.
// Safe to call repeatedly; binds once.
void bind(JNIEnv* env);

// Mirror a native-side enable toggle into the Kotlin registry. No-op until
// bound (e.g. in the eclient_runtime build where no VM exists).
void setModuleEnabled(const char* name, bool enabled);

// Game-tick dispatch: runs every enabled Kotlin module's onTick on the game
// thread. Stashes the local player so module context natives resolve.
void dispatchTick(void* localPlayer);

// True for modules whose implementation lives in Kotlin (drives nothing on
// its own — ModuleManager uses it for the "Kotlin" tag + JNI toggle mirror).
bool anyKotlinModuleEnabled();

} // namespace eclient_runtime::kotlinbridge
