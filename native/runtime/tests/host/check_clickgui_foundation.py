#!/usr/bin/env python3
"""Static guardrails for the intentionally minimal Kotlin ClickGUI."""
from pathlib import Path

ui = (Path(__file__).resolve().parents[4] / "kotlin-modules/src/com/rubidiumclient/modules/ui/EClientUi.kt").read_text()
loader = (Path(__file__).resolve().parents[4] / "tools/minecraft-host-loader.smali").read_text()
jni_bootstrap = (Path(__file__).resolve().parents[4] / "native/runtime/src/host/JniBootstrap.cpp").read_text()

assert "WeakReference<FrameLayout>" in ui
assert "MATCH_PARENT" not in ui
assert "addContentView" not in ui
assert "removeAllViews()" in ui
assert "No modules are registered yet." in ui
assert "nativeGuiTouch" not in ui
assert "EClientUi;->detach()V" in loader
assert ".method public static native nativeTouchReady()V" in loader
assert "invoke-static {}, Lcom/rubidiumclient/host/EClientLoaderProvider;->nativeTouchReady()V" in loader
assert "Java_com_rubidiumclient_host_EClientLoaderProvider_nativeTouchReady" in jni_bootstrap
assert "eclient_runtime::kotlinbridge::bind(env);" in jni_bootstrap
print("check_clickgui_foundation: view cleanup and Kotlin bridge bind point verified")
