#pragma once
// Visuales portados de la seccion Render de GatoClient: TargetHUD,
// ItemCounter, PearlTracker, LogSpot, Waypoints, panel de informacion,
// sesion, bordes de chunk, tinte de pantalla, texto propio e imagen.
//
// Contrato de hilos (el mismo del TargetHUD original): todo lo que sale del
// juego se lee en el hilo del juego (VisualsOnNormalTick) y se publica en
// g_visualsShared con g_worldMutex tomado. El overlay copia eso a g_visuals
// una vez por frame (VisualsCopyFrame) y dibuja desde la copia, sin tocar
// punteros del juego.

#include "Images/items_gatyo.h"

#if defined(__aarch64__)
// Direcciones de libminecraftpe.so 1.21.111 arm64 (IDA). Los nombres son los
// HashedString de VanillaItemNames que arma la funcion de inicio de items,
// junto al de TotemOfUndying que ya usa el Offhand.
namespace GatyoItems {
	enum : uintptr_t {
		NameGoldenApple = 0x1071FCB8,           // VanillaItemNames::GoldenApple ("minecraft:golden_apple")
		NameEnchantedGoldenApple = 0x1071F678,  // VanillaItemNames::EnchantedGoldenApple
		NameEndCrystal = 0x1071F6A0,            // VanillaItemNames::EndCrystal
		NameEnderPearl = 0x1071F768,            // VanillaItemNames::EnderPearl
		NameExperienceBottle = 0x1071F7B8,      // VanillaItemNames::ExperienceBottle
	};
	static const int kStackCount = 0x22;  // ItemStackBase: cantidad (uint8); ver ItemStackBase::isNull (0xD3E3D80)
}
#endif

// ActorType de la perla de ender: 87 con la marca de proyectil (0x400000),
// igual que la bola de fuego (0x400055) que ya usa NormalTick.
static const int kTypeEnderPearl = 0x400057;
static const float kLogSpotMaxDistance = 48.0f;  // mas lejos, desaparecer suele ser salir del rango de vision

enum GatyoItem { ITEM_TOTEM, ITEM_GAPPLE, ITEM_EGAPPLE, ITEM_CRYSTAL, ITEM_PEARL, ITEM_XP, ITEM_KINDS };

static double GatyoNow() {
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Datos publicados por el hilo del juego
// ---------------------------------------------------------------------------
struct TargetSnap {
	bool valid = false;
	uintptr_t id = 0;
	std::string name;
	int health = 0, hurtTime = 0, pops = 0, selfHealth = 20;
	float distance = 0.0f;
	bool totem = false;
	vec3_t position = {0, 0, 0};  // ojos, para dibujar la tarjeta encima
};

struct PearlTrail {
	uintptr_t id;
	std::vector<vec3_t> points;     // posiciones vistas, una por tick
	std::vector<vec3_t> predicted;  // trayectoria calculada desde la ultima posicion
	bool hasLanding = false;
	vec3_t landing;
	std::string owner;
	bool alive = true;
	int deadTicks = 0;
};

struct LogSpotEntry {
	std::string name;
	vec3_t feet;
	vec2_t hitBox;
	double time;
};

struct SessionStats {
	double start = 0;
	int kills = 0, deaths = 0, pops = 0, streak = 0, bestStreak = 0;
	float distance = 0.0f;
};

struct VisualsSnapshot {
	TargetSnap target;
	int items[ITEM_KINDS] = {};
	std::vector<PearlTrail> pearls;
	std::vector<LogSpotEntry> logSpots;
	SessionStats session;
	float yaw = 0.0f, bps = 0.0f;
	vec3_t feet = {0, 0, 0};
	bool inWorld = false;
};

static VisualsSnapshot g_visualsShared;  // la escribe el juego con g_worldMutex
static VisualsSnapshot g_visuals;        // copia del overlay

// ---------------------------------------------------------------------------
// Estado del hilo del juego
// ---------------------------------------------------------------------------
static std::atomic<uintptr_t> g_lastAttackedId{0};
static std::atomic<double> g_lastAttackedTime{-100.0};
static std::string g_lastAttackedName;  // se resuelve en NormalTick comparando punteros, sin desreferenciar
static uintptr_t g_lastAttackedNameId = 0;  // a que golpe corresponde g_lastAttackedName
static TargetSnap g_target;
static int g_items[ITEM_KINDS] = {};
static int g_itemTick = 0;
static std::vector<PearlTrail> g_pearls;
static std::vector<LogSpotEntry> g_logSpots;
struct LogSeen {
	vec3_t feet;
	vec2_t hitBox;
	int health;
	float distance;
};
static std::unordered_map<std::string, LogSeen> g_logPrev;
static SessionStats g_session;
static std::unordered_set<std::string> g_sessionDead;  // ya contados como kill hasta que revivan
static bool g_selfDead = false;
static std::atomic<bool> g_sessionStarted{false};  // el boton del menu lo apaga desde el overlay
static vec3_t g_lastFeet = {0, 0, 0};
static bool g_hasLastFeet = false;
static float g_bps = 0.0f;

// Al salir del mundo (InMainMenu): se reinicia todo menos los waypoints.
void VisualsReset() {
	g_lastAttackedId = 0;
	g_lastAttackedName.clear();
	g_lastAttackedNameId = 0;
	g_target = TargetSnap();
	for (int& count : g_items) count = 0;
	g_pearls.clear();
	g_logSpots.clear();
	g_logPrev.clear();
	g_sessionDead.clear();
	g_sessionStarted = false;
	g_selfDead = false;
	g_hasLastFeet = false;
	g_bps = 0.0f;
	std::lock_guard<std::mutex> lock(g_worldMutex);
	g_worldShared = WorldSnapshot();
	g_visualsShared = VisualsSnapshot();
}

bool FakePlayerHoldsTotem();  // fakeplayer_gatyo.h

// Golpe que no pasa por GameMode::attack (el jugador falso).
static void VisualsMarkAttacked(uintptr_t id, const std::string& name) {
	g_lastAttackedId = id;
	g_lastAttackedNameId = id;
	g_lastAttackedName = name;
	g_lastAttackedTime = GatyoNow();
}

static bool VisualsAttackedRecently(const std::string& name, double seconds) {
	return !name.empty() && name == g_lastAttackedName && GatyoNow() - g_lastAttackedTime.load() <= seconds;
}

// Totem usado (PopCounter): cuenta para la sesion si es de alguien que atacaste.
void VisualsOnPop(const std::string& name, bool self) {
	if (self || !VisualsAttackedRecently(name, 8.0)) return;
	g_session.pops++;
}

void VisualsOnSelfDeath() {
	if (g_selfDead) return;
	g_selfDead = true;
	g_session.deaths++;
	g_session.streak = 0;
}

#if defined(__aarch64__)
static int ItemKindOf(void* stack) {
	static const uintptr_t names[ITEM_KINDS] = {
		GatyoInventory::NameTotem, GatyoItems::NameGoldenApple, GatyoItems::NameEnchantedGoldenApple,
		GatyoItems::NameEndCrystal, GatyoItems::NameEnderPearl, GatyoItems::NameExperienceBottle,
	};
	auto isInstance = OffhandFn<bool (*)(void*, void*, bool)>(GatyoInventory::ItemStackIsInstance);
	for (int i = 0; i < ITEM_KINDS; i++) {
		if (isInstance(stack, (void*)(OffhandBase() + names[i]), false)) return i;
	}
	return -1;
}

static void CountItems(LocalPlayer* lp) {
	int counts[ITEM_KINDS] = {};
	auto isNull = OffhandFn<bool (*)(void*)>(GatyoInventory::ItemStackIsNull);
	auto add = [&](void* stack) {
		if (!stack || isNull(stack)) return;
		int kind = ItemKindOf(stack);
		if (kind >= 0) counts[kind] += *(uint8_t*)((char*)stack + GatyoItems::kStackCount);
	};
	void* container = OffhandContainer(lp);
	if (container) {
		for (int i = 0; i < kOffhandSlots; i++) add(OffhandSlotItem(container, i));
	}
	add(OffhandCurrent(lp));
	memcpy(g_items, counts, sizeof(counts));
}

static bool HoldsTotem(Actor* actor) {
	return OffhandIs(OffhandFn<void* (*)(void*)>(GatyoInventory::ActorGetOffhandSlot)(actor), 0);
}
#endif

static float HorizontalDistance(const vec3_t& a, const vec3_t& b) {
	float dx = a.x - b.x, dz = a.z - b.z;
	return sqrtf(dx * dx + dz * dz);
}

static float Distance3(const vec3_t& a, const vec3_t& b) {
	float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
	return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void UpdateTarget(LocalPlayer* lp, const vec3_t& feet, const std::vector<Player*>& players) {
	g_target.valid = false;
	if (!targetHud) return;
	const PlayerStruct* best = nullptr;
	Player* bestActor = nullptr;
	bool attacked = GatyoNow() - g_lastAttackedTime.load() <= 6.0;
	float bestDistance = targetHudRange;
	for (const PlayerStruct& p : nearPlayerList) {
		if (p.isBot || p.isFriend) continue;
		vec3_t pFeet = {p.position.x, p.position.y - 1.62f, p.position.z};
		float distance = Distance3(feet, pFeet);
		// el atacado hace poco gana aunque haya otro mas cerca
		if (attacked && p.nickname == g_lastAttackedName) {
			best = &p;
			bestDistance = distance;
			break;
		}
		if (distance < bestDistance) {
			best = &p;
			bestDistance = distance;
		}
	}
	if (!best) return;
	for (Player* actor : players) {
		if (actor != (Player*)lp && actor->getName() == best->nickname) {
			bestActor = actor;
			break;
		}
	}
	bool fake = best->nickname == kFakePlayerName;
	g_target.valid = true;
	g_target.id = bestActor ? (uintptr_t)bestActor : 1;
	g_target.name = best->nickname;
	g_target.health = best->health;
	g_target.hurtTime = best->hurtTime;
	g_target.pops = best->pops;
	g_target.distance = bestDistance;
	g_target.position = best->position;
	g_target.selfHealth = lp->getHealth();
	g_target.totem = false;
#if defined(__aarch64__)
	if (bestActor) g_target.totem = HoldsTotem(bestActor);
#endif
	if (fake) g_target.totem = FakePlayerHoldsTotem();
}

// Simula la perla desde su ultima velocidad (gravedad 0.03 y friccion 0.99 por
// tick, como los proyectiles arrojables) hasta que toca un bloque.
static void PredictPearl(PearlTrail& trail, BlockSource* region) {
	trail.predicted.clear();
	trail.hasLanding = false;
	if (!region || trail.points.size() < 2) return;
	vec3_t pos = trail.points.back();
	const vec3_t& prev = trail.points[trail.points.size() - 2];
	vec3_t vel = {pos.x - prev.x, pos.y - prev.y, pos.z - prev.z};
	if (fabsf(vel.x) + fabsf(vel.y) + fabsf(vel.z) < 0.01f) return;
	trail.predicted.push_back(pos);
	for (int tick = 0; tick < 300; tick++) {
		vec3_t next = {pos.x + vel.x, pos.y + vel.y, pos.z + vel.z};
		float length = Distance3(pos, next);
		int steps = ImClamp((int)ceilf(length / 0.5f), 1, 16);
		for (int i = 1; i <= steps; i++) {
			float t = (float)i / steps;
			vec3_t sample = {pos.x + (next.x - pos.x) * t, pos.y + (next.y - pos.y) * t, pos.z + (next.z - pos.z) * t};
			vec3_ti block = {(int)floorf(sample.x), (int)floorf(sample.y), (int)floorf(sample.z)};
			Block* hit = region->getBlock(block);
			if (hit && !hit->isAir()) {
				trail.predicted.push_back(sample);
				trail.landing = sample;
				trail.hasLanding = true;
				return;
			}
		}
		pos = next;
		vel.x *= 0.99f;
		vel.y = vel.y * 0.99f - 0.03f;
		vel.z *= 0.99f;
		if (tick % 2 == 1) trail.predicted.push_back(pos);
		if (pos.y < -80.0f) return;
	}
}

static void UpdatePearls(const std::vector<EntityStruct>& entities, BlockSource* region, const vec3_t& selfEyes, const std::string& selfName) {
	if (!pearlTracker) {
		g_pearls.clear();
		return;
	}
	for (PearlTrail& trail : g_pearls) trail.alive = false;
	int tracked = 0;
	for (const EntityStruct& e : entities) {
		if (e.typeId != kTypeEnderPearl || tracked >= 12) continue;
		tracked++;
		PearlTrail* trail = nullptr;
		for (PearlTrail& t : g_pearls) {
			if (t.id == e.id) trail = &t;
		}
		if (!trail) {
			PearlTrail fresh;
			fresh.id = e.id;
			// el que la tiro: el jugador mas cercano a donde aparecio
			float bestDistance = Distance3(selfEyes, e.position);
			fresh.owner = selfName;
			for (const PlayerStruct& p : nearPlayerList) {
				vec3_t eyes = p.position;
				float distance = Distance3(eyes, e.position);
				if (distance < bestDistance) {
					bestDistance = distance;
					fresh.owner = p.nickname;
				}
			}
			if (bestDistance > 4.0f) fresh.owner.clear();
			g_pearls.push_back(fresh);
			trail = &g_pearls.back();
		}
		trail->alive = true;
		trail->deadTicks = 0;
		if (trail->points.empty() || Distance3(trail->points.back(), e.position) > 0.05f) {
			trail->points.push_back(e.position);
			if (trail->points.size() > 200) trail->points.erase(trail->points.begin());
		}
		if (pearlPredict) PredictPearl(*trail, region);
		else trail->predicted.clear();
	}
	for (PearlTrail& trail : g_pearls) {
		if (trail.alive) continue;
		if (trail.deadTicks == 0 && !trail.points.empty()) {
			// cayo: la marca queda donde se la vio por ultima vez
			trail.landing = trail.points.back();
			trail.hasLanding = true;
			trail.predicted.clear();
		}
		trail.deadTicks++;
	}
	g_pearls.erase(std::remove_if(g_pearls.begin(), g_pearls.end(), [](const PearlTrail& t) { return t.deadTicks > 40; }), g_pearls.end());
}

static void UpdateLogSpots(LocalPlayer* lp, const vec3_t& feet, bool teleported) {
	if (!logSpot) {
		g_logSpots.clear();
		g_logPrev.clear();
		return;
	}
	std::unordered_map<std::string, LogSeen> current;
	for (const PlayerStruct& p : nearPlayerList) {
		if (p.isBot) continue;
		vec3_t pFeet = {p.position.x, p.position.y - 1.62f, p.position.z};
		current[p.nickname] = {pFeet, p.hitBox, p.health, Distance3(feet, pFeet)};
	}
	std::vector<std::pair<std::string, LogSeen>> gone;
	for (const auto& prev : g_logPrev) {
		if (current.count(prev.first)) continue;
		// apagar el jugador falso no es una desconexion; "Desconectar" si
		if (prev.first == kFakePlayerName && !g_fakeLoggedOut) continue;
		if (prev.second.health > 0 && prev.second.distance <= kLogSpotMaxDistance) gone.push_back(prev);
	}
	// muchos a la vez o un salto tuyo: cambio de dimension o teletransporte, no desconexiones
	if (!teleported && gone.size() <= 3) {
		for (const auto& g : gone) {
			g_logSpots.erase(std::remove_if(g_logSpots.begin(), g_logSpots.end(), [&](const LogSpotEntry& e) { return e.name == g.first; }), g_logSpots.end());
			g_logSpots.push_back({g.first, g.second.feet, g.second.hitBox, GatyoNow()});
			if (logSpotNotify) lp->displayClientMessage(TF(S_LogSpotChat, g.first.c_str(), g.second.feet.x, g.second.feet.y, g.second.feet.z), "");
		}
	}
	double now = GatyoNow();
	g_logSpots.erase(std::remove_if(g_logSpots.begin(), g_logSpots.end(), [&](const LogSpotEntry& e) {
		return current.count(e.name) || now - e.time > logSpotMinutes * 60.0;
	}), g_logSpots.end());
	g_logPrev.swap(current);
}

static void UpdateSession(const vec3_t& feet, float moved, bool teleported) {
	if (!g_sessionStarted) {
		g_session = SessionStats();
		g_session.start = GatyoNow();
		g_sessionStarted = true;
	}
	if (!teleported) g_session.distance += moved;
	for (const PlayerStruct& p : nearPlayerList) {
		if (p.health > 0) {
			g_sessionDead.erase(p.nickname);
			continue;
		}
		if (g_sessionDead.count(p.nickname)) continue;
		g_sessionDead.insert(p.nickname);
		if (VisualsAttackedRecently(p.nickname, 8.0)) {
			g_session.kills++;
			g_session.streak++;
			g_session.bestStreak = ImMax(g_session.bestStreak, g_session.streak);
		}
	}
}

// Desde NormalTick, despues de armar nearPlayerList. `players` son los
// jugadores del mundo (para buscar al objetivo por puntero) y `entities` todas
// las entidades de este tick.
void VisualsOnNormalTick(LocalPlayer* lp, BlockSource* region, const std::vector<Player*>& players, const std::vector<EntityStruct>& entities) {
	g_selfDead = false;
	vec3_t eyes = *lp->getPosition();
	vec3_t feet = {eyes.x, eyes.y - 1.62f, eyes.z};
	float moved = g_hasLastFeet ? HorizontalDistance(feet, g_lastFeet) : 0.0f;
	bool teleported = g_hasLastFeet && Distance3(feet, g_lastFeet) > 10.0f;
	g_lastFeet = feet;
	g_hasLastFeet = true;
	if (!teleported) g_bps += (moved * 20.0f - g_bps) * 0.25f;

	// a quien apunta el ultimo golpe, comparando direcciones (nunca se lee el
	// puntero). Si fue un mob queda sin nombre, asi no se le atribuye a nadie.
	uintptr_t attackedId = g_lastAttackedId.load();
	if (attackedId != g_lastAttackedNameId) {
		g_lastAttackedNameId = attackedId;
		g_lastAttackedName.clear();
		for (Player* p : players) {
			if ((uintptr_t)p == attackedId) {
				g_lastAttackedName = p->getName();
				break;
			}
		}
	}

	UpdateTarget(lp, feet, players);
#if defined(__aarch64__)
	if (itemCounter && ++g_itemTick >= 4) {
		g_itemTick = 0;
		CountItems(lp);
	}
#endif
	UpdatePearls(entities, region, eyes, lp->getName());
	UpdateLogSpots(lp, feet, teleported);
	UpdateSession(feet, moved, teleported);

	std::lock_guard<std::mutex> lock(g_worldMutex);
	VisualsSnapshot& out = g_visualsShared;
	out.target = g_target;
	memcpy(out.items, g_items, sizeof(g_items));
	out.pearls = g_pearls;
	out.logSpots = g_logSpots;
	out.session = g_session;
	out.yaw = lp->getRotation()->y;
	out.bps = g_bps;
	out.feet = feet;
	out.inWorld = true;
}

// GameMode::_attack: por aca pasan el golpe manual y los de las auras. Solo
// cuenta el GameMode del jugador local (en un mundo local el servidor tambien
// ataca con el suyo, desde otro hilo).
#if defined(__aarch64__)
bool FakePlayerOnAttack(Actor* target);  // fakeplayer_gatyo.h: el NPC no sigue al juego
void CharmsOnAttack(Actor* target);      // charms_gatyo.h: particulas de golpe

bool (*old_GameMode_attack)(GameMode* gm, Actor* target, bool a3);
bool GameMode_attack(GameMode* gm, Actor* target, bool a3) {
	LocalPlayer* lp = localPlayer;
	if (!panic && target && lp && lp->getGameMode() == gm) {
		double now = GatyoNow();
		if (target->getEntityTypeId() == kAcCrystalType) {
			// romper un cristal no cambia con quien se pelea: sigue el ultimo
			// golpeado (si no, las kills y pops con cristal no contaban en la
			// sesion y el TargetHUD soltaba al atacado)
			if (now - g_lastAttackedTime.load() <= 8.0) g_lastAttackedTime = now;
		} else {
			g_lastAttackedId = (uintptr_t)target;
			g_lastAttackedTime = now;
			CharmsOnAttack(target);
		}
		AcNoteUserAttack((void*)target);  // la suite de cristales sincroniza con este golpe
		if (FakePlayerOnAttack(target)) return true;
	}
	return old_GameMode_attack(gm, target, a3);
}
#endif

// ---------------------------------------------------------------------------
// Overlay
// ---------------------------------------------------------------------------

// Una vez por frame, antes de dibujar: copia lo que publico el juego.
void VisualsCopyFrame() {
	{
		std::lock_guard<std::mutex> lock(g_worldMutex);
		g_world.players = g_worldShared.players;
		g_world.entities = g_worldShared.entities;
		g_world.crystals = g_worldShared.crystals;  // CrystalChams (overlay)
		g_world.nearCount = g_worldShared.nearCount;
		g_world.friendCount = g_worldShared.friendCount;
		g_visuals = g_visualsShared;
	}
	std::lock_guard<std::mutex> lock(g_oreMutex);
	g_world.ores = renderOresESP;
}

static ImU32 ColorU32(const float* rgb, float alpha) {
	return GatyoU32(ImVec4(rgb[0], rgb[1], rgb[2], alpha));
}

static GLuint ItemTexture(int kind) {
	static GLuint textures[ITEM_KINDS] = {};
	if (!textures[kind]) {
		static const unsigned char* data[ITEM_KINDS] = {ItemTotemIcon, ItemGappleIcon, ItemGappleIcon, ItemCrystalIcon, ItemPearlIcon, ItemXpBottleIcon};
		static const int sizes[ITEM_KINDS] = {sizeof(ItemTotemIcon), sizeof(ItemGappleIcon), sizeof(ItemGappleIcon), sizeof(ItemCrystalIcon), sizeof(ItemPearlIcon), sizeof(ItemXpBottleIcon)};
		textures[kind] = CreateTextureFromMemoryPNG(data[kind], sizes[kind]);
		if (textures[kind]) {
			// pixel art de 16x16: sin suavizado al agrandarlo
			glBindTexture(GL_TEXTURE_2D, textures[kind]);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}
	}
	return textures[kind];
}

static void DrawItemIcon(ImDrawList* dl, int kind, ImVec2 min, float size, float alpha) {
	ImVec2 max(min.x + size, min.y + size);
	dl->AddImage((ImTextureID)(intptr_t)ItemTexture(kind), min, max, ImVec2(0, 0), ImVec2(1, 1), GatyoU32(ImVec4(1, 1, 1, alpha)));
	// la encantada lleva un brillo violeta encima
	if (kind == ITEM_EGAPPLE) dl->AddImage((ImTextureID)(intptr_t)ItemTexture(kind), min, max, ImVec2(0, 0), ImVec2(1, 1), GatyoU32(ImVec4(0.8f, 0.45f, 1.0f, 0.55f * alpha)));
}

// ---- TargetHUD ----
// La posicion del objetivo llega a 20 por segundo; se interpola para que la
// tarjeta lo siga suave.
static glm::vec3 g_targetPrev, g_targetCur;
static double g_targetMoved = 0.0;
static uintptr_t g_targetPosId = 0;

static glm::vec3 TargetSmoothedEyes(const TargetSnap& t) {
	glm::vec3 eyes(t.position.x, t.position.y, t.position.z);
	double now = GatyoNow();
	if (t.id != g_targetPosId || glm::length(eyes - g_targetCur) > 6.0f) {
		g_targetPosId = t.id;
		g_targetPrev = g_targetCur = eyes;
		g_targetMoved = now;
	} else if (eyes != g_targetCur) {
		g_targetPrev = g_targetPrev + (g_targetCur - g_targetPrev) * ImSaturate((float)((now - g_targetMoved) / 0.05));
		g_targetCur = eyes;
		g_targetMoved = now;
	}
	return g_targetPrev + (g_targetCur - g_targetPrev) * ImSaturate((float)((now - g_targetMoved) / 0.05));
}

static TargetSnap g_targetShown;
static float g_targetAppear = 0.0f, g_targetHp = 0.0f, g_targetTrail = 0.0f, g_targetFlash = 0.0f;
static uintptr_t g_targetAnimId = 0;
static ImVec2 g_targetFollowPos;  // ultima posicion sobre la cabeza (para el fade al salir de pantalla)

static void RenderTargetHud(ImDrawList* dl, bool preview) {
	float dt = ImGui::GetIO().DeltaTime;
	const TargetSnap& live = g_visuals.target;
	bool inGame = !inMenu && g_visuals.inWorld;
	float s = scaleGlobal;
	ImVec2 size(320.0f * s, (targetHudCompare ? 112.0f : 88.0f) * s);
	// Siguiendo la cabeza: si el objetivo queda fuera de la pantalla (o
	// detras) la tarjeta se desvanece donde estaba. Antes se pegaba al borde
	// o saltaba a su lugar fijo y tapaba la pantalla.
	bool follow = targetHudFollow && !preview;
	bool onScreen = true;
	if (follow && live.valid && inGame) {
		ImVec2 head;
		glm::vec3 eyes = TargetSmoothedEyes(live);
		onScreen = g_viewValid && GatyoWorldToScreen(g_view, eyes + glm::vec3(0.0f, 0.55f, 0.0f), head) &&
				   head.x >= 0.0f && head.x <= screenWidth && head.y >= 0.0f && head.y <= screenHeight;
		if (onScreen) {
			float lift = (esp && espNametag ? 105.0f : 18.0f) * s;
			ImVec2 origin(head.x - size.x * 0.5f, head.y - lift - size.y);
			origin.x = ImClamp(origin.x, 0.0f, ImMax(0.0f, screenWidth - size.x));
			origin.y = ImClamp(origin.y, 0.0f, ImMax(0.0f, screenHeight - size.y));
			g_targetFollowPos = origin;
		}
	}
	if (live.valid && inGame && onScreen) g_targetShown = live;
	else if (preview && !g_targetShown.valid) {
		g_targetShown.valid = true;
		g_targetShown.name = "Steve";
		g_targetShown.health = 14;
		g_targetShown.selfHealth = 18;
		g_targetShown.pops = 2;
		g_targetShown.distance = 3.4f;
		g_targetShown.totem = true;
	}
	bool visible = (live.valid && inGame && onScreen) || preview;
	g_targetAppear = ImClamp(g_targetAppear + (visible ? dt : -dt) * 6.0f, 0.0f, 1.0f);
	if (g_targetAppear <= 0.0f || !g_targetShown.valid) {
		g_hudRects[HUD_TARGET].visible = false;
		return;
	}
	const TargetSnap& t = g_targetShown;
	// barra animada: la vida baja suave y el tramo perdido se vacia despues
	float hp = ImSaturate(t.health / 20.0f);
	if (t.id != g_targetAnimId) {
		g_targetAnimId = t.id;
		g_targetHp = g_targetTrail = hp;
	}
	g_targetHp += (hp - g_targetHp) * ImMin(1.0f, dt * 12.0f);
	if (g_targetTrail < g_targetHp) g_targetTrail = g_targetHp;
	else g_targetTrail += (g_targetHp - g_targetTrail) * ImMin(1.0f, dt * 2.5f);
	if (t.hurtTime >= 8) g_targetFlash = 1.0f;
	g_targetFlash = ImMax(0.0f, g_targetFlash - dt * 3.0f);

	float a = g_targetAppear;
	GatyoPalette palette = GatyoGetPalette();
	// encima de la cabeza (deja lugar al nametag del ESP si esta); si no sigue
	// la cabeza, en su lugar del editor de HUD
	ImVec2 origin = follow ? g_targetFollowPos : HudGetPos(HUD_TARGET);
	ImVec2 max(origin.x + size.x, origin.y + size.y);
	float pad = 10.0f * s;
	float font = ImGui::GetFontSize();
	GatyoGlassPanel(dl, origin, max, a);
	if (g_targetFlash > 0.0f) dl->AddRect(origin, max, GatyoU32(GatyoAlpha(GatyoState::Danger, g_targetFlash * a)), 0.0f, 0, 2.0f * s);

	// nombre y distancia
	dl->AddText(nullptr, font, ImVec2(origin.x + pad, origin.y + pad), GatyoU32(GatyoAlpha(palette.text, a)), t.name.c_str());
	std::string distance = TF(S_TargetDistance, t.distance);
	float dw = HudTextWidth(distance.c_str(), font * 0.85f);
	dl->AddText(nullptr, font * 0.85f, ImVec2(max.x - pad - dw, origin.y + pad + font * 0.1f), GatyoU32(GatyoAlpha(palette.muted, a)), distance.c_str());

	// barra de vida
	float barY = origin.y + pad + font + 8.0f * s;
	ImVec2 barMin(origin.x + pad, barY), barMax(max.x - pad, barY + 12.0f * s);
	float barW = barMax.x - barMin.x;
	dl->AddRectFilled(barMin, barMax, GatyoU32(GatyoAlpha(palette.off, 0.8f * a)), 3.0f * s);
	dl->AddRectFilled(barMin, ImVec2(barMin.x + barW * g_targetTrail, barMax.y), GatyoU32(GatyoAlpha(GatyoState::Danger, 0.55f * a)), 3.0f * s);
	ImVec4 hpColor = EspHealthColor(g_targetHp);
	hpColor.w = a;
	dl->AddRectFilled(barMin, ImVec2(barMin.x + barW * g_targetHp, barMax.y), GatyoU32(hpColor), 3.0f * s);

	// vida en numeros, totem y pops
	float rowY = barMax.y + 6.0f * s;
	char hpText[32];
	snprintf(hpText, sizeof(hpText), "%d / 20", t.health);
	dl->AddText(nullptr, font * 0.9f, ImVec2(origin.x + pad, rowY), GatyoU32(GatyoAlpha(palette.text, a)), hpText);
	if (targetHudTotems) {
		float x = max.x - pad;
		if (t.pops > 0) {
			std::string pops = TF(S_TargetPops, t.pops);
			float pw = HudTextWidth(pops.c_str(), font * 0.9f);
			x -= pw;
			dl->AddText(nullptr, font * 0.9f, ImVec2(x, rowY), GatyoU32(GatyoAlpha(GatyoState::Warning, a)), pops.c_str());
			x -= 6.0f * s;
		}
		if (t.totem) {
			float icon = font * 0.95f;
			DrawItemIcon(dl, ITEM_TOTEM, ImVec2(x - icon, rowY), icon, a);
		}
	}

	// quien va ganando
	if (targetHudCompare) {
		float compareY = rowY + font + 4.0f * s;
		StrId verdict = S_TargetEven;
		ImVec4 color = palette.muted;
		if (t.selfHealth > t.health) { verdict = S_TargetWinning; color = GatyoState::Healthy; }
		else if (t.selfHealth < t.health) { verdict = S_TargetLosing; color = GatyoState::Danger; }
		char line[96];
		snprintf(line, sizeof(line), "%s  ·  %d vs %d", T(verdict), t.selfHealth, t.health);
		dl->AddText(nullptr, font * 0.85f, ImVec2(origin.x + pad, compareY), GatyoU32(GatyoAlpha(color, a)), line);
	}
	HudRegister(HUD_TARGET, origin, max);
}

// ---- ItemCounter ----
static void RenderItemCounter(ImDrawList* dl, bool preview) {
	float s = scaleGlobal;
	float icon = 34.0f * s;
	float gap = 8.0f * s;
	float pad = 8.0f * s;
	std::vector<int> shown;
	for (int i = 0; i < ITEM_KINDS; i++) {
		if (!itemCounterHideZero || g_visuals.items[i] > 0 || preview) shown.push_back(i);
	}
	if (shown.empty()) {
		g_hudRects[HUD_ITEMS].visible = false;
		return;
	}
	int n = (int)shown.size();
	ImVec2 origin = HudGetPos(HUD_ITEMS);
	ImVec2 content = itemCounterVertical ? ImVec2(icon, n * icon + (n - 1) * gap) : ImVec2(n * icon + (n - 1) * gap, icon);
	ImVec2 max(origin.x + content.x + pad * 2.0f, origin.y + content.y + pad * 2.0f);
	GatyoGlassPanel(dl, origin, max, 1.0f, false);
	float font = 20.0f * s;
	ImU32 outline = GatyoU32(GatyoAlpha(GatyoState::Shadow, 0.9f));
	for (int k = 0; k < n; k++) {
		int kind = shown[k];
		float offset = k * (icon + gap);
		ImVec2 min(origin.x + pad + (itemCounterVertical ? 0 : offset), origin.y + pad + (itemCounterVertical ? offset : 0));
		int count = g_visuals.items[kind];
		DrawItemIcon(dl, kind, min, icon, count > 0 ? 1.0f : 0.35f);
		char text[16];
		snprintf(text, sizeof(text), "%d", count);
		float w = HudTextWidth(text, font);
		// la cantidad abajo a la derecha, como en el inventario
		DrawOutlinedText(dl, ImVec2(min.x + icon - w + 2.0f * s, min.y + icon - font + 3.0f * s), GatyoU32(GatyoGetPalette().text), text, font, outline);
	}
	HudRegister(HUD_ITEMS, origin, max);
}

// ---- Panel de informacion ----
static const char* DirectionName(float yaw) {
	// yaw de Bedrock: 0 mira al sur (+Z), 90 al oeste, 180 al norte, -90 al este
	int index = ((int)floorf(yaw / 90.0f + 0.5f) % 4 + 4) % 4;
	static const StrId names[4] = {S_DirSouth, S_DirWest, S_DirNorth, S_DirEast};
	return T(names[index]);
}

static void RenderInfoPanel(ImDrawList* dl) {
	std::vector<std::pair<std::string, ImVec4>> rows;
	GatyoPalette palette = GatyoGetPalette();
	if (hudInfoDirection) rows.push_back({TF(S_InfoFacing, DirectionName(g_visuals.yaw), g_visuals.yaw), palette.text});
	if (hudInfoBps) rows.push_back({TF(S_InfoBps, g_visuals.bps), palette.text});
	if (rows.empty()) {
		g_hudRects[HUD_INFO].visible = false;
		return;
	}
	HudTextPanel(dl, HUD_INFO, rows);
}

// ---- Sesion ----
static void RenderSessionPanel(ImDrawList* dl) {
	const SessionStats& st = g_visuals.session;
	GatyoPalette palette = GatyoGetPalette();
	int seconds = st.start > 0 ? (int)(GatyoNow() - st.start) : 0;
	char line[128];
	std::vector<std::pair<std::string, ImVec4>> rows;
	snprintf(line, sizeof(line), "%s  %d:%02d:%02d", T(S_SessionTime), seconds / 3600, seconds / 60 % 60, seconds % 60);
	rows.push_back({line, palette.text});
	float kd = st.deaths > 0 ? (float)st.kills / st.deaths : (float)st.kills;
	snprintf(line, sizeof(line), "%s %d  ·  %s %d  ·  %s %.2f", T(S_SessionKills), st.kills, T(S_SessionDeaths), st.deaths, T(S_SessionKd), kd);
	rows.push_back({line, palette.text});
	snprintf(line, sizeof(line), "%s %d  ·  %s %d (%d)", T(S_SessionPops), st.pops, T(S_SessionStreak), st.streak, st.bestStreak);
	rows.push_back({line, palette.muted});
	if (st.distance >= 1000.0f) snprintf(line, sizeof(line), "%s  %.2f km", T(S_SessionDistance), st.distance / 1000.0f);
	else snprintf(line, sizeof(line), "%s  %.0f m", T(S_SessionDistance), st.distance);
	rows.push_back({line, palette.muted});
	HudTextPanel(dl, HUD_SESSION, rows);
}

void VisualsResetSession() {
	// lo pide el menu (hilo del overlay); el juego reinicia en su proximo tick
	g_sessionStarted = false;
}

// ---- Texto propio ----
static void RenderCustomText(ImDrawList* dl) {
	std::string text;
	{
		std::lock_guard<std::mutex> lock(g_textMutex);
		text = customTextValue;
	}
	if (text.empty()) {
		g_hudRects[HUD_TEXT].visible = false;
		return;
	}
	float font = 30.0f * scaleGlobal * customTextSize;
	ImVec2 pos = HudGetPos(HUD_TEXT);
	ImVec2 size(HudTextWidth(text.c_str(), font), font);
	ImVec4 shadow = GatyoAlpha(GatyoState::Shadow, 0.55f);
	dl->AddText(nullptr, font, ImVec2(pos.x + 1.5f, pos.y + 1.5f), GatyoU32(shadow), text.c_str());
	int start = dl->VtxBuffer.Size;
	dl->AddText(nullptr, font, pos, GatyoU32(ImVec4(1, 1, 1, 1)), text.c_str());
	GatyoShadeVerts(dl, start, dl->VtxBuffer.Size, true);
	HudRegister(HUD_TEXT, pos, ImVec2(pos.x + size.x, pos.y + size.y));
}

// ---- Imagen ----
static GLuint g_hudImageTexture = 0;
static int g_hudImageW = 0, g_hudImageH = 0;
static bool g_hudImageTried = false;

void LoadHudImage() {
	g_hudImageTried = true;
	if (g_hudImageTexture) {
		glDeleteTextures(1, &g_hudImageTexture);
		g_hudImageTexture = 0;
	}
	g_hudImageW = g_hudImageH = 0;
	EnsureGatyoDirs();
	std::string data;
	for (const char* name : {"hud.png", "hud.jpg"}) {
		data = readFile(GatyoDir() + "/" + name);
		if (!data.empty()) break;
	}
	int w, h, channels;
	if (data.empty() || !stbi_info_from_memory((const stbi_uc*)data.data(), (int)data.size(), &w, &h, &channels)) return;
	g_hudImageTexture = CreateTextureFromMemoryPNG((const unsigned char*)data.data(), (int)data.size());
	if (g_hudImageTexture) {
		g_hudImageW = w;
		g_hudImageH = h;
	}
}

bool HudImageLoaded() {
	return g_hudImageTexture != 0;
}

static void RenderHudImage(ImDrawList* dl, bool preview) {
	if (!g_hudImageTried) LoadHudImage();
	ImVec2 pos = HudGetPos(HUD_IMAGE);
	if (!g_hudImageTexture) {
		if (!preview) {
			g_hudRects[HUD_IMAGE].visible = false;
			return;
		}
		// sin imagen: un recuadro para poder ubicarlo desde el editor
		ImVec2 max(pos.x + 160.0f * scaleGlobal, pos.y + 90.0f * scaleGlobal);
		GatyoGlassPanel(dl, pos, max, 1.0f, false);
		dl->AddText(ImVec2(pos.x + 8.0f * scaleGlobal, pos.y + 8.0f * scaleGlobal), GatyoU32(GatyoGetPalette().muted), T(S_ImageMissing));
		HudRegister(HUD_IMAGE, pos, max);
		return;
	}
	// el lado mas largo mide 240 px a escala 1
	float longest = (float)ImMax(g_hudImageW, g_hudImageH);
	float k = 240.0f * scaleGlobal * hudImageScale / ImMax(longest, 1.0f);
	ImVec2 max(pos.x + g_hudImageW * k, pos.y + g_hudImageH * k);
	dl->AddImage((ImTextureID)(intptr_t)g_hudImageTexture, pos, max, ImVec2(0, 0), ImVec2(1, 1), GatyoU32(ImVec4(1, 1, 1, hudImageOpacity)));
	HudRegister(HUD_IMAGE, pos, max);
}

// Elementos del HUD de este archivo; los llama RenderHud. `preview` los fuerza
// para ubicarlos desde el editor.
void RenderVisualHud(ImDrawList* dl, bool preview) {
	bool inGame = !inMenu;
	if (targetHud) RenderTargetHud(dl, preview);
	if (itemCounter && (inGame || preview)) RenderItemCounter(dl, preview);
	if (hudInfo && (inGame || preview)) RenderInfoPanel(dl);
	if (sessionHud && (inGame || preview)) RenderSessionPanel(dl);
	if (customText) RenderCustomText(dl);
	if (hudImage) RenderHudImage(dl, preview);
}

// ---- Overlays del mundo ----
void RenderScreenTint(ImDrawList* dl) {
	dl->AddRectFilled(ImVec2(0, 0), ImVec2(screenWidth, screenHeight), GatyoU32(ImVec4(screenTintColor[0], screenTintColor[1], screenTintColor[2], screenTintColor[3])));
}

static glm::vec3 ToGlm(const vec3_t& v) {
	return glm::vec3(v.x, v.y, v.z);
}

static void RenderPearls(ImDrawList* dl) {
	float thickness = 2.0f * scaleGlobal;
	for (const PearlTrail& trail : g_visuals.pearls) {
		float fade = trail.alive ? 1.0f : ImSaturate(1.0f - trail.deadTicks / 40.0f);
		ImU32 color = ColorU32(pearlColor, fade);
		ImU32 outline = GatyoU32(GatyoAlpha(GatyoState::Shadow, 0.7f * fade));
		for (size_t i = 1; i < trail.points.size(); i++) {
			GatyoLine3DOutlined(dl, g_view, ToGlm(trail.points[i - 1]), ToGlm(trail.points[i]), color, outline, thickness);
		}
		// lo que falta del vuelo, mas tenue
		ImU32 predicted = ColorU32(pearlColor, 0.45f * fade);
		for (size_t i = 1; i < trail.predicted.size(); i++) {
			GatyoLine3D(dl, g_view, ToGlm(trail.predicted[i - 1]), ToGlm(trail.predicted[i]), predicted, thickness * 0.75f);
		}
		if (trail.hasLanding) {
			glm::vec3 p = ToGlm(trail.landing);
			GatyoBox3D(dl, g_view, p - glm::vec3(0.3f), p + glm::vec3(0.3f), color, thickness, ColorU32(pearlColor, 0.18f * fade), outline);
		}
		if (pearlOwner && !trail.owner.empty() && !trail.points.empty()) {
			glm::vec3 head = ToGlm(trail.alive ? trail.points.back() : trail.landing);
			GatyoText3D(dl, g_view, head + glm::vec3(0, 0.6f, 0), trail.owner.c_str(), GatyoU32(GatyoAlpha(GatyoGetPalette().text, fade)), ImGui::GetFontSize() * 0.85f);
		}
	}
}

static void RenderLogSpots(ImDrawList* dl) {
	double now = GatyoNow();
	float thickness = 2.0f * scaleGlobal;
	for (const LogSpotEntry& e : g_visuals.logSpots) {
		float half = ImMax(e.hitBox.x, 0.6f) * 0.5f;
		float height = e.hitBox.y > 0.1f ? e.hitBox.y : 1.8f;
		glm::vec3 lo(e.feet.x - half, e.feet.y, e.feet.z - half);
		glm::vec3 hi(e.feet.x + half, e.feet.y + height, e.feet.z + half);
		GatyoBox3D(dl, g_view, lo, hi, ColorU32(logSpotColor, 0.9f), thickness, ColorU32(logSpotColor, 0.16f), GatyoU32(GatyoAlpha(GatyoState::Shadow, 0.7f)));
		int ago = (int)(now - e.time);
		std::string label = TF(S_LogSpotAgo, e.name.c_str(), ago / 60, ago % 60);
		GatyoText3D(dl, g_view, glm::vec3(e.feet.x, e.feet.y + height + 0.35f, e.feet.z), label.c_str(), GatyoU32(GatyoGetPalette().text), ImGui::GetFontSize() * 0.85f);
	}
}

static void RenderWaypoints(ImDrawList* dl) {
	float font = ImGui::GetFontSize() * 0.9f;
	for (const Waypoint& w : waypointList) {
		glm::vec3 p(w.x, w.y, w.z);
		ImU32 color = ColorU32(w.color, 1.0f);
		// columna corta y rombo arriba
		GatyoLine3DOutlined(dl, g_view, p, p + glm::vec3(0, 2.0f, 0), color, GatyoU32(GatyoAlpha(GatyoState::Shadow, 0.6f)), 2.0f * scaleGlobal);
		ImVec2 top;
		if (!GatyoWorldToScreen(g_view, p + glm::vec3(0, 2.2f, 0), top)) continue;
		float r = 7.0f * scaleGlobal;
		dl->AddQuadFilled(ImVec2(top.x, top.y - r), ImVec2(top.x + r, top.y), ImVec2(top.x, top.y + r), ImVec2(top.x - r, top.y), color);
		dl->AddQuad(ImVec2(top.x, top.y - r), ImVec2(top.x + r, top.y), ImVec2(top.x, top.y + r), ImVec2(top.x - r, top.y), GatyoU32(GatyoAlpha(GatyoState::Shadow, 0.8f)), 1.5f);
		std::string label = w.name;
		if (waypointDistance) {
			vec3_t point = {w.x, w.y, w.z};
			char distance[24];
			snprintf(distance, sizeof(distance), " · %.0f m", Distance3(g_visuals.feet, point));
			label += distance;
		}
		GatyoText3D(dl, g_view, p + glm::vec3(0, 2.6f, 0), label.c_str(), GatyoU32(GatyoGetPalette().text), font);
	}
}

// Bordes del chunk actual (columnas en las esquinas y anillos cada 4 bloques)
// y las esquinas de los 8 vecinos, mas tenues.
static void RenderChunkBorders(ImDrawList* dl) {
	const vec3_t& feet = g_visuals.feet;
	float cx = floorf(feet.x / 16.0f) * 16.0f;
	float cz = floorf(feet.z / 16.0f) * 16.0f;
	float baseY = floorf(feet.y);
	float bottom = baseY - 16.0f, top = baseY + 24.0f;
	ImVec4 accent = GatyoVisualColor(ImVec2(screenWidth * 0.5f, screenHeight * 0.5f));
	ImU32 main = GatyoU32(GatyoAlpha(accent, 0.9f));
	ImU32 ring = GatyoU32(GatyoAlpha(accent, 0.45f));
	ImU32 neighbor = GatyoU32(GatyoAlpha(GatyoState::Warning, 0.55f));
	float thick = 1.6f * scaleGlobal;
	for (int dx = -1; dx <= 2; dx++) {
		for (int dz = -1; dz <= 2; dz++) {
			glm::vec3 base(cx + dx * 16.0f, bottom, cz + dz * 16.0f);
			bool own = (dx == 0 || dx == 1) && (dz == 0 || dz == 1);
			GatyoLine3D(dl, g_view, base, glm::vec3(base.x, top, base.z), own ? main : neighbor, own ? thick * 1.4f : thick);
		}
	}
	for (float y = baseY - 8.0f; y <= baseY + 16.0f; y += 4.0f) {
		glm::vec3 a(cx, y, cz), b(cx + 16, y, cz), c(cx + 16, y, cz + 16), d(cx, y, cz + 16);
		GatyoLine3D(dl, g_view, a, b, ring, thick);
		GatyoLine3D(dl, g_view, b, c, ring, thick);
		GatyoLine3D(dl, g_view, c, d, ring, thick);
		GatyoLine3D(dl, g_view, d, a, ring, thick);
	}
}

void RenderFakePlayerBody(ImDrawList* dl);  // fakeplayer_gatyo.h

void RenderVisualsWorld(ImDrawList* dl) {
	if (!g_visuals.inWorld) return;
	if (fakePlayer) RenderFakePlayerBody(dl);
	if (chunkBorders) RenderChunkBorders(dl);
	if (waypoints) RenderWaypoints(dl);
	if (logSpot) RenderLogSpots(dl);
	if (pearlTracker) RenderPearls(dl);
}

// ---------------------------------------------------------------------------
// Ajustes
// ---------------------------------------------------------------------------

void DrawWaypointSettings(float width) {
	ChatTextField(S_WaypointName, &waypointNameDraft, width);
	if (AccentButton(TL(S_WaypointAdd))) {
		Waypoint point;
		{
			std::lock_guard<std::mutex> lock(g_textMutex);
			point.name = waypointNameDraft;
			waypointNameDraft.clear();
		}
		if (point.name.empty()) point.name = TF(S_WaypointDefault, (int)waypointList.size() + 1);
		point.x = floorf(g_visuals.feet.x) + 0.5f;
		point.y = floorf(g_visuals.feet.y);
		point.z = floorf(g_visuals.feet.z) + 0.5f;
		ImVec4 accent = GatyoGetPalette().accent;
		point.color[0] = accent.x;
		point.color[1] = accent.y;
		point.color[2] = accent.z;
		waypointList.push_back(point);
		onFunctionSound = true;
	}
	if (waypointList.empty()) {
		MutedText(T(S_WaypointEmpty));
		return;
	}
	int remove = -1;
	for (int i = 0; i < (int)waypointList.size(); i++) {
		Waypoint& w = waypointList[i];
		ImGui::PushID(i);
		ImGui::ColorEdit3("##wpcolor", w.color, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%s  (%.0f, %.0f, %.0f)", w.name.c_str(), w.x, w.y, w.z);
		ImGui::SameLine();
		if (ImGui::Button(TL(S_Delete))) remove = i;
		ImGui::PopID();
	}
	if (remove >= 0) waypointList.erase(waypointList.begin() + remove);
}

void DrawHoleEspSettings(float width, bool narrow);  // worldesp_gatyo.h

// Pestaña "Mundo" de Visuales.
void DrawVisualsWorldTab(float u) {
	ImGui::PushItemWidth(u * 300);
	SectionTitle(S_SectionPearls);
	ModToggle(MOD_pearlTracker);
	if (pearlTracker) {
		ModToggle(MOD_pearlPredict);
		ImGui::SameLine();
		ModToggle(MOD_pearlOwner);
		ImGui::SameLine();
		ColorEditT(S_Color, pearlColor);
	}
	SectionTitle(S_SectionLogSpot);
	ModToggle(MOD_logSpot);
	if (logSpot) {
		ModToggle(MOD_logSpotNotify);
		ImGui::SameLine();
		ColorEditT(S_Color, logSpotColor);
		ImGui::SliderInt(TL(S_LogSpotMinutes), &logSpotMinutes, 1, 60);
	}
	SectionTitle(S_SectionWaypoints);
	ModToggle(MOD_waypoints);
	if (waypoints) {
		ImGui::SameLine();
		ModToggle(MOD_waypointDistance);
		DrawWaypointSettings(u * 300);
	}
	SectionTitle(S_SectionHoles);
	ModToggle(MOD_holeEsp);
	if (holeEsp) DrawHoleEspSettings(u * 300, false);
	SectionTitle(S_SectionWorldMisc);
	ModToggle(MOD_chunkBorders);
	ImGui::SameLine();
	ModToggle(MOD_screenTint);
	if (screenTint) {
		ImGui::SameLine();
		ImGui::ColorEdit4(TL(S_TintColor), screenTintColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
	}
	ImGui::PopItemWidth();
}

void DrawHudImageReload() {
	if (ImGui::Button(TL(S_ReloadImage))) {
		LoadHudImage();
		Notify(HudImageLoaded() ? T(S_ReloadImage) : T(S_ImageMissing), HudImageLoaded() ? NOTIFY_ON : NOTIFY_ERROR);
	}
	Tip(S_TipHudImage);
}

// Secciones nuevas de la pagina HUD (debajo de la lista de modulos).
void DrawVisualHudSettings(float u) {
	SectionTitle(S_SectionTarget);
	ImGui::PushItemWidth(u * 260);
	ModToggle(MOD_targetHud);
	if (targetHud) {
		ImGui::SameLine();
		ImGui::SliderFloat(TL(S_TargetRange), &targetHudRange, 3.0f, 30.0f, "%.0f");
		Tip(S_TipTargetRange);
		ModToggle(MOD_targetHudFollow);
		ImGui::SameLine();
		ModToggle(MOD_targetHudCompare);
		ImGui::SameLine();
		ModToggle(MOD_targetHudTotems);
	}

	SectionTitle(S_SectionItems);
	ModToggle(MOD_itemCounter);
	if (itemCounter) {
		ModToggle(MOD_itemCounterVertical);
		ImGui::SameLine();
		ModToggle(MOD_itemCounterHideZero);
	}

	SectionTitle(S_SectionInfo);
	ModToggle(MOD_hudInfo);
	if (hudInfo) {
		ModToggle(MOD_hudInfoDirection);
		ImGui::SameLine();
		ModToggle(MOD_hudInfoBps);
	}

	SectionTitle(S_SectionSession);
	ModToggle(MOD_sessionHud);
	if (sessionHud) {
		ImGui::SameLine();
		if (ImGui::Button(TL(S_SessionReset))) VisualsResetSession();
	}

	SectionTitle(S_SectionTextImage);
	ModToggle(MOD_customText);
	if (customText) {
		ChatTextField(S_CustomTextValue, &customTextValue, u * 260);
		ImGui::SliderFloat(TL(S_TextSize), &customTextSize, 0.5f, 3.0f, "%.2fx");
	}
	if (ModToggle(MOD_hudImage) && hudImage) LoadHudImage();
	if (hudImage) {
		ImGui::SliderFloat(TL(S_ImageScale), &hudImageScale, 0.1f, 4.0f, "%.2fx");
		ImGui::SameLine();
		ImGui::SliderFloat(TL(S_Opacity), &hudImageOpacity, 0.05f, 1.0f, "%.2f");
		DrawHudImageReload();
	}
	ImGui::PopItemWidth();
}
