#pragma once
namespace eclient_runtime::modules::gameplay {
bool initialize();
void shutdown();

// FlyX factors (0.1 - 2.0)
float getFlyXUpH();
void setFlyXUpH(float v);
float getFlyXUpV();
void setFlyXUpV(float v);

// KillAura settings (Apollon full — 1.21.111)
float getKillRange();
void setKillRange(float v);
float getRotationsRange();
void setRotationsRange(float v);
int getAuraType();
void setAuraType(int v);
bool getSwing();
void setSwing(bool v);
bool getRotations();
void setRotations(bool v);
bool getHurtCheck();
void setHurtCheck(bool v);
int getMaxKillTick();
void setMaxKillTick(int v);
}
