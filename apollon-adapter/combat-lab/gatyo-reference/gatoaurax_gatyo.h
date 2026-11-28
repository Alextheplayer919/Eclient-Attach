#pragma once
// GatoAuraX portado de GatoClient (reconstruccion del KillAuraX de Noctyrex
// v4.3 + modos propios). Los 16 modos de rotacion conservan la misma
// matematica y las mismas constantes que GatoAuraXRots1..8.cpp; lo que cambia
// es de donde salen los datos:
//   stateVector->pos / velocity  -> Actor+GetPosition (StateVectorComponent: pos, posAnterior, velocidad)
//   aabbShape->aabb / height     -> el AABBShapeComponent de getAABBDim (el AABB esta 0x18 antes que las medidas)
//   rotation->presentRot         -> getRotation() (pitch, yaw)
//   getEyePos()                  -> la posicion: en Bedrock la de un jugador ya esta a la altura de los ojos
//   isKeyDown(WASD/espacio)      -> el joystick (MoveInput*, fly_gatyo.h) y el salto
// La rotacion se aplica igual que en GatoClient: en el jugador (camara,
// cabeza y cuerpo, desde el hook Intersects) y en el PlayerAuthInputPacket
// (pitch +0x2C, yaw +0x30, cabeza +0x40, verificado en su write()).
// Switch, Auto Trident y Rain Mode usan los helpers de inventario y clima de
// plus999aura_gatyo.h (se incluye despues: aca solo van declarados). No
// portado: el ping de Eclipse (sin conexion de red: usa solo "Adelanto de Eclipse").

#include <map>

static const float kGaxRad2Deg = 57.295776f;
static const float kGaxPi = 3.14159265f;

enum GaxRotation {
	GAX_ROT_NONE, GAX_ROT_SMOOTH, GAX_ROT_NEMESIS, GAX_ROT_STRAFE, GAX_ROT_VORTEX, GAX_ROT_FRONTSTRAFE,
	GAX_ROT_AIRHVH, GAX_ROT_ATOMIC, GAX_ROT_ALPHA, GAX_ROT_SCORE, GAX_ROT_LEGION, GAX_ROT_VALHALLA,
	GAX_ROT_BERSERK, GAX_ROT_RAGNAROK, GAX_ROT_FENRIR, GAX_ROT_ECLIPSE, GAX_ROT_COUNT
};
static const char* const kGaxRotNames[GAX_ROT_COUNT] = {
	"None", "Smooth", "Nemesis", "Strafe", "Vortex", "FrontStrafe", "AirHvH Pro", "Atomic", "Alpha", "Score",
	"Legion", "Valhalla", "Berserk", "Ragnarok", "Fenrir", "Eclipse",
};

struct GaxV3 {
	float x, y, z;
};
struct GaxRot {
	float x, y;  // pitch, yaw
};
struct GaxBox {
	GaxV3 lower, upper;
};

// Jugador falso (fakeplayer_gatyo.h): un actor de mentira con los campos que
// el aura lee directo de memoria. Lo que en un actor real pasa por funciones
// del juego (hitbox, tipo, ataque) se desvia aca.
Actor* FakePlayerActor();  // nullptr si no esta, esta muerto o desconectado
const GaxBox& FakePlayerBox();
void FakePlayerAuraHit(LocalPlayer* lp);

// Arma, tridente y clima compartidos con el +999 Aura (plus999aura_gatyo.h).
struct AuraSkyCache {
	bool open = false;
	int cooldown = 0;
};
static int AuraWantedSlot(LocalPlayer* lp, BlockSource* region, Actor* target, bool rainMode, bool autoTrident,
                          int switchMode, AuraSkyCache& sky);
static bool AuraSwitchTo(LocalPlayer* lp, int slot, int switchMode);
static void AuraSwitchRestore(LocalPlayer* lp, bool silent);

static bool GaxIsFake(Actor* a) {
	return a && a == FakePlayerActor();
}

// El NPC del jugador falso es un jugador real del cliente; solo la vida es simulada.
// El juego solo lo interpola hacia las posiciones de los MoveActorAbsolute, asi
// que su velocidad y su AABB salen de la simulacion y de su posicion.
bool FakePlayerIsNpc(Actor* actor);
Actor* FakePlayerNpcActor();  // nullptr si no hay NPC, esta muerto o desconectado
int FakePlayerHealth();
GaxV3 FakePlayerNpcStep();
const GaxBox& FakePlayerNpcBox();

// ---------------------------------------------------------------------------
// Acceso a los componentes del actor
// ---------------------------------------------------------------------------
static bool GaxValid(Actor* a) {
	if (GaxIsFake(a)) return true;
	return a && a->getPosition() && (uintptr_t)a->getAABBDim() > 0x1000;
}

static GaxV3 GaxPos(Actor* a) {
	const vec3_t* p = a->getPosition();
	return {p->x, p->y, p->z};
}

static GaxV3 GaxVel(Actor* a) {
	if (FakePlayerIsNpc(a)) return FakePlayerNpcStep();
	const vec3_t* v = (const vec3_t*)((char*)a->getPosition() + 24);
	return {v->x, v->y, v->z};
}

static const GaxBox& GaxAABB(Actor* a) {
	if (GaxIsFake(a)) return FakePlayerBox();
	if (FakePlayerIsNpc(a)) return FakePlayerNpcBox();
	return *(const GaxBox*)((char*)a->getAABBDim() - 0x18);
}

static float GaxHeight(Actor* a) {
	if (GaxIsFake(a)) return 1.8f;
	return a->getAABBDim()->y;
}

static float GaxYaw(Actor* a) {
	vec2_t* rot = a->getRotation();
	return rot ? rot->y : 0.0f;
}

static const int kGaxPlayerType = 319;  // ActorType de jugador (319 = 0x13F)

// ---------------------------------------------------------------------------
// Teclas del original (W/A/S/D/espacio) desde el joystick y el salto
// ---------------------------------------------------------------------------
static const uint32_t kGaxKeyW = 0x57, kGaxKeyA = 0x41, kGaxKeyS = 0x53, kGaxKeyD = 0x44, kGaxKeySpace = 0x20;
static bool g_gaxKeyW, g_gaxKeyA, g_gaxKeyS, g_gaxKeyD, g_gaxKeySpace;

static void GaxUpdateKeys(LocalPlayer* lp) {
	g_gaxKeyW = g_gaxKeyA = g_gaxKeyS = g_gaxKeyD = false;
	float length = sqrtf(g_moveInputX * g_moveInputX + g_moveInputZ * g_moveInputZ);
	if (length > g_moveDeadzone) {
		// relativo a la camara: adelante (-sin, cos) e izquierda (cos, sin)
		float yaw = GaxYaw(lp) / kGaxRad2Deg;
		float forward = (g_moveInputX * -sinf(yaw) + g_moveInputZ * cosf(yaw)) / length;
		float left = (g_moveInputX * cosf(yaw) + g_moveInputZ * sinf(yaw)) / length;
		g_gaxKeyW = forward > 0.38f;
		g_gaxKeyS = forward < -0.38f;
		g_gaxKeyA = left > 0.38f;
		g_gaxKeyD = left < -0.38f;
	}
	g_gaxKeySpace = GaxVel(lp).y > 0.2f;  // subiendo por un salto
}

static bool GaxKey(uint32_t vk) {
	switch (vk) {
		case kGaxKeyW: return g_gaxKeyW;
		case kGaxKeyA: return g_gaxKeyA;
		case kGaxKeyS: return g_gaxKeyS;
		case kGaxKeyD: return g_gaxKeyD;
		case kGaxKeySpace: return g_gaxKeySpace;
	}
	return false;
}

// Reloj de juego en segundos (el original lee el tiempo de ImGui).
static double GaxClock() {
	static const auto start = std::chrono::steady_clock::now();
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// ---------------------------------------------------------------------------
// Contexto de los handlers (GatoAuraXRotCtx)
// ---------------------------------------------------------------------------
struct GaxRotCtx {
	float pred50, c158, c15C, c160, c164, c20C, c214, c120, c124;
	int c11C, c130;
	bool randomizeHit;
	GaxRot* rotOut;
	float* headYaw;
	float eclipseLead;
};

static GaxV3 GaxNormalize(const GaxV3& v) {
	float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
	if (len < 0.0001f) return v;
	return {v.x / len, v.y / len, v.z / len};
}

static int& GaxTickCounter() {
	static int tick = 0;
	return tick;
}

// RotHeadYaw: yaw hacia el target suavizado desde el yaw actual (factor c120)
static float GaxHeadYaw(GaxRotCtx& c, LocalPlayer* lp, const GaxV3& targetPos, float yawActual) {
	GaxV3 l = GaxPos(lp);
	float targetYaw = -atan2f(targetPos.x - l.x, targetPos.z - l.z) * kGaxRad2Deg;
	while (targetYaw > 180.0f) targetYaw -= 360.0f;
	while (targetYaw < -180.0f) targetYaw += 360.0f;
	float headYaw = yawActual + (targetYaw - yawActual) * c.c120;
	while (headYaw > 180.0f) headYaw -= 360.0f;
	while (headYaw < -180.0f) headYaw += 360.0f;
	if (c.headYaw) *c.headYaw = headYaw;
	return headYaw;
}

// ---------------------------------------------------------------------------
// Alpha: recorre las 4 esquinas del hitbox predicho, cada 2 ticks
// ---------------------------------------------------------------------------
static void GaxAlpha(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	GaxV3 lpPos = GaxPos(lp);
	GaxV3 tPos = GaxPos(target), tVel = GaxVel(target);
	const GaxBox& box = GaxAABB(target);
	float t = c.pred50 / 50.0f;
	int counter = ++GaxTickCounter();
	int corner = (counter / 2) % 4;

	float tY = tPos.y, tZ = tPos.z;
	float predX = tPos.x + t * tVel.x;
	float predZ = tPos.z + t * tVel.z;
	float predY = tY + t * tVel.y;
	float height = box.upper.y - box.lower.y;
	float width = box.upper.x - box.lower.x;
	predY += height * 0.65f;
	float w38 = width * 0.38f;
	float h15 = height * 0.15f;

	GaxV3 n = GaxNormalize({predX - lpPos.x, predY - lpPos.y, predZ - lpPos.z});
	GaxV3 right = GaxNormalize({-n.z, 0.0f, n.x});
	float offX = w38 * right.x;
	float offZ = w38 * right.z;
	switch (corner) {
		case 0: predY += h15; predX -= offX; predZ -= offZ; break;
		case 1: predX += offX; predY += h15; predZ += offZ; break;
		case 2: predX += offX; predY -= h15; predZ += offZ; break;
		case 3: predY -= h15; predX -= offX; predZ -= offZ; break;
	}

	float dx = predX - lpPos.x, dy = predY - lpPos.y, dz = predZ - lpPos.z;
	float dist = (float)sqrt((double)dy * dy + (double)dx * dx + (double)dz * dz);
	float pitch = c.rotOut->x, yaw = c.rotOut->y;
	if (dist > 0.001f) {
		pitch = asinf(dy / dist) * -kGaxRad2Deg;
		yaw = -atan2f(dx, dz) * kGaxRad2Deg;
	}
	// el fold inferior de pitch suma 90 (literal del binario)
	while (pitch > 90.0f) pitch -= 180.0f;
	while (pitch < -90.0f) pitch += 90.0f;
	while (yaw > 180.0f) yaw -= 360.0f;
	while (yaw < -180.0f) yaw += 360.0f;
	*c.rotOut = {pitch, yaw};
	GaxHeadYaw(c, lp, {tPos.x, tY, tZ}, yaw);
}

// ---------------------------------------------------------------------------
// Smooth: lerp continuo por distancia, prediccion con velocidad relativa
// ---------------------------------------------------------------------------
static void GaxSmooth(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	GaxV3 lpPos = GaxPos(lp), lpVel = GaxVel(lp);
	GaxV3 tPos = GaxPos(target), tVel = GaxVel(target);
	float dist = (float)sqrt((double)(lpPos.y - tPos.y) * (lpPos.y - tPos.y) + (double)(lpPos.x - tPos.x) * (lpPos.x - tPos.x) +
	                         (double)(lpPos.z - tPos.z) * (lpPos.z - tPos.z));
	float k = dist * 0.15f;
	float predY = tPos.y + (tVel.y - lpVel.y) * k;
	float predX = tPos.x + (tVel.x - lpVel.x) * k;
	float predZ = tPos.z + (tVel.z - lpVel.z) * k;
	float aimY = predY + GaxHeight(target) * 0.75f;

	float dx = predX - lpPos.x, dy = aimY - lpPos.y, dz = predZ - lpPos.z;
	float dist2 = (float)sqrt((double)dy * dy + (double)dx * dx + (double)dz * dz);
	float pitch = c.rotOut->x, yaw = c.rotOut->y;
	if (dist2 > 0.001f) {
		pitch = asinf(dy / dist2) * -kGaxRad2Deg;
		yaw = -atan2f(dx, dz) * kGaxRad2Deg;
	}
	while (pitch > 90.0f) pitch -= 180.0f;
	while (pitch < -90.0f) pitch += 90.0f;
	while (yaw > 180.0f) yaw -= 360.0f;
	while (yaw < -180.0f) yaw += 360.0f;

	float dPitch = pitch - c.rotOut->x;
	float dYaw = yaw - c.rotOut->y;
	while (dYaw > 180.0f) dYaw -= 360.0f;
	while (dYaw < -180.0f) dYaw += 360.0f;
	float f = 8.0f / (dist + 2.0f);
	float lerp = f <= 0.85f ? (f >= 0.4f ? f : 0.4f) : 0.85f;
	float newYaw = c.rotOut->y + lerp * dYaw;
	float newPitch = c.rotOut->x + lerp * dPitch;
	while (newPitch > 90.0f) newPitch -= 180.0f;
	while (newPitch < -90.0f) newPitch += 90.0f;
	while (newYaw > 180.0f) newYaw -= 360.0f;
	while (newYaw < -180.0f) newYaw += 360.0f;
	*c.rotOut = {newPitch, newYaw};
}

// Rotacion base + wrap + lerp hacia rotOut (cola de Nemesis, Vortex y similares)
static void GaxAimLerp(GaxRotCtx& c, float dx, float dy, float dz, float lerp) {
	float dist = (float)sqrt((double)dy * dy + (double)dx * dx + (double)dz * dz);
	float pitch = c.rotOut->x, yaw = c.rotOut->y;
	if (dist > 0.001f) {
		pitch = asinf(dy / dist) * -kGaxRad2Deg;
		yaw = -atan2f(dx, dz) * kGaxRad2Deg;
	}
	while (pitch > 90.0f) pitch -= 180.0f;
	while (pitch < -90.0f) pitch += 180.0f;
	while (yaw > 180.0f) yaw -= 360.0f;
	while (yaw < -180.0f) yaw += 360.0f;
	float curP = c.rotOut->x, curY = c.rotOut->y;
	float dYaw = yaw - curY;
	float dPit = pitch - curP;
	while (dYaw > 180.0f) dYaw -= 360.0f;
	while (dYaw < -180.0f) dYaw += 360.0f;
	pitch = curP + dPit * lerp;
	yaw = curY + dYaw * lerp;
	while (pitch > 90.0f) pitch -= 180.0f;
	while (pitch < -90.0f) pitch += 180.0f;
	while (yaw > 180.0f) yaw -= 360.0f;
	while (yaw < -180.0f) yaw += 360.0f;
	*c.rotOut = {pitch, yaw};
}

// ---------------------------------------------------------------------------
// Nemesis: punto orbital lateral; la altura sale de las teclas, lerp 0.9
// ---------------------------------------------------------------------------
static void GaxNemesis(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	GaxV3 lpPos = GaxPos(lp);
	GaxV3 tPos = GaxPos(target), tVel = GaxVel(target);
	float t = c.pred50 / 50.0f;
	GaxV3 pred = {tPos.x + tVel.x * t, tPos.y + tVel.y * t, tPos.z + tVel.z * t};
	float h = GaxHeight(target);
	bool kW = GaxKey(kGaxKeyW), kS = GaxKey(kGaxKeyS), kA = GaxKey(kGaxKeyA), kD = GaxKey(kGaxKeyD), kSp = GaxKey(kGaxKeySpace);

	float dxp = pred.x - lpPos.x, dzp = pred.z - lpPos.z;
	float d = sqrtf(dxp * dxp + dzp * dzp);
	float px = 0.0f, pz = 0.0f;
	if (d > 0.001f) {
		px = -(dzp / d);
		pz = dxp / d;
	}
	float vOff = kS ? 0.1f : (kW ? (kSp ? 0.25f : 0.85f) : 0.5f);
	float aimY = vOff * h + pred.y;
	if (kA && !kD) {
		pred.x += px * 0.45f;
		pred.z += pz * 0.45f;
	} else if (!kA && kD) {
		pred.x -= px * 0.45f;
		pred.z -= pz * 0.45f;
	}
	if (kS && (kA || kD)) {
		float tf = (float)GaxClock();
		pred.x += sinf(tf * 25.0f) * 0.3f;
		pred.z += cosf(tf * 25.0f) * 0.3f;
		aimY += cosf(tf * 18.0f) * 0.2f;
	}
	GaxAimLerp(c, pred.x - lpPos.x, aimY - lpPos.y, pred.z - lpPos.z, 0.9f);
}

// ---------------------------------------------------------------------------
// Vortex: helice alrededor del target predicho (t = dist*0.15), lerp 0.8
// ---------------------------------------------------------------------------
static void GaxVortex(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	GaxV3 lpPos = GaxPos(lp);
	GaxV3 tPos = GaxPos(target), tVel = GaxVel(target);
	double d0 = (double)(lpPos.y - tPos.y) * (lpPos.y - tPos.y) + (double)(lpPos.x - tPos.x) * (lpPos.x - tPos.x) +
	            (double)(lpPos.z - tPos.z) * (lpPos.z - tPos.z);
	float tPred = (float)sqrt(d0) * 0.15f;
	const GaxBox& box = GaxAABB(target);
	float radiusBase = (box.upper.x - box.lower.x) * 0.5f;
	float h = GaxHeight(target);
	float ang = (float)GaxClock() * 16.0f;
	float radius = radiusBase * 1.3f;
	float aimX = tPos.x + tVel.x * tPred;
	float aimY = tPos.y + tVel.y * tPred;
	float aimZ = tPos.z + tVel.z * tPred;
	float dx = aimX + cosf(ang) * radius - lpPos.x;
	float dy = aimY + h * 0.5f + sinf(ang * 0.5f) * (h * 0.3f) - lpPos.y;
	float dz = aimZ + sinf(ang) * radius - lpPos.z;
	GaxAimLerp(c, dx, dy, dz, 0.8f);
}

// ---------------------------------------------------------------------------
// FrontStrafe: 4 rutas por distancia (3.0), teclas y velocidad del target
// ---------------------------------------------------------------------------
static std::map<Actor*, float> g_gaxFsTargetYaw;

static void GaxFrontStrafe(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	if (!target->getRotation()) return;
	GaxV3 tPos = GaxPos(target), tVel = GaxVel(target), lPos = GaxPos(lp);
	float dx0 = tPos.x - lPos.x, dy0 = tPos.y - lPos.y, dz0 = tPos.z - lPos.z;
	double dy0sq = (double)dy0 * dy0;
	float dist3 = (float)sqrt(dy0sq + (double)dx0 * dx0 + (double)dz0 * dz0);
	float velx = tVel.x, vely = tVel.y, velz = tVel.z;
	float speed2D = sqrtf(velx * velx + velz * velz);

	float pitch = c.rotOut->x, yaw = c.rotOut->y;
	if (dist3 <= 3.0f) {
		if (GaxKey(kGaxKeyW) || GaxKey(kGaxKeyA) || GaxKey(kGaxKeyS) || GaxKey(kGaxKeyD) || speed2D <= 0.1f) {
			// ruta A: orbita temporal alrededor del target
			float ang = (float)GaxClock() * 3.0f;
			float r = c.c158;
			float dx = tPos.x + cosf(ang) * r - lPos.x;
			float dz = tPos.z + sinf(ang) * r - lPos.z;
			float d = (float)sqrt((double)dx * dx + dy0sq + (double)dz * dz);
			if (d > 0.001f) {
				pitch = asinf(dy0 / d) * -kGaxRad2Deg;
				yaw = -atan2f(dx, dz) * kGaxRad2Deg;
			}
		} else {
			// ruta B: 1 bloque delante del facing del target, suavizado por target
			auto it = g_gaxFsTargetYaw.find(target);
			float cur = GaxYaw(target);
			float prev = it != g_gaxFsTargetYaw.end() ? it->second : cur;
			float delta = cur - prev;
			while (delta > 180.0f) delta -= 360.0f;
			while (delta < -180.0f) delta += 360.0f;
			float ang = (cur + delta * c.c160) * 0.0174532924f;
			float dx = tPos.x - sinf(ang) - lPos.x;
			float dz = tPos.z + cosf(ang) - lPos.z;
			float d = (float)sqrt((double)dx * dx + dy0sq + (double)dz * dz);
			if (d > 0.001f) {
				pitch = asinf(dy0 / d) * -kGaxRad2Deg;
				yaw = -atan2f(dx, dz) * kGaxRad2Deg;
			}
		}
	} else {
		float dxN = 0.0f, dyN = 0.0f, dzN = 0.0f;
		if (dist3 != 0.0f) {
			dyN = dy0 / dist3;
			dzN = dz0 / dist3;
			dxN = dx0 / dist3;
		}
		bool chase = false;
		if (speed2D > 0.1f && velx * dxN + vely * dyN + velz * dzN > 0.0f) {
			// ruta C: persecucion, adelante del target en su direccion de movimiento
			float tp = dist3 * 0.125f;
			float pX = tPos.x + velx * tp, pY = tPos.y + vely * tp, pZ = tPos.z + velz * tp;
			float speed3 = (float)sqrt((double)velx * velx + (double)vely * vely + (double)velz * velz);
			float dirX = 0.0f, dirY = 0.0f, dirZ = 0.0f;
			if (speed3 != 0.0f) {
				dirX = velx / speed3;
				dirY = vely / speed3;
				dirZ = velz / speed3;
			}
			float dy = pY + dirY * c.c15C - lPos.y;
			float dx = pX + dirX * c.c15C - lPos.x;
			float dz = pZ + dirZ * c.c15C - lPos.z;
			float d = (float)sqrt((double)dy * dy + (double)dx * dx + (double)dz * dz);
			if (d > 0.001f) {
				pitch = asinf(dy / d) * -kGaxRad2Deg;
				yaw = -atan2f(dx, dz) * kGaxRad2Deg;
			}
			chase = true;
		}
		if (!chase) {
			// ruta D: directo a la posicion actual
			pitch = asinf(dy0 / dist3) * -kGaxRad2Deg;
			yaw = -atan2f(dx0, dz0) * kGaxRad2Deg;
		}
	}
	while (pitch > 90.0f) pitch -= 180.0f;
	while (pitch < -90.0f) pitch += 180.0f;
	while (yaw > 180.0f) yaw -= 360.0f;
	while (yaw < -180.0f) yaw += 360.0f;
	*c.rotOut = {pitch, yaw};
	// el original nunca limpia el mapa; aca se acota para no juntar punteros viejos
	if (g_gaxFsTargetYaw.size() > 64) g_gaxFsTargetYaw.clear();
	g_gaxFsTargetYaw[target] = GaxYaw(target);
}

// ---------------------------------------------------------------------------
// Historial del target (AirHvH Pro y Valhalla tienen uno cada uno)
// ---------------------------------------------------------------------------
struct GaxHistory {
	static const int kCap = 16, kMax = 10;
	GaxV3 pos[kCap];
	float yaw[kCap];
	int off = 0, cnt = 0;

	void Record(const GaxV3& p, float y) {
		int slot = (off + cnt) & (kCap - 1);
		pos[slot] = p;
		yaw[slot] = y;
		if (cnt < kMax) cnt++;
		else off = (off + 1) & (kCap - 1);
	}
	float YawAt(int i) const { return yaw[(off + i) & (kCap - 1)]; }
	const GaxV3& PosAt(int i) const { return pos[(off + i) & (kCap - 1)]; }
	const GaxV3& FromEnd(int k) const { return pos[(off - k + cnt) & (kCap - 1)]; }

	float YawVariance() const {
		if (cnt < 3) return 999.0f;
		float v[kMax];
		int n = 0;
		float sum = 0.0f;
		for (int i = 1; i < cnt; i++) {
			float d = YawAt(i) - YawAt(i - 1);
			if (d > 180.0f) d += -360.0f;
			if (-180.0f > d) d += 360.0f;
			v[n++] = d;
			sum += d;
		}
		float mean = sum / (float)n, var = 0.0f;
		for (int i = 0; i < n; i++) var += (v[i] - mean) * (v[i] - mean);
		return var / (float)n;
	}

	int ZigzagXZ() const {
		if (cnt < 4) return 0;
		int changes = 0;
		float prevSign = 0.0f;
		for (int i = 0; i + 2 < cnt; i++) {
			const GaxV3 &p0 = PosAt(i), &p1 = PosAt(i + 1), &p2 = PosAt(i + 2);
			float curv = (p2.x - 2.0f * p1.x + p0.x) + (p2.z - 2.0f * p1.z + p0.z);
			if (curv != 0.0f && prevSign != 0.0f && curv * prevSign < 0.0f) changes++;
			prevSign = curv > 0.0f ? 1.0f : -1.0f;
		}
		return changes;
	}

	float AccelVariance() const {
		if (cnt < 4) return 0.0f;
		float v[kMax];
		int n = 0;
		for (int i = 2; i < cnt; i++) {
			const GaxV3 &p0 = PosAt(i), &p1 = PosAt(i - 1), &p2 = PosAt(i - 2);
			float ax = p0.x - 2.0f * p1.x + p2.x, ay = p0.y - 2.0f * p1.y + p2.y, az = p0.z - 2.0f * p1.z + p2.z;
			v[n++] = (float)sqrt((double)ax * ax + (double)ay * ay + (double)az * az);
		}
		float sum = 0.0f;
		for (int i = 0; i < n; i++) sum += v[i];
		float mean = sum / (float)n, var = 0.0f;
		for (int i = 0; i < n; i++) var += (v[i] - mean) * (v[i] - mean);
		return var / (float)n;
	}

	// velocidad, aceleracion y jerk por diferencias finitas (o la velocidad del snapshot)
	void Derivatives(const GaxV3& snapVel, GaxV3& vel, GaxV3& acc, GaxV3& jerk) const {
		acc = {0, 0, 0};
		jerk = {0, 0, 0};
		if (cnt < 3) {
			vel = snapVel;
			return;
		}
		const GaxV3 &p1 = FromEnd(1), &p2 = FromEnd(2), &p3 = FromEnd(3);
		GaxV3 d21 = {p1.x - p2.x, p1.y - p2.y, p1.z - p2.z};
		GaxV3 d32 = {p2.x - p3.x, p2.y - p3.y, p2.z - p3.z};
		acc = {d21.x - d32.x, d21.y - d32.y, d21.z - d32.z};
		vel = d21;
		if (cnt >= 4) {
			const GaxV3& p4 = FromEnd(4);
			jerk = {acc.x - (d32.x - (p3.x - p4.x)), acc.y - (d32.y - (p3.y - p4.y)), acc.z - (d32.z - (p3.z - p4.z))};
		}
	}
};

// Maquina de estados compartida por AirHvH Pro y Valhalla: devuelve el
// estado y su factor, y deja la velocidad de extrapolacion en spd.
static int GaxPredictState(const GaxHistory& hist, float& yawDelta, const GaxV3& vel, const GaxV3& acc,
                           float c20C, float pred50, float& stateFactor, float& spd) {
	if (hist.cnt >= 2) {
		float d = hist.YawAt(hist.cnt - 1) - hist.YawAt(hist.cnt - 2);
		if (d > 180.0f) d += -360.0f;
		if (-180.0f > d) d += 360.0f;
		yawDelta = d;
	}
	int state = 0;
	stateFactor = 0.5f;
	if (fabsf(yawDelta) > 5.0f && hist.YawVariance() < 3.0f) {
		stateFactor = 0.9f;
		state = yawDelta <= 0.0f ? 1 : 0;
	}
	if (hist.ZigzagXZ() >= 3 && state == 0) {
		stateFactor = 0.8f;
		state = 3;
	}
	if (fabsf(vel.y) > sqrtf(vel.x * vel.x + vel.z * vel.z) * 0.7f) {
		stateFactor = 0.85f;
		state = 4 + (vel.y >= 0.0f ? 1 : 0);
	}
	if (hist.AccelVariance() > 2.0f) {
		stateFactor = 0.6f;
		state = 6;
	}
	float dlen = (float)sqrt((double)vel.y * vel.y + (double)vel.x * vel.x + (double)vel.z * vel.z);
	float speed = dlen * 0.5f * 2.0f + 3.0f;
	float alen = (float)sqrt((double)acc.y * acc.y + (double)acc.x * acc.x + (double)acc.z * acc.z);
	if (alen > 0.5f) speed *= 0.8f;
	switch (state) {
		case 0: speed *= 1.2f; break;
		case 3: speed *= 0.7f; break;
		case 4: case 5: speed *= 0.9f; break;
		case 6: speed *= 0.5f; break;
	}
	speed = stateFactor * c20C * speed + pred50 / 50.0f;
	spd = speed <= 1.0f ? 1.0f : (speed > 10.0f ? 10.0f : speed);
	return state;
}

// ---------------------------------------------------------------------------
// AirHvH Pro: historial + maquina de estados + extrapolacion con curva
// ---------------------------------------------------------------------------
static GaxHistory g_gaxAirHist;
static float g_gaxAirYawDelta = 0.0f;
static GaxV3 g_gaxAirLastTarget = {0, 0, 0};

static void GaxAirHvHPro(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	if (!target->getRotation()) return;
	GaxV3 tPos = GaxPos(target), tVelSnap = GaxVel(target);
	GaxV3 lPos = GaxPos(lp), lVel = GaxVel(lp);
	g_gaxAirHist.Record(tPos, GaxYaw(target));
	GaxV3 vel, acc, jerk;
	g_gaxAirHist.Derivatives(tVelSnap, vel, acc, jerk);
	float stateFactor, spd;
	int state = GaxPredictState(g_gaxAirHist, g_gaxAirYawDelta, vel, acc, c.c20C, c.pred50, stateFactor, spd);

	float s3 = spd * spd * spd;
	float aimX = tPos.x + spd * vel.x + s3 * (0.5f * acc.x + (1.0f / 6.0f) * jerk.x);
	float aimY = tPos.y + spd * vel.y + s3 * (0.5f * acc.y + (1.0f / 6.0f) * jerk.y);
	float aimZ = tPos.z + spd * vel.z + s3 * (0.5f * acc.z + (1.0f / 6.0f) * jerk.z);
	if (state == 1 || state == 2) {
		float ang = atan2f(aimZ - g_gaxAirLastTarget.z, aimX - g_gaxAirLastTarget.x) + (state == 1 ? 0.3f : -0.3f);
		aimX += cosf(ang) * 0.5f;
		aimZ += sinf(ang) * 0.5f;
	}
	aimY += 0.3f;  // Vertical Offset por defecto del modulo original

	float selfK = spd * 0.3f;
	float dx = aimX - (lPos.x + selfK * lVel.x);
	float dy = aimY - (lPos.y + selfK * lVel.y);
	float dz = aimZ - (lPos.z + selfK * lVel.z);
	float dist = (float)sqrt((double)dy * dy + (double)dx * dx + (double)dz * dz);
	float pitch = c.rotOut->x, yaw = c.rotOut->y;
	if (dist > 0.001f) {
		pitch = asinf(dy / dist) * -kGaxRad2Deg;
		yaw = -atan2f(dx, dz) * kGaxRad2Deg;
	}
	while (pitch > 90.0f) pitch -= 180.0f;
	while (pitch < -90.0f) pitch += 180.0f;
	while (yaw > 180.0f) yaw -= 360.0f;
	while (yaw < -180.0f) yaw += 360.0f;
	g_gaxAirLastTarget = tPos;
	*c.rotOut = {pitch, yaw};
	*c.headYaw = yaw;
}

// ---------------------------------------------------------------------------
// Atomic: la esquina mas cercana de las 8 del box trasladado t ticks
// ---------------------------------------------------------------------------
static void GaxAtomic(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	GaxV3 l = GaxPos(lp), ext = GaxVel(target);
	const GaxBox& b = GaxAABB(target);
	float t = c.pred50 / 50.0f;
	float ax = t * ext.x, ay = t * ext.y, az = t * ext.z;
	GaxV3 p1 = {b.lower.x + ax, b.lower.y + ay, b.lower.z + az};
	GaxV3 p2 = {b.upper.x + ax, b.upper.y + ay, b.upper.z + az};

	double dy1 = (double)(l.y - p1.y) * (l.y - p1.y), dx1 = (double)(l.x - p1.x) * (l.x - p1.x), dz1 = (double)(l.z - p1.z) * (l.z - p1.z);
	double dx2 = (double)(l.x - p2.x) * (l.x - p2.x), dy2 = (double)(l.y - p2.y) * (l.y - p2.y), dz2 = (double)(l.z - p2.z) * (l.z - p2.z);
	GaxV3 bestP = p1;
	float best = (float)sqrt(dx1 + dy1 + dz1);
	auto tryCorner = [&](double sq, GaxV3 p, bool update) {
		float cand = (float)sqrt(sq);
		if (best > cand) {
			bestP = p;
			if (update) best = cand;
		}
	};
	tryCorner(dx2 + dy1 + dz1, {p2.x, p1.y, p1.z}, true);
	tryCorner(dx1 + dy2 + dz1, {p1.x, p2.y, p1.z}, true);
	tryCorner(dx2 + dy2 + dz1, {p2.x, p2.y, p1.z}, true);
	tryCorner(dx1 + dy1 + dz2, {p1.x, p1.y, p2.z}, true);
	tryCorner(dx2 + dy1 + dz2, {p2.x, p1.y, p2.z}, true);
	tryCorner(dx1 + dy2 + dz2, {p1.x, p2.y, p2.z}, true);
	tryCorner(dx2 + dy2 + dz2, {p2.x, p2.y, p2.z}, false);  // el original no actualiza best en la ultima

	float dy = bestP.y - l.y, dx = bestP.x - l.x, dz = bestP.z - l.z;
	float d3 = (float)sqrt((double)dy * dy + (double)dx * dx + (double)dz * dz);
	float pitch = c.rotOut->x, yaw = c.rotOut->y;
	if (d3 > 0.001f) {
		pitch = asinf(dy / d3) * -kGaxRad2Deg;
		yaw = -atan2f(dx, dz) * kGaxRad2Deg;
	}
	while (pitch > 90.0f) pitch -= 180.0f;
	while (pitch < -90.0f) pitch += 180.0f;
	while (yaw > 180.0f) yaw -= 360.0f;
	while (yaw < -180.0f) yaw += 360.0f;
	*c.rotOut = {pitch, yaw};
	*c.headYaw = yaw;
}

// Cola de Score y Legion: rotacion directa, sin escribir si queda NaN
static void GaxStoreAim(GaxRot& rot, float dx, float dy, float dz, double dist) {
	float pitch = asinf(dy / (float)dist) * -kGaxRad2Deg;
	float yaw = -atan2f(dx, dz) * kGaxRad2Deg;
	if (!std::isfinite(pitch) || !std::isfinite(yaw)) return;
	while (pitch > 90.0f) pitch += -180.0f;
	while (pitch < -90.0f) pitch += 180.0f;
	while (yaw > 180.0f) yaw += -360.0f;
	while (yaw < -180.0f) yaw += 360.0f;
	rot = {pitch, yaw};
}

// ---------------------------------------------------------------------------
// Score: prediccion lineal con lead por distancia y las 4 esquinas horizontales
// ---------------------------------------------------------------------------
static int g_gaxScoreCounter = 0;

static void GaxScore(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	GaxV3 eye = GaxPos(lp), p = GaxPos(target), v = GaxVel(target);
	double d0 = sqrt((double)(eye.y - p.y) * (eye.y - p.y) + (double)(eye.x - p.x) * (eye.x - p.x) + (double)(eye.z - p.z) * (eye.z - p.z));
	float t = (float)d0 * 0.15f;
	float lead = 0.5f <= t ? t : 0.5f;
	if (!(t <= 4.5f)) lead = 4.5f;
	if (sqrtf(v.z * v.z + v.x * v.x) > 0.15f) lead *= 1.5f;
	const GaxBox& bb = GaxAABB(target);
	float x = v.x * lead + p.x;
	float z = v.z * lead + p.z;
	float yAim = (bb.upper.y - bb.lower.y) * 0.7f + (v.y * lead + p.y);
	float off = (bb.upper.x - bb.lower.x) * 0.4f;
	switch ((++g_gaxScoreCounter / 2) % 4) {
		case 0: z += off; x += off; break;
		case 1: x -= off; z += off; break;
		case 2: x -= off; z -= off; break;
		case 3: z -= off; x += off; break;
	}
	float dx = x - eye.x, dy = yAim - eye.y, dz = z - eye.z;
	GaxStoreAim(*c.rotOut, dx, dy, dz, sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz));
}

// ---------------------------------------------------------------------------
// Legion: esquina mas cercana del box desplazado, -0.35 en Y y orbita de 0.15
// ---------------------------------------------------------------------------
static void GaxLegion(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	GaxV3 eye = GaxPos(lp), vel = GaxVel(target), tp = GaxPos(target);
	if (GaxIsFake(target) || target->getEntityTypeId() == kGaxPlayerType) tp.y -= 1.6f;  // Actor::getHumanPos
	double d0 = sqrt((double)(eye.y - tp.y) * (eye.y - tp.y) + (double)(eye.x - tp.x) * (eye.x - tp.x) + (double)(eye.z - tp.z) * (eye.z - tp.z));
	float k = (float)d0 * 0.1f + c.pred50 / 50.0f;
	const GaxBox& bb = GaxAABB(target);
	float lx = k * vel.x + bb.lower.x, ux = k * vel.x + bb.upper.x;
	float ly = k * vel.y + bb.lower.y, uy = k * vel.y + bb.upper.y;
	float lz = k * vel.z + bb.lower.z, uz = k * vel.z + bb.upper.z;
	double ayL = (double)(eye.y - ly) * (eye.y - ly), ayU = (double)(eye.y - uy) * (eye.y - uy);
	double bxL = (double)(eye.x - lx) * (eye.x - lx), bxU = (double)(eye.x - ux) * (eye.x - ux);
	double czL = (double)(eye.z - lz) * (eye.z - lz), czU = (double)(eye.z - uz) * (eye.z - uz);
	float bestX = lx, bestY = ly, bestZ = lz;
	float best = (float)sqrt((bxL + ayL) + czL);
	auto tryCorner = [&](double xy, double zz, float cx, float cy, float cz) {
		float d = (float)sqrt(xy + zz);
		if (best > d) {
			best = d;
			bestX = cx;
			bestY = cy;
			bestZ = cz;
		}
	};
	tryCorner(bxU + ayL, czL, ux, ly, lz);
	tryCorner(bxL + ayU, czL, lx, uy, lz);
	tryCorner(bxU + ayU, czL, ux, uy, lz);
	tryCorner(bxL + ayL, czU, lx, ly, uz);
	tryCorner(bxU + ayL, czU, ux, ly, uz);
	tryCorner(bxL + ayU, czU, lx, uy, uz);
	tryCorner(bxU + ayU, czU, ux, uy, uz);
	float ang = (float)GaxClock() * 30.0f;
	float dy = (bestY - 0.35f) - eye.y;
	float dx = (sinf(ang) * 0.15f + bestX) - eye.x;
	float dz = (cosf(ang) * 0.15f + bestZ) - eye.z;
	GaxStoreAim(*c.rotOut, dx, dy, dz, sqrt((double)dy * dy + (double)dx * dx + (double)dz * dz));
}

// ---------------------------------------------------------------------------
// Valhalla: combinacion de los handlers (historial, teclas, orbita, esquinas)
// ---------------------------------------------------------------------------
static GaxHistory g_gaxValHist;
static float g_gaxValYawDelta = 0.0f;
static GaxV3 g_gaxValLastTarget = {0, 0, 0};
static int g_gaxValTick = 0;

static void GaxValhalla(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	if (!target->getRotation()) return;
	GaxV3 lpPos = GaxPos(lp), lVel = GaxVel(lp);
	GaxV3 tPos = GaxPos(target), tVelSnap = GaxVel(target);
	g_gaxValHist.Record(tPos, GaxYaw(target));
	g_gaxValTick++;
	GaxV3 vel, acc, jerk;
	g_gaxValHist.Derivatives(tVelSnap, vel, acc, jerk);
	float stateFactor, spd;
	int state = GaxPredictState(g_gaxValHist, g_gaxValYawDelta, vel, acc, c.c20C, c.pred50, stateFactor, spd);

	float s3 = spd * spd * spd;
	float aimX = tPos.x + spd * vel.x + s3 * (0.5f * acc.x + (1.0f / 6.0f) * jerk.x);
	float aimY = tPos.y + spd * vel.y + s3 * (0.5f * acc.y + (1.0f / 6.0f) * jerk.y);
	float aimZ = tPos.z + spd * vel.z + s3 * (0.5f * acc.z + (1.0f / 6.0f) * jerk.z);
	const GaxBox& box = GaxAABB(target);
	float h = GaxHeight(target);
	float w = box.upper.x - box.lower.x;

	bool kW = GaxKey(kGaxKeyW), kS = GaxKey(kGaxKeyS), kA = GaxKey(kGaxKeyA), kD = GaxKey(kGaxKeyD), kSp = GaxKey(kGaxKeySpace);
	float vOff = kS ? 0.1f : (kW ? (kSp ? 0.25f : 0.85f) : 0.5f);
	aimY = vOff * h + aimY;
	if (kA != kD) {
		float dxp = aimX - lpPos.x, dzp = aimZ - lpPos.z;
		float d = sqrtf(dxp * dxp + dzp * dzp);
		if (d > 0.001f) {
			float sign = kA ? 1.0f : -1.0f;
			aimX += -(dzp / d) * 0.45f * sign;
			aimZ += (dxp / d) * 0.45f * sign;
		}
	}
	if (kS && (kA || kD)) {
		float tf = (float)GaxClock();
		aimX += sinf(tf * 25.0f) * 0.3f;
		aimZ += cosf(tf * 25.0f) * 0.3f;
		aimY += cosf(tf * 18.0f) * 0.2f;
	}
	if (state == 1 || state == 2) {
		float ang = atan2f(aimZ - g_gaxValLastTarget.z, aimX - g_gaxValLastTarget.x) + (state == 1 ? 0.3f : -0.3f);
		aimX += cosf(ang) * 0.5f;
		aimZ += sinf(ang) * 0.5f;
	}

	float dist2D = sqrtf((aimX - lpPos.x) * (aimX - lpPos.x) + (aimZ - lpPos.z) * (aimZ - lpPos.z));
	float orbitRadius = dist2D > 3.0f ? w * 0.5f * 0.8f : w * 0.5f * 1.3f;
	float orbitSpeed = dist2D > 3.0f ? 12.0f : 16.0f;
	float orbitAng = (float)GaxClock() * orbitSpeed;
	aimX += cosf(orbitAng) * orbitRadius;
	aimZ += sinf(orbitAng) * orbitRadius;
	aimY += sinf(orbitAng * 0.5f) * (h * 0.3f);

	float cornerOff = w * 0.38f;
	switch ((g_gaxValTick / 2) % 4) {
		case 0: aimX -= cornerOff; aimZ -= cornerOff; aimY += h * 0.15f; break;
		case 1: aimX += cornerOff; aimZ += cornerOff; aimY += h * 0.15f; break;
		case 2: aimX += cornerOff; aimZ += cornerOff; aimY -= h * 0.15f; break;
		case 3: aimX -= cornerOff; aimZ -= cornerOff; aimY -= h * 0.15f; break;
	}
	aimY += 0.3f;

	float selfK = spd * 0.3f;
	float dx = aimX - (lpPos.x + selfK * lVel.x);
	float dy = aimY - (lpPos.y + selfK * lVel.y);
	float dz = aimZ - (lpPos.z + selfK * lVel.z);
	float lerp = dist2D < 3.0f ? 0.9f : (dist2D > 6.0f ? 0.7f : 0.85f);
	GaxAimLerp(c, dx, dy, dz, lerp);
	g_gaxValLastTarget = tPos;
	*c.headYaw = c.rotOut->y;
}

// ---------------------------------------------------------------------------
// Berserk: persecucion agresiva con aim vertical y orbita por tick
// ---------------------------------------------------------------------------
static GaxHistory g_gaxBerHist;
static int g_gaxBerTick = 0;

static void GaxBerserk(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	GaxV3 lpPos = GaxPos(lp), lVel = GaxVel(lp);
	GaxV3 tPos = GaxPos(target), tVel = GaxVel(target);
	g_gaxBerHist.Record(tPos, 0.0f);
	g_gaxBerTick++;
	GaxV3 vel = tVel, acc = {0, 0, 0}, jerk;
	if (g_gaxBerHist.cnt >= 3) g_gaxBerHist.Derivatives(tVel, vel, acc, jerk);

	float dx0 = tPos.x - lpPos.x, dy0 = tPos.y - lpPos.y, dz0 = tPos.z - lpPos.z;
	float dist3 = (float)sqrt((double)dy0 * dy0 + (double)dx0 * dx0 + (double)dz0 * dz0);
	float speed = ImClamp(dist3 * 0.1f + c.pred50 / 50.0f, 1.0f, 8.0f);
	float aimX = tPos.x + vel.x * speed + 0.5f * acc.x * speed * speed;
	float aimY = tPos.y + vel.y * speed + 0.5f * acc.y * speed * speed;
	float aimZ = tPos.z + vel.z * speed + 0.5f * acc.z * speed * speed;
	const GaxBox& box = GaxAABB(target);
	float h = GaxHeight(target);
	float w = box.upper.x - box.lower.x;

	float yNorm = (aimY - lpPos.y) / (dist3 > 0.001f ? dist3 : 1.0f);
	if (yNorm > 0.3f) aimY += h * 0.2f;
	else if (yNorm < -0.3f) aimY -= h * 0.1f;
	if (dist3 > 6.0f) {
		float lead = dist3 * 0.15f;
		aimX += vel.x * lead;
		aimZ += vel.z * lead;
	}
	float orbitAng = (float)g_gaxBerTick * 0.35f;
	float orbitR = dist3 < 4.0f ? w * 0.8f : w * 0.4f;
	aimX += cosf(orbitAng) * orbitR;
	aimZ += sinf(orbitAng) * orbitR;
	if (dist3 < 3.0f) {
		float off = w * 0.3f;
		switch (g_gaxBerTick % 4) {
			case 0: aimX -= off; aimZ -= off; break;
			case 1: aimX += off; aimZ += off; break;
			case 2: aimX += off; aimZ -= off; break;
			case 3: aimX -= off; aimZ += off; break;
		}
	}
	aimY += 0.3f;

	float dx = aimX - (lpPos.x + 0.2f * lVel.x);
	float dy = aimY - (lpPos.y + 0.2f * lVel.y);
	float dz = aimZ - (lpPos.z + 0.2f * lVel.z);
	float lerp = dist3 > 8.0f ? 0.6f : (dist3 > 4.0f ? 0.75f : 0.9f);
	GaxAimLerp(c, dx, dy, dz, lerp);
	*c.headYaw = c.rotOut->y;
}

// ---------------------------------------------------------------------------
// Ragnarok / Fenrir / Eclipse (counter-HvH): tracker con prediccion adaptativa
// ---------------------------------------------------------------------------
static float GaxClampf(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

static float GaxWrap180(float a) {
	while (a > 180.0f) a -= 360.0f;
	while (a < -180.0f) a += 360.0f;
	return a;
}

static float GaxWrap90(float p) {
	while (p > 90.0f) p -= 180.0f;
	while (p < -90.0f) p += 180.0f;
	return p;
}

static void GaxWriteRot(GaxRotCtx& c, float dx, float dy, float dz) {
	float dist = (float)sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz);
	if (dist <= 0.001f) return;
	float pitch = GaxWrap90(asinf(GaxClampf(dy / dist, -1.0f, 1.0f)) * -kGaxRad2Deg);
	float yaw = GaxWrap180(-atan2f(dx, dz) * kGaxRad2Deg);
	if (!std::isfinite(pitch) || !std::isfinite(yaw)) return;
	*c.rotOut = {pitch, yaw};
	if (c.headYaw) *c.headYaw = yaw;
}

struct GaxCounterTracker {
	Actor* lastActor = nullptr;
	int tick = 0;
	GaxV3 prevFeet = {0, 0, 0}, prevStep = {0, 0, 0}, velEma = {0, 0, 0}, accEma = {0, 0, 0};
	float prevBearing = 0.0f;
	bool hasPrev = false;
	float oscAxisX = 1.0f, oscAxisZ = 0.0f, flipPeriod = 0.0f;
	int lastFlipTick = -100;
	int flipTicks[8] = {-100, -100, -100, -100, -100, -100, -100, -100};
	int flipHead = 0;
	float lastHeading = 0.0f, turnRate = 0.0f;
	bool headingValid = false;

	void Reset(Actor* a) {
		*this = GaxCounterTracker();
		lastActor = a;
	}
	int RecentFlips() const {
		int n = 0;
		for (int i = 0; i < 8; i++) {
			if (tick - flipTicks[i] <= 26) n++;
		}
		return n;
	}
};

static GaxCounterTracker g_gaxTrRagnarok, g_gaxTrFenrir, g_gaxTrEclipse;

// Reconstruye el AABB en coordenadas de pies y predice el siguiente paso
static bool GaxUpdateAndPredict(GaxCounterTracker& tr, Actor* target, LocalPlayer* lp, GaxV3& outPredFeet, float& outW, float& outH, GaxV3& outEye) {
	if (tr.lastActor != target) tr.Reset(target);
	tr.tick++;
	outEye = GaxPos(lp);
	const GaxBox& bb = GaxAABB(target);
	outW = std::max(0.2f, bb.upper.x - bb.lower.x);
	outH = std::max(0.5f, bb.upper.y - bb.lower.y);
	GaxV3 feetNow = {(bb.lower.x + bb.upper.x) * 0.5f, bb.lower.y, (bb.lower.z + bb.upper.z) * 0.5f};
	GaxV3 svVel = GaxVel(target);

	GaxV3 step = svVel;
	if (tr.hasPrev) {
		GaxV3 dPos = {feetNow.x - tr.prevFeet.x, feetNow.y - tr.prevFeet.y, feetNow.z - tr.prevFeet.z};
		float svMag2 = svVel.x * svVel.x + svVel.y * svVel.y + svVel.z * svVel.z;
		float dpMag2 = dPos.x * dPos.x + dPos.y * dPos.y + dPos.z * dPos.z;
		if (svMag2 < 0.0001f && dpMag2 < 25.0f) step = dPos;
		if (dpMag2 > 36.0f) {
			// teleport o perla: reiniciar historial
			tr.Reset(target);
			tr.hasPrev = true;
			tr.prevFeet = feetNow;
			outPredFeet = feetNow;
			return true;
		}
	}
	float spH = sqrtf(step.x * step.x + step.z * step.z);
	float prevSpH = sqrtf(tr.prevStep.x * tr.prevStep.x + tr.prevStep.z * tr.prevStep.z);

	// 1) zigzag por eje propio 2D
	if (tr.hasPrev && spH > 0.03f && prevSpH > 0.03f) {
		float dot2D = step.x * tr.prevStep.x + step.z * tr.prevStep.z;
		if (dot2D < -0.25f * spH * prevSpH) {
			int iv = tr.tick - tr.lastFlipTick;
			if (iv >= 2 && iv <= 20) tr.flipPeriod = tr.flipPeriod <= 0.0f ? (float)iv : 0.4f * tr.flipPeriod + 0.6f * (float)iv;
			tr.lastFlipTick = tr.tick;
			tr.flipTicks[tr.flipHead & 7] = tr.tick;
			tr.flipHead++;
			float dvx = step.x - tr.prevStep.x, dvz = step.z - tr.prevStep.z;
			float dvm = sqrtf(dvx * dvx + dvz * dvz);
			if (dvm > 0.01f) {
				tr.oscAxisX = dvx / dvm;
				tr.oscAxisZ = dvz / dvm;
			}
		}
	}

	// 2) EMA de velocidad/aceleracion y giro (CTRV)
	GaxV3 oldVel = tr.velEma;
	tr.velEma = {oldVel.x + 0.72f * (step.x - oldVel.x), oldVel.y + 0.72f * (step.y - oldVel.y), oldVel.z + 0.72f * (step.z - oldVel.z)};
	tr.accEma.x = GaxClampf(tr.accEma.x * 0.45f + 0.55f * (tr.velEma.x - oldVel.x), -0.25f, 0.25f);
	tr.accEma.y = GaxClampf(tr.accEma.y * 0.45f + 0.55f * (tr.velEma.y - oldVel.y), -0.25f, 0.25f);
	tr.accEma.z = GaxClampf(tr.accEma.z * 0.45f + 0.55f * (tr.velEma.z - oldVel.z), -0.25f, 0.25f);
	if (spH > 0.03f) {
		float heading = atan2f(step.z, step.x);
		if (tr.headingValid) {
			float dh = heading - tr.lastHeading;
			while (dh > kGaxPi) dh -= 2.0f * kGaxPi;
			while (dh < -kGaxPi) dh += 2.0f * kGaxPi;
			tr.turnRate = 0.45f * tr.turnRate + 0.55f * GaxClampf(dh, -0.6f, 0.6f);
		}
		tr.lastHeading = heading;
		tr.headingValid = true;
	} else {
		tr.turnRate *= 0.5f;
		tr.headingValid = false;
	}

	// 3) prediccion adaptativa
	float bearingNow = atan2f(outEye.z - feetNow.z, outEye.x - feetNow.x);
	float vxPred = step.x, vyPred, vzPred = step.z;
	if (spH > 0.40f && tr.hasPrev) {
		// cruce volando: el vector gira con el cambio de rumbo relativo
		float db = bearingNow - tr.prevBearing;
		vxPred = step.x * cosf(db) - step.z * sinf(db);
		vzPred = step.x * sinf(db) + step.z * cosf(db);
	} else if (tr.RecentFlips() >= 2 && tr.flipPeriod >= 2.0f && tr.flipPeriod <= 16.0f) {
		// zigzag sobre el eje propio con particion sub-tick
		float ox = tr.oscAxisX, oz = tr.oscAxisZ, tx = -oz, tz = ox;
		float vOsc = step.x * ox + step.z * oz;
		float vTan = tr.velEma.x * tx + tr.velEma.z * tz;
		float s = vOsc >= 0.0f ? 1.0f : -1.0f;
		float spdOsc = fabsf(vOsc);
		float phase = (float)(tr.tick - tr.lastFlipTick) + 1.0f;
		float lead = 1.5f, rem = lead, disp = 0.0f;
		while (rem > 1e-4f) {
			float dt = std::min(1.0f, rem);
			rem -= dt;
			if (phase + dt > tr.flipPeriod) {
				float tBefore = std::max(0.0f, tr.flipPeriod - phase);
				float tAfter = dt - tBefore;
				disp += s * spdOsc * tBefore - s * spdOsc * tAfter;
				s = -s;
				phase = tAfter;
			} else {
				disp += s * spdOsc * dt;
				phase += dt;
			}
		}
		vxPred = ox * disp + tx * vTan * lead;
		vzPred = oz * disp + tz * vTan * lead;
	} else if (spH > 0.03f && fabsf(tr.turnRate) > 0.02f) {
		// orbita (contra FrontStrafe / Vortex / Berserk)
		float heading = atan2f(tr.velEma.z, tr.velEma.x) + tr.turnRate;
		float sp = sqrtf(tr.velEma.x * tr.velEma.x + tr.velEma.z * tr.velEma.z);
		vxPred = cosf(heading) * sp;
		vzPred = sinf(heading) * sp;
	} else {
		vxPred = tr.velEma.x + 0.35f * tr.accEma.x;
		vzPred = tr.velEma.z + 0.35f * tr.accEma.z;
	}

	// 4) descenso alternado de BypassFly (vy = -4.2997 con vx = vz = 0)
	if (step.y < -2.2f && spH < 0.06f && tr.hasPrev) {
		vyPred = -0.1266f;
		vxPred = tr.prevStep.x;
		vzPred = tr.prevStep.z;
	} else {
		vyPred = step.y;
	}
	if (spH > 0.02f || fabsf(step.y) > 0.02f) tr.prevStep = step;
	tr.prevFeet = feetNow;
	tr.prevBearing = bearingNow;
	tr.hasPrev = true;
	outPredFeet = {feetNow.x + vxPred, feetNow.y + vyPred, feetNow.z + vzPred};
	return true;
}

static void GaxRagnarok(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	GaxV3 pred, eye;
	float w = 0.6f, h = 1.8f;
	if (!GaxUpdateAndPredict(g_gaxTrRagnarok, target, lp, pred, w, h, eye)) return;
	float hw = std::max(0.05f, w * 0.5f - 0.07f);
	float cx = GaxClampf(eye.x, pred.x - hw, pred.x + hw);
	float cz = GaxClampf(eye.z, pred.z - hw, pred.z + hw);
	float cy = GaxClampf(eye.y, pred.y + 0.25f, pred.y + h - 0.22f);
	// micro-oscilacion aperiodica (proporcion aurea) dentro del hitbox
	float phi = fmodf((float)g_gaxTrRagnarok.tick * 0.61803398875f, 1.0f) * 2.0f * kGaxPi;
	float dx0 = pred.x - eye.x, dz0 = pred.z - eye.z;
	float d2 = sqrtf(dx0 * dx0 + dz0 * dz0);
	float nx = d2 > 0.05f ? -dz0 / d2 : 1.0f;
	float nz = d2 > 0.05f ? dx0 / d2 : 0.0f;
	float ax = GaxClampf(cx + nx * sinf(phi) * 0.05f, pred.x - hw, pred.x + hw);
	float az = GaxClampf(cz + nz * sinf(phi) * 0.05f, pred.z - hw, pred.z + hw);
	float ay = GaxClampf(cy + cosf(phi) * 0.05f, pred.y + 0.20f, pred.y + h - 0.18f);
	GaxWriteRot(c, ax - eye.x, ay - eye.y, az - eye.z);
}

static void GaxFenrir(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	GaxV3 pred, eye;
	float w = 0.6f, h = 1.8f;
	if (!GaxUpdateAndPredict(g_gaxTrFenrir, target, lp, pred, w, h, eye)) return;
	float hw = std::max(0.05f, w * 0.5f - 0.05f);
	float vx = g_gaxTrFenrir.velEma.x, vz = g_gaxTrFenrir.velEma.z;
	float sp = sqrtf(vx * vx + vz * vz);
	float leadX = sp > 0.04f ? (vx / sp) * 0.09f : 0.0f;
	float leadZ = sp > 0.04f ? (vz / sp) * 0.09f : 0.0f;
	float ax = GaxClampf(GaxClampf(eye.x, pred.x - hw, pred.x + hw) + leadX, pred.x - hw, pred.x + hw);
	float az = GaxClampf(GaxClampf(eye.z, pred.z - hw, pred.z + hw) + leadZ, pred.z - hw, pred.z + hw);
	float ay = GaxClampf(eye.y, pred.y + 0.22f, pred.y + h - 0.20f);
	GaxWriteRot(c, ax - eye.x, ay - eye.y, az - eye.z);
}

// Eclipse: Ragnarok sin oscilacion con horizonte = ping/50 + adelanto. Gatyo
// no lee el ping, asi que es el caso "mundo local" del original (ping 0).
static void GaxEclipse(GaxRotCtx& c, Actor* target, LocalPlayer* lp) {
	GaxV3 pred, eye;
	float w = 0.6f, h = 1.8f;
	if (!GaxUpdateAndPredict(g_gaxTrEclipse, target, lp, pred, w, h, eye)) return;
	const GaxCounterTracker& tr = g_gaxTrEclipse;
	bool fastZigzag = tr.RecentFlips() >= 3 && tr.flipPeriod > 0.0f && tr.flipPeriod <= 6.0f;
	if (!fastZigzag) {
		float extra = c.eclipseLead >= 0.0f ? c.eclipseLead : 1.0f;
		float k = GaxClampf(extra, 1.0f, 4.0f) - 1.0f;
		pred.x += tr.velEma.x * k;
		pred.y += tr.velEma.y * k;
		pred.z += tr.velEma.z * k;
	}
	float hw = std::max(0.05f, w * 0.5f - 0.07f);
	float ax = GaxClampf(eye.x, pred.x - hw, pred.x + hw);
	float az = GaxClampf(eye.z, pred.z - hw, pred.z + hw);
	float ay = GaxClampf(eye.y, pred.y + 0.25f, pred.y + h - 0.22f);
	GaxWriteRot(c, ax - eye.x, ay - eye.y, az - eye.z);
}

// ---------------------------------------------------------------------------
// Modulo
// ---------------------------------------------------------------------------
static GaxRot g_gaxRot = {0, 0};      // rotOut {pitch, yaw}
static float g_gaxHeadYaw = 0.0f;     // headYawOut
static bool g_gaxApplyRotation = false;  // hay objetivo y el modo gira
static int g_gaxTickCtr = 0;
static AuraSkyCache g_gaxSky;
// copia del AABB del objetivo para dibujarlo: el Actor* puede morir antes del frame
static GaxBox g_gaxTvBox;
static bool g_gaxTvValid = false;

static void GaxCollectTargets(LocalPlayer* lp, Level* level, std::vector<Actor*>& targets, float range, bool mobs) {
	GaxV3 l = GaxPos(lp);
	auto inRange = [&](Actor* a) {
		GaxV3 p = GaxPos(a);
		float dx = l.x - p.x, dy = l.y - p.y, dz = l.z - p.z;
		return sqrtf(dx * dx + dy * dy + dz * dz) < range;
	};

	// jugadores: mismas listas que el KillAura de Gatyo (amigos, bots, antibot)
	std::vector<Player*> players;
	level->getGameplayUserManager()->forEachActivePlayer([&](Player& player) {
		players.push_back(&player);
		return true;
	}, true);
	Actor* npcActor = FakePlayerNpcActor();
	bool npcSeen = false;
	for (Player* player : players) {
		if ((Actor*)player == (Actor*)lp || !GaxValid(player)) continue;
		const std::string& name = player->getName();
		bool npc = FakePlayerIsNpc(player);
		npcSeen = npcSeen || npc;
		if (antiBot && !npc) {
			vec2_t dims = *player->getAABBDim();
			if (name.length() <= 1 || player->getNameTag().length() <= 1) continue;
			if (dims.y < 0.05f || dims.x < 0.05f || dims.y > 2.1f || dims.x > 10.9f) continue;
		}
		if (std::find(botList.begin(), botList.end(), name) != botList.end()) continue;
		if (std::find(friendList.begin(), friendList.end(), name) != friendList.end()) continue;
		if (!player->isAttackableGamemode() || (npc ? FakePlayerHealth() : player->getHealth()) == 0) continue;
		if (inRange(player)) targets.push_back(player);
	}

	// jugador falso: entra como un jugador mas (amigos incluidos). El NPC, solo
	// si el juego no lo dio entre los jugadores activos.
	bool fakeFriend = std::find(friendList.begin(), friendList.end(), std::string(kFakePlayerName)) != friendList.end();
	Actor* fake = FakePlayerActor();
	if (!npcSeen && npcActor && GaxValid(npcActor)) fake = npcActor;
	if (fake && !fakeFriend && inRange(fake)) targets.push_back(fake);

	if (mobs) {
		for (Mob* mob : level->getRuntimeActorList()) {
			if ((Actor*)mob == (Actor*)lp || !GaxValid(mob)) continue;
			if (!MobDetected(mob->getEntityTypeId()) || mob->getHealth() == 0) continue;
			if (inRange(mob)) targets.push_back(mob);
		}
	}

	std::sort(targets.begin(), targets.end(), [&](Actor* a, Actor* b) {
		GaxV3 pa = GaxPos(a), pb = GaxPos(b);
		float da = (pa.x - l.x) * (pa.x - l.x) + (pa.y - l.y) * (pa.y - l.y) + (pa.z - l.z) * (pa.z - l.z);
		float db = (pb.x - l.x) * (pb.x - l.x) + (pb.y - l.y) * (pb.y - l.y) + (pb.z - l.z) * (pb.z - l.z);
		return da < db;
	});
}

static void GaxAttack(LocalPlayer* lp, GameMode* gm, Actor* target) {
	if (gaxHurttime && ((Mob*)target)->getHurtTime() > 8) return;
	bool fake = GaxIsFake(target);
	for (int i = 0; i < gaxMultiplier; i++) {
		if (gaxRandomizeHit && (rand() % 100 + 1) > gaxHitChance) continue;
		if (!gaxNoSwing) lp->swing();
		if (fake) FakePlayerAuraHit(lp);  // no existe en el mundo: GameMode::attack no sabria que hacer
		else gm->attack(target, true);
	}
}

static void GaxResetState(LocalPlayer* lp) {
	g_gaxTickCtr = 0;
	g_gaxTvValid = false;
	g_gaxApplyRotation = false;
	g_gaxSky = AuraSkyCache();
	g_gaxFsTargetYaw.clear();
	if (lp && lp->getRotation()) {
		vec2_t* rot = lp->getRotation();
		g_gaxRot = {rot->x, rot->y};  // seed = rotacion actual del juego
		g_gaxHeadYaw = rot->y;
	}
}

// Desde normalTick, con el jugador y su mundo ya leidos.
static void GatoAuraXOnNormalTick(LocalPlayer* lp, Level* level, GameMode* gm, BlockSource* region) {
	static bool wasEnabled = false;
	if (!gatoAuraX || !level || !gm || !GaxValid(lp)) {
		if (wasEnabled) GaxResetState(nullptr);
		wasEnabled = false;
		g_gaxApplyRotation = false;
		g_gaxTvValid = false;
		return;
	}
	if (!wasEnabled) {
		GaxResetState(lp);
		wasEnabled = true;
	}
	GaxUpdateKeys(lp);

	std::vector<Actor*> targets;
	GaxCollectTargets(lp, level, targets, gaxRange, gaxAttackMobs);
	if (Actor* npc = FakePlayerNpcActor()) {
		// GATYO_PROBE: que ve el aura del NPC del jugador falso (cada 2 s)
		static int probeTicks = 0;
		if (++probeTicks >= 40) {
			probeTicks = 0;
			GaxV3 p = GaxPos(npc), l = GaxPos(lp), sim = FakePlayerNpcStep();
			const vec3_t* gameVel = (const vec3_t*)((char*)npc->getPosition() + 24);
			const GaxBox& gameBox = *(const GaxBox*)((char*)npc->getAABBDim() - 0x18);
			int index = (int)(std::find(targets.begin(), targets.end(), npc) - targets.begin());
			__android_log_print(ANDROID_LOG_INFO, "Gatyo",
				"aurax: npc idx %d/%d modo %d dist %.2f | pos %.2f %.2f %.2f | vel juego %.3f %.3f %.3f sim %.3f %.3f %.3f | "
				"aabb juego %.2f %.2f %.2f..%.2f %.2f %.2f | rot %.1f %.1f",
				index, (int)targets.size(), gaxRotationMode, sqrtf((p.x - l.x) * (p.x - l.x) + (p.y - l.y) * (p.y - l.y) + (p.z - l.z) * (p.z - l.z)),
				p.x, p.y, p.z, gameVel->x, gameVel->y, gameVel->z, sim.x, sim.y, sim.z,
				gameBox.lower.x, gameBox.lower.y, gameBox.lower.z, gameBox.upper.x, gameBox.upper.y, gameBox.upper.z, g_gaxRot.x, g_gaxRot.y);
		}
	}
	g_gaxTickCtr++;
	g_gaxTvValid = !targets.empty();
	if (g_gaxTvValid) g_gaxTvBox = GaxAABB(targets[0]);
	int mode = ImClamp(gaxRotationMode, 0, GAX_ROT_COUNT - 1);
	g_gaxApplyRotation = !targets.empty() && mode != GAX_ROT_NONE && mode != GAX_ROT_STRAFE;
	if (targets.empty() || g_gaxTickCtr < gaxInterval) return;

	// tridente (lluvia o agua sobre el objetivo) o mejor arma con Switch. En PC
	// el tridente cambia apenas llueve aunque no haya nadie; aca espera a que
	// haya a quien pegarle, para no sacarte el item de la mano porque si.
	Actor* target = targets[0];
	int slot = AuraWantedSlot(lp, region, target, gaxRainMode, gaxAutoTrident, gaxSwitchMode, g_gaxSky);
	bool restore = AuraSwitchTo(lp, slot, gaxSwitchMode);

	// rotacion base hacia targets[0] con el desfase vertical
	GaxV3 l = GaxPos(lp), aim = GaxPos(target);
	aim.y += gaxVertOffset;
	float dx = aim.x - l.x, dy = aim.y - l.y, dz = aim.z - l.z;
	float dist = sqrtf(dx * dx + dy * dy + dz * dz);
	float pitch = g_gaxRot.x, yaw = g_gaxRot.y;
	if (dist > 0.001f) {
		pitch = GaxWrap90(asinf(dy / dist) * -kGaxRad2Deg);
		yaw = GaxWrap180(-atan2f(dx, dz) * kGaxRad2Deg);
	}
	if (mode != GAX_ROT_NONE && mode != GAX_ROT_STRAFE) {
		g_gaxRot = {pitch, yaw};  // pre-store: los handlers parten de aca
		g_gaxHeadYaw = yaw;
	}

	GaxRotCtx ctx = {50.0f, 0.9f, 8.0f, 1.0f, 3.0f, 0.7f, 1.5f, 0.1f, 0.1f, 8, 20, gaxRandomizeHit, &g_gaxRot, &g_gaxHeadYaw, gaxEclipseLead};
	switch (mode) {
		case GAX_ROT_STRAFE: {
			// sway temporal (el original no lo aplica: ni cabeza ni paquete)
			float t = (float)(GaxClock() * 1000.0) * 0.1f;
			g_gaxRot = {pitch + sinf(t) * 10.0f, yaw + cosf(t) * 10.0f};
			break;
		}
		case GAX_ROT_SMOOTH: GaxSmooth(ctx, target, lp); break;
		case GAX_ROT_NEMESIS: GaxNemesis(ctx, target, lp); break;
		case GAX_ROT_VORTEX: GaxVortex(ctx, target, lp); break;
		case GAX_ROT_FRONTSTRAFE: GaxFrontStrafe(ctx, target, lp); break;
		case GAX_ROT_AIRHVH: GaxAirHvHPro(ctx, target, lp); break;
		case GAX_ROT_ATOMIC: GaxAtomic(ctx, target, lp); break;
		case GAX_ROT_ALPHA: GaxAlpha(ctx, target, lp); break;
		case GAX_ROT_SCORE: GaxScore(ctx, target, lp); break;
		case GAX_ROT_LEGION: GaxLegion(ctx, target, lp); break;
		case GAX_ROT_VALHALLA: GaxValhalla(ctx, target, lp); break;
		case GAX_ROT_BERSERK: GaxBerserk(ctx, target, lp); break;
		case GAX_ROT_RAGNAROK: GaxRagnarok(ctx, target, lp); break;
		case GAX_ROT_FENRIR: GaxFenrir(ctx, target, lp); break;
		case GAX_ROT_ECLIPSE: GaxEclipse(ctx, target, lp); break;
	}

	if (gaxTargetMode == 0) {
		GaxAttack(lp, gm, target);
	} else {
		for (Actor* t : targets) GaxAttack(lp, gm, t);
	}
	AuraSwitchRestore(lp, restore);
	g_gaxTickCtr = 0;
}

static bool GatoAuraXMovesCamera() {
	return g_gaxApplyRotation && gaxMoveCamera;
}

// Desde el hook Intersects del jugador: camara, cabeza y cuerpo.
static void GatoAuraXApplyLocalRotation(LocalPlayer* lp) {
	if (!g_gaxApplyRotation) return;
	if (gaxMoveCamera) {
		lp->setRotationX(g_gaxRot.x);
		lp->setRotationY(g_gaxRot.y);
	}
	lp->setYHeadRot(g_gaxHeadYaw);
	lp->setYBodyRotation(g_gaxHeadYaw);
}

// Desde el hook del PlayerAuthInputPacket (su write()).
static void GatoAuraXSpoofPacket(void* packet) {
	if (!g_gaxApplyRotation) return;
	#if defined(__aarch64__)
	*(float*)((char*)packet + 0x2C) = g_gaxRot.x;   // pitch
	*(float*)((char*)packet + 0x30) = g_gaxRot.y;   // yaw
	*(float*)((char*)packet + 0x40) = g_gaxHeadYaw; // "Player's Head Rotation"
	#endif
}

// Contorno del objetivo (Target Visualize), con la proyeccion del ESP.
static void RenderGatoAuraXTarget(ImDrawList* draw_list, vec3_t cameraPos, glm::vec2 axisFov, glm::mat4 viewMatrix) {
	if (!gatoAuraX || !gaxTargetVisualize || gaxRotationMode < 1 || !g_gaxTvValid) return;
	glm::vec2 displaySize(screenWidth, screenHeight);
	glm::vec3 camPos(cameraPos.x, cameraPos.y, cameraPos.z);
	const GaxBox& b = g_gaxTvBox;
	glm::vec3 corners[8] = {
		{b.lower.x, b.lower.y, b.lower.z}, {b.upper.x, b.lower.y, b.lower.z}, {b.upper.x, b.lower.y, b.upper.z}, {b.lower.x, b.lower.y, b.upper.z},
		{b.lower.x, b.upper.y, b.lower.z}, {b.upper.x, b.upper.y, b.lower.z}, {b.upper.x, b.upper.y, b.upper.z}, {b.lower.x, b.upper.y, b.upper.z},
	};
	glm::vec2 screen[8];
	for (int i = 0; i < 8; i++) {
		if (!OWorldToScreen(camPos, corners[i], screen[i], axisFov, displaySize, viewMatrix)) return;
	}
	static const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
	ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(gaxTvColor[0], gaxTvColor[1], gaxTvColor[2], gaxTvColor[3]));
	float thickness = ImMax(1.0f, 1.5f * scaleGlobal);
	for (const auto& e : edges) {
		draw_list->AddLine(ImVec2(screen[e[0]].x, screen[e[0]].y), ImVec2(screen[e[1]].x, screen[e[1]].y), color, thickness);
	}
}
