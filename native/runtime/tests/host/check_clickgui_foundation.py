#!/usr/bin/env python3
"""Static guardrails for the intentionally minimal Kotlin ClickGUI."""
from pathlib import Path

ui = (Path(__file__).resolve().parents[4] / "kotlin-modules/src/com/rubidiumclient/modules/ui/EClientUi.kt").read_text()
loader = (Path(__file__).resolve().parents[4] / "tools/minecraft-host-loader.smali").read_text()

assert "WeakReference<FrameLayout>" in ui
assert "MATCH_PARENT" not in ui
assert "addContentView" not in ui
assert "removeAllViews()" in ui
assert "No modules are registered yet." in ui
assert "nativeGuiTouch" not in ui
assert "EClientUi;->detach()V" in loader
print("check_clickgui_foundation: bounded view tree and pause cleanup verified")
