#pragma once

namespace eclient_runtime::host::InGameGui {

bool initialize();
void shutdown();
void toggleMenu(); // kept for compat — now toggles collapsed
bool menuOpen();   // kept for compat — returns !collapsed

void setCollapsed(bool collapsed);
bool isCollapsed();
void toggleCollapsed();
bool isTouchOnGui(float x, float y); // hit-test for input hook

void setPhysicalWindowSize(int width, int height);
void submitTouch(float x, float y, int action);

} // namespace eclient_runtime::host::InGameGui
