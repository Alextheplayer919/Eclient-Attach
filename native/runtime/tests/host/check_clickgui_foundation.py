#!/usr/bin/env python3
"""Static guardrails for the intentionally minimal Kotlin ClickGUI."""
from pathlib import Path
import re

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
resume_method = re.search(
    r"\.method public onActivityResumed\(Landroid/app/Activity;\)V\n(?P<body>.*?)\.end method",
    loader,
    re.DOTALL,
)
assert resume_method
resume_body = resume_method.group("body")
touch_ready = "invoke-static {}, Lcom/rubidiumclient/host/EClientLoaderProvider;->nativeTouchReady()V"
module_init = "invoke-static {}, Lcom/rubidiumclient/modules/ModuleRegistry;->init()V"
ui_attach = "invoke-static {p1}, Lcom/rubidiumclient/modules/ui/EClientUi;->attach(Landroid/app/Activity;)V"
assert touch_ready in resume_body
assert resume_body.index(touch_ready) < resume_body.index(module_init) < resume_body.index(ui_attach)
assert re.search(
    r"Java_com_rubidiumclient_host_EClientLoaderProvider_nativeTouchReady\(JNIEnv\* env, jclass\)"
    r"\s*\{\s*eclient_runtime::kotlinbridge::bind\(env\);\s*\}",
    jni_bootstrap,
    re.DOTALL,
)
print("check_clickgui_foundation: view cleanup and Kotlin bridge bind point verified")
