#pragma once

namespace eclient_runtime::flyinput {

// Shared state for the Fly module's own on-screen fly buttons (UP/DOWN).
// The GUI writes the button rects (logical/window coords) each frame; the
// view-layer input route feeds raw pointer events; the fly tick reads the
// held flags. All state is thread-safe on its own.

// Button geometry in ImGui/window logical coordinates (the same space as
// the floating menu bar). Centers + diameter.
struct Buttons {
    bool valid{false};
    float upCx{0}, upCy{0}, downCx{0}, downCy{0}, dia{0};
};

void setRects(const Buttons& b);
Buttons rects();
void clearRects();

// Feed a pointer event AFTER window->physical scaling. action codes match
// the InGameGui convention: 0 = down, 1 = move, 2 = up/cancel.
// Returns true when this event belongs to a fly button gesture and must be
// CONSUMED (never delivered to the game or ImGui).
bool touch(int pointerId, int action, float logicalX, float logicalY);

// Held state for the fly tick. Any tracked pointer pressing a button holds it.
bool heldUp();
bool heldDown();

// Drop all tracked pointers (fly toggled off / menu opened / buttons hidden).
void reset();

} // namespace eclient_runtime::flyinput
