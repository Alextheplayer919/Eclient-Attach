#pragma once
// Jugador falso para probar combate y visuales, como el FakePlayer de
// GatoClient. Una simulacion propia (fisica, vida, armadura, totems) y dos
// formas de mostrarla:
//
// - NPC (la normal): se inyecta un AddPlayerPacket (netinject_gatyo.h) y el
//   juego crea un jugador de verdad del lado del cliente: su modelo, su
//   hitbox, sus animaciones y su nametag. Se le pega como a cualquiera (el
//   toque, el KillAura, GatoAuraX) y GameMode::attack lo desvia aca para
//   calcular el daño, sin mandarle nada al servidor. Se mueve con paquetes
//   MoveActorAbsolute, asi el juego lo interpola como a un jugador de un
//   servidor; el daño, el tótem y la muerte salen con ActorEvent. Lleva puesta
//   la armadura del ajuste, una espada y el tótem en la segunda mano.
// - Virtual (respaldo): si el juego no crea el NPC (no hay handler de red o no
//   acepta el paquete) se usa lo de antes: se agrega a las listas de Gatyo, se
//   dibuja dentro del juego (worldrender_gatyo.h) y GatoAuraX lo ve por un
//   actor de mentira.
//
// El servidor y los demas jugadores no lo ven en ningun caso.

enum FakeRequest { FAKE_REQ_HERE = 1, FAKE_REQ_HIT = 2, FAKE_REQ_PEARL = 4, FAKE_REQ_LOGOUT = 8 };
static std::atomic<int> g_fakeRequests{0};  // botones del menu -> hilo del juego

enum FakeMode { FAKE_MODE_NPC, FAKE_MODE_VIRTUAL };
static std::atomic<int> g_fakeMode{FAKE_MODE_NPC};

static const int kTypeEndCrystal = 71;  // ActorType::EnderCrystal

struct FakeState {
	bool spawned = false;
	vec3_t center = {0, 0, 0};  // camina en circulo alrededor de aca
	vec3_t feet = {0, 0, 0};
	vec3_t vel = {0, 0, 0};
	vec3_t step = {0, 0, 0};  // lo que se movio en el ultimo tick: la velocidad que ven las auras
	bool onGround = false;
	float angle = 0.0f;
	float yaw = 0.0f, pitch = 0.0f;
	float health = 20.0f, absorption = 0.0f;
	int hurtCooldown = 0;     // invulnerabilidad de 10 ticks despues de un golpe
	int absorptionTicks = 0, regenTicks = 0, regenTimer = 0, naturalTimer = 0, gappleCooldown = 0;
	int totemsLeft = 0;
	int deadTicks = 0;        // 1..20 tirado con vida 0, despues desaparece hasta reaparecer
	int combatTick = 0;
	bool teleportNext = false;
	bool pearlActive = false;
	vec3_t pearlPos = {0, 0, 0}, pearlVel = {0, 0, 0};
	int pearlTicks = 0;
	uintptr_t pearlId = 0x10;  // ids ficticios, nunca direcciones reales
};
static FakeState g_fake;
static std::atomic<float> g_fakeYaw{0.0f};  // hacia donde mira, para el modelo virtual

// NPC (solo hilo del juego)
static uint64_t g_npcRuntimeId = 0;
static int64_t g_npcUniqueId = 0;
static uint64_t g_npcSerial = 0;
static bool g_npcSpawned = false;
static int g_npcWaitTicks = 0;
static Actor* g_npcActor = nullptr;  // del tick actual; se vuelve a buscar cada tick
static bool g_npcLoggedPos = false;
static GaxBox g_npcBox;  // hitbox de jugador alrededor de la posicion que dibuja el juego
static int g_npcEquipKey = -1;  // equipo que ya lleva puesto (material | tótem); -1 = nada todavia
static bool g_npcEquipLogged = false;

// cristales cerca del jugador falso, para detectar sus explosiones
struct FakeCrystal {
	uintptr_t id;
	vec3_t pos;
};
static std::vector<FakeCrystal> g_fakeCrystals;

static bool FakeVirtual() {
	return g_fakeMode.load() == FAKE_MODE_VIRTUAL;
}

static bool FakePresentAndAlive() {
	return fakePlayer && g_fake.spawned && !g_fakeLoggedOut && g_fake.deadTicks == 0;
}

bool FakePlayerHoldsTotem() {
	return fakePlayerInfinite || g_fake.totemsLeft > 0;
}

// El NPC de este tick, para que NormalTick y las auras usen la vida simulada.
bool FakePlayerIsNpc(Actor* actor) {
	return actor && !FakeVirtual() && actor == g_npcActor;
}

Actor* FakePlayerNpcActor() {
	return !FakeVirtual() && FakePresentAndAlive() ? g_npcActor : nullptr;
}

GaxV3 FakePlayerNpcStep() {
	return {g_fake.step.x, g_fake.step.y, g_fake.step.z};
}

const GaxBox& FakePlayerNpcBox() {
	return g_npcBox;
}

int FakePlayerHealth() {
	return g_fake.deadTicks > 0 ? 0 : (int)ceilf(g_fake.health);
}

int FakePlayerHurtTime() {
	return g_fake.hurtCooldown;
}

// ---------------------------------------------------------------------------
// Actor de mentira para GatoAuraX (solo en modo virtual). El aura lee directo
// de memoria la posicion (+0x208 -> {pos, posOld, vel}), la rotacion (+0x218)
// y el hurtTime (+0x194); esos campos apuntan a datos nuestros. Lo que en un
// actor real pasa por funciones del juego (hitbox, tipo, ataque) lo desvia el
// aura preguntando por FakePlayerActor.
// ---------------------------------------------------------------------------
struct FakeStateVector {
	vec3_t pos, posOld, vel;
};
alignas(16) static uint8_t g_fakeActorMem[0x400];
static FakeStateVector g_fakeStateVector;
static vec2_t g_fakeRotation = {0, 0};
static GaxBox g_fakeBox;
static bool g_fakeActorReady = false;

Actor* FakePlayerActor() {
	return FakeVirtual() && g_fakeActorReady && FakePresentAndAlive() ? (Actor*)g_fakeActorMem : nullptr;
}

const GaxBox& FakePlayerBox() {
	return g_fakeBox;
}

static void FakeUpdateActor() {
	const vec3_t& feet = g_fake.feet;
	vec3_t eyes = {feet.x, feet.y + 1.62f, feet.z};
	FakeStateVector& sv = g_fakeStateVector;
	sv.posOld = g_fakeActorReady ? sv.pos : eyes;
	sv.pos = eyes;
	sv.vel = g_fake.vel;
	g_fakeRotation = {g_fake.pitch, g_fake.yaw};
	g_fakeBox.lower = {feet.x - 0.3f, feet.y, feet.z - 0.3f};
	g_fakeBox.upper = {feet.x + 0.3f, feet.y + 1.8f, feet.z + 0.3f};
#if defined(__aarch64__)
	*(FakeStateVector**)(g_fakeActorMem + Fields64::GetPosition) = &g_fakeStateVector;
	*(vec2_t**)(g_fakeActorMem + Fields64::ActorRotation) = &g_fakeRotation;
	*(int*)(g_fakeActorMem + Fields64::GetHurtTime) = g_fake.hurtCooldown;
	g_fakeActorReady = true;
#endif
}

// ---------------------------------------------------------------------------
// NPC: aparecer, moverse, eventos
// ---------------------------------------------------------------------------
static vec3_t FakeEyes() {
	return {g_fake.feet.x, g_fake.feet.y + 1.62f, g_fake.feet.z};
}

static void NpcSpawn() {
#if defined(__aarch64__)
	if (!NetReady()) return;
	// ids altos y nuevos en cada aparicion, lejos de los que reparte el servidor
	g_npcSerial++;
	g_npcRuntimeId = 0x0047617479000000ull + g_npcSerial;
	g_npcUniqueId = (int64_t)g_npcRuntimeId;
	uint64_t uuidHi = ((uint64_t)rand() << 32) ^ (uint64_t)rand() ^ 0x4761747900000000ull;
	uint64_t uuidLo = ((uint64_t)rand() << 32) ^ (uint64_t)rand() ^ g_npcSerial;
	NetAddPlayer(g_npcRuntimeId, g_npcUniqueId, uuidHi, uuidLo, kFakePlayerName, g_fake.feet, g_fake.pitch, g_fake.yaw);
	g_npcSpawned = true;
	g_npcWaitTicks = 0;
	g_npcLoggedPos = false;
	g_npcEquipKey = -1;
	g_fake.teleportNext = true;
#endif
}

static void NpcRemove() {
#if defined(__aarch64__)
	if (g_npcSpawned && NetReady()) NetRemoveActor(g_npcUniqueId);
#endif
	g_npcSpawned = false;
	g_npcActor = nullptr;
}

static void FakeHurtEffects(bool damaged) {
#if defined(__aarch64__)
	if (FakeVirtual() || !g_npcSpawned) return;
	if (damaged) NetActorEvent(g_npcRuntimeId, NET_EVENT_HURT);
	NetPlayerSound(damaged ? NET_SOUND_HURT : NET_SOUND_ATTACK_NODAMAGE, FakeEyes());
#endif
}

static void FakePopEffects() {
	if (FakeVirtual() || !g_npcSpawned) {
		PopCounterOnPop(kFakePlayerName, false, false);
		return;
	}
#if defined(__aarch64__)
	// el PopCounter lo cuenta desde su hook del ActorEvent, como uno real
	NetActorEvent(g_npcRuntimeId, NET_EVENT_TOTEM);
#endif
}

static void FakeDeathEffects() {
	if (FakeVirtual() || !g_npcSpawned) {
		PopCounterOnDeath(kFakePlayerName, false, false);
		return;
	}
#if defined(__aarch64__)
	NetActorEvent(g_npcRuntimeId, NET_EVENT_DEATH);
	NetPlayerSound(NET_SOUND_DEATH, FakeEyes());
#endif
}

// ---------------------------------------------------------------------------
// Equipo del NPC: la armadura del ajuste, una espada del mismo material y el
// tótem en la segunda mano mientras le queden. Se le pone al actor con las
// mismas llamadas que hace el juego al recibir MobArmorEquipment (0x6DD40A8 ->
// 0xA8442EC) y MobEquipment (0x6DD3CD0) de un servidor, asi el modelo la lleva
// puesta y lo que lee equipo (TargetHUD, AcArmorOf) la ve como en un jugador.
// ---------------------------------------------------------------------------
#if defined(__aarch64__)
namespace FakeEquip {
	enum : uintptr_t {
		ItemStackFromName = 0xD3DF728,       // ItemStack(std::string_view, int cantidad, int aux, CompoundTag const*)
		PlayerInventorySetItem = 0xCE1128C,  // PlayerInventory::setItem(int, ItemStack const&, ContainerID, bool)
		EmutlsGetAddress = 0xFE12CF0,        // PLT de __emutls_get_address del juego
		ItemRegistryTls = 0x106704B8,        // thread_local weak_ptr<ItemRegistry> (ItemRegistryManager, 0xCF142F4)
	};
	static const int kSetArmor = 584 / 8;           // vtable de Actor: setArmor(ArmorSlot, ItemStack const&)
	static const int kSetEquippedSlot = 1368 / 8;   // vtable de Mob: setEquippedSlot(EquipmentSlot, ItemStack const&)
	static const int kOffhandSlot = 1;              // EquipmentSlot::Offhand
	static const int kItemStackBuffer = 0xC0;       // sizeof(ItemStack) = 0x98, con margen
}

// Por material (indice = fakePlayerArmor): casco, pechera, pantalones, botas y
// la espada de la mano.
static const char* const kNpcGear[4][5] = {
	{nullptr, nullptr, nullptr, nullptr, nullptr},
	{"minecraft:iron_helmet", "minecraft:iron_chestplate", "minecraft:iron_leggings", "minecraft:iron_boots", "minecraft:iron_sword"},
	{"minecraft:diamond_helmet", "minecraft:diamond_chestplate", "minecraft:diamond_leggings", "minecraft:diamond_boots", "minecraft:diamond_sword"},
	{"minecraft:netherite_helmet", "minecraft:netherite_chestplate", "minecraft:netherite_leggings", "minecraft:netherite_boots", "minecraft:netherite_sword"},
};

// El ItemStack por nombre busca el item en el registro del hilo; si el hilo no
// tiene uno, el juego aborta en vez de devolver un stack vacio. Se mira el
// weak_ptr sin tocar sus contadores: control presente y sin expirar.
static bool NpcItemRegistryReady() {
	void* slot = OffhandFn<void* (*)(void*)>(FakeEquip::EmutlsGetAddress)((void*)(OffhandBase() + FakeEquip::ItemRegistryTls));
	void* control = slot ? ((void**)slot)[1] : nullptr;
	return control && *(long*)((char*)control + 8) >= 0;  // __shared_owners_: -1 = expirado
}

// Arma el ItemStack `name` (vacio si es nullptr o no existe), se lo pasa a
// `put` y lo destruye. Devuelve si el item existia.
template <typename Put>
static bool NpcWithStack(const char* name, Put put) {
	void* empty = (void*)(OffhandBase() + GatyoInventory::ItemStackEmpty);
	if (!name) {
		put(empty);
		return true;
	}
	alignas(16) uint8_t stack[FakeEquip::kItemStackBuffer];
	memset(stack, 0, sizeof(stack));
	OffhandFn<void* (*)(void*, const char*, size_t, int, int, void*)>(FakeEquip::ItemStackFromName)(stack, name, strlen(name), 1, 0, nullptr);
	bool found = !OffhandFn<bool (*)(void*)>(GatyoInventory::ItemStackIsNull)(stack);
	put(found ? (void*)stack : empty);
	((void (*)(void*))(*(void***)stack)[0])(stack);  // ~ItemStack
	return found;
}

// Cada tick con el NPC; solo hace algo cuando cambia el material o el tótem.
static void NpcEquip(Actor* npc) {
	int set = ImClamp(fakePlayerArmor, 0, 3);
	bool totem = FakePlayerHoldsTotem();
	int key = set | (totem ? 4 : 0);
	if (key == g_npcEquipKey) return;
	if (!NpcItemRegistryReady()) {
		if (!g_npcEquipLogged) {
			g_npcEquipLogged = true;
			__android_log_print(ANDROID_LOG_WARN, "Gatyo", "fakeplayer: sin registro de items en este hilo, el NPC va sin equipo");
		}
		return;
	}
	g_npcEquipKey = key;
	void** vtable = *(void***)npc;
	auto setArmor = (void (*)(Actor*, int, void*))vtable[FakeEquip::kSetArmor];
	auto setEquipped = (bool (*)(Actor*, int, void*))vtable[FakeEquip::kSetEquippedSlot];
	int found = 0;
	for (int slot = 0; slot < 4; slot++) {
		found += NpcWithStack(kNpcGear[set][slot], [&](void* s) { setArmor(npc, slot, s); });
	}
	// mano: el slot 0 de su inventario, que es el elegido de un jugador nuevo
	char* inventory = *(char**)((char*)npc + GatyoInventory::kPlayerInventory);
	if (inventory) {
		NpcWithStack(kNpcGear[set][4], [&](void* s) {
			OffhandFn<void (*)(void*, int, void*, uint8_t, bool)>(FakeEquip::PlayerInventorySetItem)(inventory, 0, s, GatyoInventory::kContainerInventory, false);
		});
	}
	NpcWithStack(totem ? "minecraft:totem_of_undying" : nullptr, [&](void* s) { setEquipped(npc, FakeEquip::kOffhandSlot, s); });
	if (!g_npcEquipLogged) {
		// GATYO_PROBE: el registro respondio y las piezas existen
		g_npcEquipLogged = true;
		__android_log_print(ANDROID_LOG_INFO, "Gatyo", "fakeplayer: equipo material %d, %d/4 piezas, tótem %d", set, found, totem ? 1 : 0);
	}
}
#endif

// ---------------------------------------------------------------------------
// Daño. Valores del juego: arma de la mano, critico x1.5 al caer, armadura con
// dureza y Proteccion (4% por nivel en cada pieza, hasta 80%).
// ---------------------------------------------------------------------------
static void FakeArmorStats(float& armor, float& toughness) {
	static const float kArmor[4] = {0.0f, 15.0f, 20.0f, 20.0f};      // nada, hierro, diamante, netherite
	static const float kToughness[4] = {0.0f, 0.0f, 8.0f, 12.0f};
	int set = ImClamp(fakePlayerArmor, 0, 3);
	armor = kArmor[set];
	toughness = kToughness[set];
}

static float FakeReduceDamage(float damage) {
	float armor, toughness;
	FakeArmorStats(armor, toughness);
	if (armor > 0.0f) {
		float effective = ImClamp(armor - damage / (2.0f + toughness / 4.0f), armor * 0.2f, 20.0f);
		damage *= 1.0f - effective / 25.0f;
		float epf = ImMin(20.0f, 4.0f * ImClamp(fakePlayerProtection, 0, 4));
		damage *= 1.0f - epf * 0.04f;
	}
	return ImMax(0.0f, damage);
}

// Daño del item en la mano: Item::getAttackDamage (slot 37; las espadas lo
// leen de +0x1C8, el item base da 0 = mano, 1 de daño).
static float FakeHeldDamage(LocalPlayer* lp) {
#if defined(__aarch64__)
	char* inventory = *(char**)((char*)lp + GatyoInventory::kPlayerInventory);
	if (!inventory) return 1.0f;
	int selected = *(int*)(inventory + 0x10);  // PlayerInventory: slot elegido de la barra
	void* container = *(void**)(inventory + GatyoInventory::kInventoryContainer);
	if (!container || selected < 0 || selected > 8) return 1.0f;
	void* stack = OffhandSlotItem(container, selected);
	if (!stack || OffhandFn<bool (*)(void*)>(GatyoInventory::ItemStackIsNull)(stack)) return 1.0f;
	void** weakItem = *(void***)((char*)stack + 8);  // WeakPtr<Item>
	void* item = weakItem ? *weakItem : nullptr;
	if (!item) return 1.0f;
	typedef int (*AttackDamage)(void*);
	int damage = (*(AttackDamage**)item)[37](item);
	return damage > 0 ? (float)damage : 1.0f;
#else
	return 1.0f;
#endif
}

static float FakeMeleeDamage(LocalPlayer* lp, bool* crit) {
	float damage = FakeHeldDamage(lp);
	// critico: cayendo despues de saltar
	const vec3_t* pos = lp->getPosition();
	float fallSpeed = pos ? ((const vec3_t*)((const char*)pos + 24))->y : 0.0f;
	bool critical = fallSpeed < -0.1f;
	if (crit) *crit = critical;
	return critical ? damage * 1.5f : damage;
}

// `from` empuja (golpe o explosion) salvo con "Sin empuje"; `forced` ignora la
// invulnerabilidad (boton Golpear). `knock` es la fuerza del empuje.
static void FakeApplyDamage(float raw, const vec3_t* from, bool forced, float knock = 0.4f) {
	if (!FakePresentAndAlive()) return;
	if (!forced && g_fake.hurtCooldown > 0) {
		FakeHurtEffects(false);
		return;
	}
	float damage = FakeReduceDamage(raw);
	float fromAbsorption = ImMin(g_fake.absorption, damage);
	g_fake.absorption -= fromAbsorption;
	g_fake.health -= damage - fromAbsorption;
	g_fake.hurtCooldown = 10;
	VisualsMarkAttacked(FakeVirtual() ? 1 : (uintptr_t)g_npcActor, kFakePlayerName);
	if (from && !fakePlayerNoKnockback) {
		float dx = g_fake.feet.x - from->x, dz = g_fake.feet.z - from->z;
		float length = ImMax(sqrtf(dx * dx + dz * dz), 0.01f);
		g_fake.vel.x = g_fake.vel.x * 0.5f + dx / length * knock;
		g_fake.vel.z = g_fake.vel.z * 0.5f + dz / length * knock;
		if (g_fake.onGround) g_fake.vel.y = ImMin(0.4f, g_fake.vel.y * 0.5f + knock);
	}
	FakeHurtEffects(true);
	if (g_fake.health > 0.0f) return;
	if (FakePlayerHoldsTotem()) {
		// tótem: 1 de vida, Absorcion II 5 s y Regeneracion II 40 s. Con vida
		// infinita no se gasta y vuelve con la vida llena, como en GatoClient.
		if (!fakePlayerInfinite) g_fake.totemsLeft--;
		g_fake.health = fakePlayerInfinite ? 20.0f : 1.0f;
		g_fake.absorption = 8.0f;
		g_fake.absorptionTicks = 100;
		g_fake.regenTicks = 800;
		FakePopEffects();
		return;
	}
	g_fake.health = 0.0f;
	g_fake.deadTicks = 1;
	FakeDeathEffects();
}

void CharmsOnFakeHit(const vec3_t& feet, int hurtTime);  // charms_gatyo.h

// Golpe tuyo (toque, auras, pelea simulada).
static void FakeMeleeHit(LocalPlayer* lp, bool forced) {
	bool crit = false;
	float damage = FakeMeleeDamage(lp, &crit);
	vec3_t eyes = *lp->getPosition();
	// el NPC ya paso por GameMode::attack; el virtual no tiene actor
	if (FakeVirtual()) CharmsOnFakeHit(g_fake.feet, forced ? 0 : g_fake.hurtCooldown);
	FakeApplyDamage(damage, &eyes, forced);
#if defined(__aarch64__)
	if (!FakeVirtual() && g_npcSpawned) NetPlayerSound(NET_SOUND_ATTACK_STRONG, FakeEyes());
#endif
}

// GameMode::attack (visuals_gatyo.h) cuando el objetivo es el NPC: el golpe
// se resuelve aca y no sigue al juego (no sale ningun paquete al servidor).
bool FakePlayerOnAttack(Actor* target) {
	LocalPlayer* lp = localPlayer;
	if (!lp || !FakePlayerIsNpc(target)) return false;
	FakeMeleeHit(lp, false);
	return true;
}

// Golpe de GatoAuraX al actor de mentira (modo virtual).
void FakePlayerAuraHit(LocalPlayer* lp) {
	FakeMeleeHit(lp, false);
}

// Explosion de cristal (poder 6): daño por distancia y exposicion, como el juego.
static float FakeExplosionExposure(BlockSource* region, const vec3_t& center) {
	if (!region) return 1.0f;
	int open = 0, total = 0;
	for (int ix = 0; ix <= 2; ix++) {
		for (int iy = 0; iy <= 2; iy++) {
			for (int iz = 0; iz <= 2; iz++) {
				vec3_t p = {g_fake.feet.x - 0.3f + 0.3f * ix, g_fake.feet.y + 0.9f * iy, g_fake.feet.z - 0.3f + 0.3f * iz};
				float dx = p.x - center.x, dy = p.y - center.y, dz = p.z - center.z;
				float length = sqrtf(dx * dx + dy * dy + dz * dz);
				int steps = ImMax(1, (int)(length / 0.3f));
				bool blocked = false;
				for (int s = 1; s < steps && !blocked; s++) {
					float t = (float)s / steps;
					vec3_ti b = {(int)floorf(center.x + dx * t), (int)floorf(center.y + dy * t), (int)floorf(center.z + dz * t)};
					Block* block = region->getBlock(b);
					blocked = block && !block->isAir();
				}
				open += blocked ? 0 : 1;
				total++;
			}
		}
	}
	return (float)open / total;
}

static void FakeExplosion(BlockSource* region, const vec3_t& center) {
	if (!FakePresentAndAlive()) return;
	const float doublePower = 12.0f;
	float dx = g_fake.feet.x - center.x, dy = g_fake.feet.y - center.y, dz = g_fake.feet.z - center.z;
	float distance = sqrtf(dx * dx + dy * dy + dz * dz) / doublePower;
	if (distance > 1.0f) return;
	float impact = (1.0f - distance) * FakeExplosionExposure(region, center);
	float damage = ((impact * impact + impact) / 2.0f) * 7.0f * doublePower + 1.0f;
	FakeApplyDamage(damage, &center, false, impact);
}

// Un cristal que estaba cerca y ya no esta exploto.
static void FakeTrackCrystals(BlockSource* region, const std::vector<EntityStruct>& entities) {
	std::vector<FakeCrystal> current;
	for (const EntityStruct& e : entities) {
		if (e.typeId != kTypeEndCrystal) continue;
		float dx = e.position.x - g_fake.feet.x, dz = e.position.z - g_fake.feet.z;
		if (dx * dx + dz * dz < 16.0f * 16.0f) current.push_back({e.id, e.position});
	}
	for (const FakeCrystal& old : g_fakeCrystals) {
		bool still = false;
		for (const FakeCrystal& c : current) still = still || c.id == old.id;
		if (!still) FakeExplosion(region, old.pos);
	}
	g_fakeCrystals.swap(current);
}

// ---------------------------------------------------------------------------
// Fisica: gravedad, choque con bloques, friccion y salto automatico
// ---------------------------------------------------------------------------
static bool FakeSolid(BlockSource* region, float x, float y, float z) {
	vec3_ti b = {(int)floorf(x), (int)floorf(y), (int)floorf(z)};
	Block* block = region->getBlock(b);
	return block && !block->isAir();
}

static bool FakeCornersSolid(BlockSource* region, float x, float y, float z) {
	const float h = 0.299f;
	return FakeSolid(region, x - h, y, z - h) || FakeSolid(region, x + h, y, z - h) ||
	       FakeSolid(region, x - h, y, z + h) || FakeSolid(region, x + h, y, z + h);
}

static bool FakeBodyBlocked(BlockSource* region, float x, float y, float z) {
	return FakeCornersSolid(region, x, y + 0.1f, z) || FakeCornersSolid(region, x, y + 1.0f, z) || FakeCornersSolid(region, x, y + 1.7f, z);
}

static void FakePhysicsTick(BlockSource* region, float dirX, float dirZ) {
	FakeState& f = g_fake;
	float accel = f.onGround ? 0.1f : 0.02f;
	f.vel.x += dirX * accel;
	f.vel.z += dirZ * accel;
	if (!region) return;
	// vertical
	float ny = f.feet.y + f.vel.y;
	if (f.vel.y <= 0.0f && FakeCornersSolid(region, f.feet.x, ny, f.feet.z)) {
		ny = floorf(ny) + 1.0f;  // arriba del bloque de abajo
		if (ny > f.feet.y + 0.6f) ny = f.feet.y;
		f.vel.y = 0.0f;
		f.onGround = true;
	} else if (f.vel.y > 0.0f && FakeCornersSolid(region, f.feet.x, ny + 1.8f, f.feet.z)) {
		ny = f.feet.y;
		f.vel.y = 0.0f;
		f.onGround = false;
	} else {
		f.onGround = false;
	}
	f.feet.y = ny;
	// horizontal, eje por eje; contra una pared salta si puede
	bool wantsMove = dirX != 0.0f || dirZ != 0.0f;
	float nx = f.feet.x + f.vel.x;
	if (FakeBodyBlocked(region, nx, f.feet.y, f.feet.z)) {
		if (f.onGround && wantsMove) f.vel.y = 0.42f;
		f.vel.x = 0.0f;
	} else {
		f.feet.x = nx;
	}
	float nz = f.feet.z + f.vel.z;
	if (FakeBodyBlocked(region, f.feet.x, f.feet.y, nz)) {
		if (f.onGround && wantsMove) f.vel.y = 0.42f;
		f.vel.z = 0.0f;
	} else {
		f.feet.z = nz;
	}
	float friction = f.onGround ? 0.546f : 0.91f;
	f.vel.x *= friction;
	f.vel.z *= friction;
	f.vel.y = (f.vel.y - 0.08f) * 0.98f;
}

// Primer bloque solido debajo (o el mismo y si no hay nada cerca).
static float FakeGroundBelow(BlockSource* region, float x, float y, float z) {
	if (!region) return y;
	for (int i = 0; i < 24; i++) {
		float probe = floorf(y) - i;
		if (FakeSolid(region, x, probe, z)) return probe + 1.0f;
	}
	return y;
}

static void FakeResetStats() {
	g_fake.health = 20.0f;
	g_fake.absorption = 0.0f;
	g_fake.absorptionTicks = g_fake.regenTicks = g_fake.regenTimer = g_fake.naturalTimer = g_fake.gappleCooldown = 0;
	g_fake.hurtCooldown = 0;
	g_fake.totemsLeft = fakePlayerTotems;
	g_fake.deadTicks = 0;
	g_fake.vel = {0, 0, 0};
}

static void FakeSpawnInFront(LocalPlayer* lp, BlockSource* region) {
	vec3_t eyes = *lp->getPosition();
	float yaw = lp->getRotation()->y;
	float rad = yaw * (float)M_PI / 180.0f;
	float x = eyes.x - sinf(rad) * 3.0f, z = eyes.z + cosf(rad) * 3.0f;
	float y = FakeGroundBelow(region, x, eyes.y - 0.62f, z);
	g_fake.center = {x, y, z};
	g_fake.feet = g_fake.center;
	g_fake.angle = 0.0f;
	g_fake.spawned = true;
	g_fake.teleportNext = true;
	g_fakeLoggedOut = false;
	FakeResetStats();
}

// Vida que vuelve sola, efectos y manzanas doradas.
static void FakeHealthTick() {
	FakeState& f = g_fake;
	if (f.hurtCooldown > 0) f.hurtCooldown--;
	if (f.absorptionTicks > 0 && --f.absorptionTicks == 0) f.absorption = 0.0f;
	if (f.gappleCooldown > 0) f.gappleCooldown--;
	if (f.regenTicks > 0) {
		// Regeneracion II: 1 de vida cada 25 ticks
		f.regenTicks--;
		if (++f.regenTimer >= 25) {
			f.regenTimer = 0;
			f.health = ImMin(20.0f, f.health + 1.0f);
		}
	} else if (f.health < 20.0f && ++f.naturalTimer >= 80) {
		f.naturalTimer = 0;
		f.health = ImMin(20.0f, f.health + 1.0f);
	}
	if (fakePlayerGapples && f.health <= 10.0f && f.gappleCooldown == 0) {
		// manzana dorada: Absorcion I 2 min y Regeneracion II 5 s
		f.absorption = ImMax(f.absorption, 4.0f);
		f.absorptionTicks = 2400;
		f.regenTicks = ImMax(f.regenTicks, 100);
		f.gappleCooldown = 120;
	}
}

// Perla hacia ti con la misma fisica que la prediccion; al caer, el jugador
// falso aparece donde cayo. Entra como entidad para el PearlTracker.
static void FakePearlTick(BlockSource* region, std::vector<EntityStruct>& entities) {
	if (!g_fake.pearlActive) return;
	vec3_t pos = g_fake.pearlPos;
	vec3_t next = {pos.x + g_fake.pearlVel.x, pos.y + g_fake.pearlVel.y, pos.z + g_fake.pearlVel.z};
	vec3_t lastAir = pos;
	bool landed = ++g_fake.pearlTicks > 200 || next.y < -80.0f;
	for (int i = 1; i <= 4 && !landed && region; i++) {
		float t = i / 4.0f;
		vec3_t sample = {pos.x + (next.x - pos.x) * t, pos.y + (next.y - pos.y) * t, pos.z + (next.z - pos.z) * t};
		if (FakeSolid(region, sample.x, sample.y, sample.z)) landed = true;
		else lastAir = sample;
	}
	if (landed) {
		g_fake.pearlActive = false;
		g_fake.center = {lastAir.x, floorf(lastAir.y), lastAir.z};
		g_fake.feet = g_fake.center;
		g_fake.vel = {0, 0, 0};
		g_fake.angle = 0.0f;
		g_fake.teleportNext = true;
		return;
	}
	g_fake.pearlPos = next;
	g_fake.pearlVel.x *= 0.99f;
	g_fake.pearlVel.y = g_fake.pearlVel.y * 0.99f - 0.03f;
	g_fake.pearlVel.z *= 0.99f;
	entities.push_back({g_fake.pearlId, kTypeEnderPearl, g_fake.pearlPos, {0.25f, 0.25f}});
}

void FakePlayerReset() {
	g_fake = FakeState();
	g_fakeLoggedOut = false;
	g_fakeRequests = 0;
	g_fakeMode = FAKE_MODE_NPC;
	g_npcSpawned = false;
	g_npcActor = nullptr;
	g_npcEquipKey = -1;
	g_fakeCrystals.clear();
}

// Desde NormalTick, despues de los jugadores reales y antes de los visuales.
void FakePlayerOnNormalTick(LocalPlayer* lp, BlockSource* region, std::vector<EntityStruct>& entities) {
	if (!fakePlayer) {
		if (g_fake.spawned) {
			NpcRemove();
			FakePlayerReset();
		}
		return;
	}
	int requests = g_fakeRequests.exchange(0);
	vec3_t eyes = *lp->getPosition();
	if (!g_fake.spawned || (requests & FAKE_REQ_HERE)) FakeSpawnInFront(lp, region);
	if (requests & FAKE_REQ_LOGOUT) g_fakeLoggedOut = !g_fakeLoggedOut;
	bool present = !g_fakeLoggedOut;
	vec3_t feetBefore = g_fake.feet;

	if (g_fake.deadTicks > 0) {
		// muerto: 1 s en el suelo, 3 s sin aparecer y vuelve con vida y totems
		if (++g_fake.deadTicks > 80) {
			FakeResetStats();
			g_fake.feet = g_fake.center;
			g_fake.teleportNext = true;
		}
	} else if (present) {
		FakeHealthTick();
		bool hit = (requests & FAKE_REQ_HIT) || (fakePlayerCombat && ++g_fake.combatTick >= 14);
		if (hit) {
			g_fake.combatTick = 0;
			FakeMeleeHit(lp, (requests & FAKE_REQ_HIT) != 0);
		}
		// camina en circulo alrededor del centro (si lo empujaron, vuelve)
		float dirX = 0.0f, dirZ = 0.0f;
		if (fakePlayerMove && !g_fake.pearlActive) {
			g_fake.angle += 0.035f;
			float tx = g_fake.center.x + cosf(g_fake.angle) * 2.5f, tz = g_fake.center.z + sinf(g_fake.angle) * 2.5f;
			float dx = tx - g_fake.feet.x, dz = tz - g_fake.feet.z;
			float length = sqrtf(dx * dx + dz * dz);
			if (length > 0.25f) {
				dirX = dx / length;
				dirZ = dz / length;
			}
		}
		FakePhysicsTick(region, dirX, dirZ);
		if (g_fake.feet.y < -90.0f) FakeSpawnInFront(lp, region);
		// te mira como un rival (yaw 0 = +Z)
		vec3_t own = FakeEyes();
		float dx = eyes.x - own.x, dy = eyes.y - own.y, dz = eyes.z - own.z;
		float flat = sqrtf(dx * dx + dz * dz);
		g_fake.yaw = atan2f(-dx, dz) * 180.0f / (float)M_PI;
		g_fake.pitch = -atan2f(dy, ImMax(flat, 0.01f)) * 180.0f / (float)M_PI;
		g_fakeYaw = g_fake.yaw;

		if ((requests & FAKE_REQ_PEARL) && !g_fake.pearlActive) {
			float length = ImMax(sqrtf(dx * dx + dy * dy + dz * dz), 0.01f);
			g_fake.pearlActive = true;
			g_fake.pearlTicks = 0;
			g_fake.pearlId++;
			g_fake.pearlPos = own;
			g_fake.pearlVel = {dx / length * 1.2f, dy / length * 1.2f + 0.25f, dz / length * 1.2f};
		}
		FakeTrackCrystals(region, entities);
	}
	// muerto o desconectado no mira cristales: los que se rompan mientras tanto
	// no le pueden explotar encima al volver
	if (g_fake.deadTicks > 0 || !present) g_fakeCrystals.clear();
	FakePearlTick(region, entities);
	if (g_fake.teleportNext) g_fake.step = {0, 0, 0};
	else g_fake.step = {g_fake.feet.x - feetBefore.x, g_fake.feet.y - feetBefore.y, g_fake.feet.z - feetBefore.z};
	FakeUpdateActor();

	bool visible = present && g_fake.deadTicks <= 20;
	if (!FakeVirtual()) {
#if defined(__aarch64__)
		Level* level = lp->getLevel();
		if (visible && !g_npcSpawned) NpcSpawn();
		if (!visible && g_npcSpawned) NpcRemove();
		g_npcActor = g_npcSpawned ? NetRuntimeEntity(level, g_npcRuntimeId) : nullptr;
		if (g_npcSpawned && !g_npcActor && ++g_npcWaitTicks > 20) {
			// el juego no lo creo: se sigue con el modelo virtual
			__android_log_print(ANDROID_LOG_WARN, "Gatyo", "fakeplayer: el NPC no aparecio, modo virtual");
			g_npcSpawned = false;
			g_fakeMode = FAKE_MODE_VIRTUAL;
		}
		if (g_npcActor) {
			g_npcWaitTicks = 0;
			if (const vec3_t* p = g_npcActor->getPosition()) {
				// la posicion de un jugador es la de los ojos
				g_npcBox.lower = {p->x - 0.3f, p->y - 1.62f, p->z - 0.3f};
				g_npcBox.upper = {p->x + 0.3f, p->y + 0.18f, p->z + 0.3f};
			}
			if (!g_npcLoggedPos) {
				// GATYO_PROBE: para comprobar si la posicion del actor es la de los ojos
				g_npcLoggedPos = true;
				vec3_t* p = g_npcActor->getPosition();
				if (p) __android_log_print(ANDROID_LOG_INFO, "Gatyo", "fakeplayer: npc pos %.2f %.2f %.2f, pies %.2f %.2f %.2f", p->x, p->y, p->z, g_fake.feet.x, g_fake.feet.y, g_fake.feet.z);
			}
			if (g_fake.deadTicks == 0) NetMoveActor(g_npcRuntimeId, FakeEyes(), g_fake.pitch, g_fake.yaw, g_fake.onGround, g_fake.teleportNext);
			g_fake.teleportNext = false;
			NpcEquip(g_npcActor);
		}
		if (!NetReady() && !g_npcSpawned && ++g_npcWaitTicks > 100) {
			// 5 s sin handler de red: modelo virtual
			__android_log_print(ANDROID_LOG_WARN, "Gatyo", "fakeplayer: sin handler de red, modo virtual");
			g_fakeMode = FAKE_MODE_VIRTUAL;
		}
#else
		g_fakeMode = FAKE_MODE_VIRTUAL;
#endif
		if (!FakeVirtual()) return;
	}

	// modo virtual: entra a las listas de Gatyo como un jugador mas
	if (!visible) return;
	bool isFriend = std::find(friendList.begin(), friendList.end(), std::string(kFakePlayerName)) != friendList.end();
	vec3_t head = FakeEyes();
	vec2_t hitBox = {0.6f, 1.8f};
	nearPlayerList.push_back({kFakePlayerName, "§dGatyo§fBot", head, hitBox, FakePlayerHealth(), g_fake.hurtCooldown, 7, false, isFriend, false});
	nearPlayerList.back().yaw = g_fake.yaw;
	auto pops = g_popCounts.find(kFakePlayerName);
	if (pops != g_popCounts.end()) nearPlayerList.back().pops = pops->second;
	if (isFriend) {
		if (playerNear) nearFriendList.push_back(kFakePlayerName);
	} else {
		nearPlayer.push_back(kFakePlayerName);
	}
}

// ---------------------------------------------------------------------------
// Modo virtual: dibujo
// ---------------------------------------------------------------------------
bool WorldRenderActive();  // worldrender_gatyo.h

bool FakePlayerVirtualVisible() {
	return fakePlayer && FakeVirtual();
}

// Nametag como el del juego: texto blanco sobre un recuadro oscuro.
static void FakeNametag(ImDrawList* dl, glm::vec3 feet, bool hurt) {
	if (esp && espNametag) return;  // ya lo dibuja el ESP
	ImVec2 s;
	if (!GatyoWorldToScreen(g_view, feet + glm::vec3(0.0f, 2.1f, 0.0f), s)) return;
	float font = ImGui::GetFontSize() * 0.9f;
	ImVec2 size = ImGui::CalcTextSize(kFakePlayerName);
	size.x *= font / ImGui::GetFontSize();
	size.y *= font / ImGui::GetFontSize();
	float pad = 4.0f * scaleGlobal;
	ImVec2 min(s.x - size.x * 0.5f - pad, s.y - size.y - pad * 1.5f);
	dl->AddRectFilled(min, ImVec2(s.x + size.x * 0.5f + pad, s.y), GatyoU32(ImVec4(0, 0, 0, 0.25f)));
	dl->AddText(nullptr, font, ImVec2(min.x + pad, min.y + pad * 0.75f), GatyoU32(hurt ? GatyoState::Danger : ImVec4(1, 1, 1, 1)), kFakePlayerName);
}

// Con el render dentro del juego activo solo va el nametag; si no arranca,
// queda este cuerpo de cajas solidas, sin profundidad.
void RenderFakePlayerBody(ImDrawList* dl) {
	if (!FakeVirtual()) return;  // el NPC lo dibuja el juego
	const PlayerStruct* fake = nullptr;
	for (const PlayerStruct& p : g_world.players) {
		if (p.nickname == kFakePlayerName) {
			fake = &p;
			break;
		}
	}
	if (!fake) return;
	glm::vec3 feet(fake->position.x, fake->position.y - 1.62f, fake->position.z);
	if (WorldRenderActive()) {
		FakeNametag(dl, feet, fake->hurtTime > 0 || fake->health <= 0);
		return;
	}
	float yaw = g_fakeYaw.load() * (float)M_PI / 180.0f;
	glm::vec3 forward(-sinf(yaw), 0.0f, cosf(yaw)), right(cosf(yaw), 0.0f, sinf(yaw)), up(0.0f, 1.0f, 0.0f);

	struct Part { float x0, x1, y0, y1, z0, z1; ImVec4 color; };
	ImVec4 skin = GatyoRgb(199, 150, 120), shirt = GatyoRgb(0, 168, 168), pants = GatyoRgb(60, 60, 160);
	ImVec4 eye = GatyoRgb(70, 50, 130), white = GatyoRgb(240, 240, 240);
	const Part parts[] = {
		{-0.25f, 0.0f, 0.0f, 0.75f, -0.125f, 0.125f, pants},     // piernas
		{0.0f, 0.25f, 0.0f, 0.75f, -0.125f, 0.125f, pants},
		{-0.25f, 0.25f, 0.75f, 1.5f, -0.125f, 0.125f, shirt},    // torso
		{-0.5f, -0.25f, 0.75f, 1.5f, -0.125f, 0.125f, skin},     // brazos
		{0.25f, 0.5f, 0.75f, 1.5f, -0.125f, 0.125f, skin},
		{-0.25f, 0.25f, 1.5f, 2.0f, -0.25f, 0.25f, skin},        // cabeza
		{-0.19f, -0.06f, 1.69f, 1.75f, 0.25f, 0.27f, white},     // ojos
		{-0.125f, -0.06f, 1.69f, 1.75f, 0.25f, 0.28f, eye},
		{0.06f, 0.19f, 1.69f, 1.75f, 0.25f, 0.27f, white},
		{0.06f, 0.125f, 1.69f, 1.75f, 0.25f, 0.28f, eye},
	};
	const float scale = 0.9f;
	bool hurt = fake->hurtTime > 0 || fake->health <= 0;
	struct Built { glm::vec3 corners[8]; ImVec4 color; float distance; };
	std::vector<Built> boxes;
	for (const Part& part : parts) {
		Built b;
		glm::vec3 local[8];
		GatyoBoxCorners(glm::vec3(part.x0, part.y0, part.z0), glm::vec3(part.x1, part.y1, part.z1), local);
		glm::vec3 center(0.0f);
		for (int i = 0; i < 8; i++) {
			glm::vec3 p = local[i] * scale;
			b.corners[i] = feet + right * p.x + up * p.y + forward * p.z;
			center += b.corners[i];
		}
		b.color = hurt ? GatyoMix(part.color, GatyoState::Danger, 0.55f) : part.color;
		b.distance = glm::length(center / 8.0f - g_view.cam);
		boxes.push_back(b);
	}
	// de la mas lejana a la mas cercana
	std::sort(boxes.begin(), boxes.end(), [](const Built& a, const Built& b) { return a.distance > b.distance; });
	ImU32 edge = GatyoU32(GatyoAlpha(GatyoState::Shadow, 0.55f));
	for (const Built& b : boxes) GatyoSolidBox3D(dl, g_view, b.corners, b.color, edge, ImMax(1.0f, scaleGlobal));

	// nombre y vida encima, si el nametag del ESP no lo esta dibujando ya
	if (!(esp && espNametag)) {
		char label[64];
		snprintf(label, sizeof(label), "%s  %d HP", kFakePlayerName, fake->health);
		ImVec4 color = hurt ? GatyoState::Danger : GatyoGetPalette().text;
		GatyoText3D(dl, g_view, feet + glm::vec3(0.0f, 2.15f, 0.0f), label, GatyoU32(color), ImGui::GetFontSize() * 0.9f);
	}
}

// Boton de ataque en modo virtual: raycast desde tus ojos hacia donde miras,
// contra la hitbox del jugador falso, cortado por el primer bloque.
static bool FakePlayerTryClickHit(LocalPlayer* lp) {
	if (!FakeVirtual() || !FakePresentAndAlive() || !g_fakeActorReady) return false;
	vec3_t eyes = *lp->getPosition();
	vec2_t rot = *lp->getRotation();
	float pitch = rot.x * (float)M_PI / 180.0f, yaw = rot.y * (float)M_PI / 180.0f;
	glm::vec3 origin(eyes.x, eyes.y, eyes.z);
	glm::vec3 dir(-sinf(yaw) * cosf(pitch), -sinf(pitch), cosf(yaw) * cosf(pitch));
	const float reach = 3.5f;
	// rayo contra la caja (metodo de las placas), un poco inflada como en el juego
	glm::vec3 lo(g_fakeBox.lower.x - 0.1f, g_fakeBox.lower.y - 0.1f, g_fakeBox.lower.z - 0.1f);
	glm::vec3 hi(g_fakeBox.upper.x + 0.1f, g_fakeBox.upper.y + 0.1f, g_fakeBox.upper.z + 0.1f);
	float tMin = 0.0f, tMax = reach;
	for (int i = 0; i < 3; i++) {
		if (fabsf(dir[i]) < 1e-6f) {
			if (origin[i] < lo[i] || origin[i] > hi[i]) return false;
			continue;
		}
		float t1 = (lo[i] - origin[i]) / dir[i], t2 = (hi[i] - origin[i]) / dir[i];
		if (t1 > t2) std::swap(t1, t2);
		tMin = ImMax(tMin, t1);
		tMax = ImMin(tMax, t2);
		if (tMin > tMax) return false;
	}
	// un bloque antes del jugador falso tapa el golpe
	BlockSource* region = lp->getDimensionBlockSource();
	for (float t = 0.0f; region && t < tMin; t += 0.1f) {
		glm::vec3 p = origin + dir * t;
		if (FakeSolid(region, p.x, p.y, p.z)) return false;
	}
	FakeMeleeHit(lp, false);
	lp->swing();
	return true;
}

#if defined(__aarch64__)
// ClientInputCallbacks::handleDestoryOrAttackButtonPress: en modo virtual el
// golpe al jugador falso sale de aca (el NPC no lo necesita: el juego lo ve).
uint64_t (*old_HandleDestroyOrAttack)(ClientInstance* ci);
uint64_t HandleDestroyOrAttack(ClientInstance* ci) {
	LocalPlayer* lp = localPlayer;
	if (!panic && fakePlayer && lp && !inMenu && FakePlayerTryClickHit(lp)) return 1;
	return old_HandleDestroyOrAttack(ci);
}
#endif

// ---------------------------------------------------------------------------
// Ajustes (pagina Jugadores y fila del ClickGUI). `narrow` pone cada control
// en su linea, para las ventanas del ClickGUI.
// ---------------------------------------------------------------------------
void DrawFakePlayerSettings(float width, bool narrow) {
	MutedText(T(S_TipFakePlayer));
	ModToggle(MOD_fakePlayer);
	if (!fakePlayer) return;
	if (FakeVirtual()) MutedText(T(S_FakeVirtualMode));
	ModToggle(MOD_fakePlayerMove);
	if (!narrow) ImGui::SameLine();
	ModToggle(MOD_fakePlayerCombat);
	if (!narrow) ImGui::SameLine();
	ModToggle(MOD_fakePlayerGapples);
	ModToggle(MOD_fakePlayerNoKnockback);
	if (!narrow) ImGui::SameLine();
	ModToggle(MOD_fakePlayerInfinite);
	ImGui::PushItemWidth(width);
	static const StrId armors[] = {S_ArmorNone, S_ArmorIron, S_ArmorDiamond, S_ArmorNetherite};
	if (narrow) MutedText(T(S_FakeArmor));
	ComboT(narrow ? "##fakearmor" : TL(S_FakeArmor), &fakePlayerArmor, armors, IM_ARRAYSIZE(armors));
	if (fakePlayerArmor > 0) {
		if (narrow) MutedText(T(S_FakeProtection));
		ImGui::SliderInt(narrow ? "##fakeprot" : TL(S_FakeProtection), &fakePlayerProtection, 0, 4);
	}
	if (!fakePlayerInfinite) {
		if (narrow) MutedText(T(S_FakeTotems));
		ImGui::SliderInt(narrow ? "##faketotems" : TL(S_FakeTotems), &fakePlayerTotems, 0, 10);
	}
	ImGui::PopItemWidth();
	ImVec2 size(narrow ? -1.0f : 0.0f, 0.0f);
	auto button = [&](StrId label, int request, bool last) {
		if (ImGui::Button(TL(label), size)) {
			g_fakeRequests.fetch_or(request);
			onFunctionSound = true;
		}
		if (!narrow && !last) ImGui::SameLine();
	};
	button(S_FakeBringHere, FAKE_REQ_HERE, false);
	button(S_FakeHit, FAKE_REQ_HIT, false);
	button(S_FakePearl, FAKE_REQ_PEARL, false);
	button(g_fakeLoggedOut ? S_FakeReconnect : S_FakeLogout, FAKE_REQ_LOGOUT, true);
}
