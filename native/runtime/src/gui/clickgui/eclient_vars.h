#pragma once
// Extern declarations for Zeff's variables.h globals — so GameplayModules can read them
// without including variables.h (which would cause duplicate definitions).
// InGameGui.cpp is the single TU that includes variables.h and thus defines these.

#include <string>
#include <vector>

// Core toggles
extern bool killAura;
extern bool targetLock;
extern bool fullBright;
extern bool noHurtCam;
extern bool noCamDist;
extern bool noBoatRotation;
extern bool noCamSleep;
extern bool placeCam;
extern bool noSlowDown;
extern bool offSlowdownTrigger;
extern bool noclip;
extern bool xrayCam;
extern bool noEmoteCooldown;
extern bool noBlur;
extern bool noCaveVignette;
extern bool creativeFly;
extern bool forceCreativeFly;
extern bool alwaysSprinting;
extern bool step;
extern bool antiKnockback;
extern bool offDrown;
extern bool clickAutoMine;
extern bool esp;
extern bool antiInvis;
extern bool playerNear;
extern bool deathPosition;

// Values
extern float killRange;
extern float rotationsRange;
extern int auraType;
extern bool swing;
extern bool rotations;
extern bool hurtCheck;
extern int maxKillTick;
extern float fov;
extern bool enableFov;
extern int speedValue;
extern float jumpValue;
extern float gravityValue;

// Config / state from variables.h (used by Patches() bridging)
extern bool panic;
extern bool inMenu;
extern bool inHudScreen;
