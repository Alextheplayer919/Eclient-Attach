#pragma once
// Suite de cristales portada de GatoClient (AutoCrystal, AutoCrystalX,
// CrystalPlacer, CrystalHelper, AntiCrystal, CrystalChams). Aca vive la capa
// comun:
//
//   - offsets nuevos de libminecraftpe.so 1.21.111 arm64 (los de inventario y
//     paquetes se reusan del Offhand; los de aqui abajo van aparte para no
//     tocar offsets.h)
//   - helpers de actor/bloque/item sobre funciones ya probadas del cliente:
//     el AABB vive en getAABBDim() - 0x18 (GatoAuraX), el nombre del bloque es
//     la string de BlockType + 8 (igual que el BypassFly) y los items se
//     preguntan con ItemStackBase::isInstance
//   - coleccion y filtro de objetivos con las mismas listas del KillAura
//     (amigos, bots, antibot, FakePlayer*, isAttackableGamemode)
//   - exposicion estilo vanilla: BlockSource::getSeenPercent esta inlinada y
//     no se encontro, asi que se reimplementa con 27 muestras dentro del AABB
//     y un raycast a celdas, con cache por explosion
//   - formulas de danio de cristales: la directa (AutoCrystal) y la variante
//     con el paso a entero del original (AutoCrystalX)
//   - validez de colocacion: base obsidiana/bedrock, aire encima y jaula del
//     cristal libre de entidades (los cristales no cuentan)
//   - cambio de slot con separacion cliente/servidor: escritura en el slot del
//     PlayerInventory + MobEquipmentPacket, con restauracion al terminar
//   - envio de paquetes por la via probada del modulo Chat (ClientInstance
//     vtable +2400 -> PacketSender -> vtable +24), fallback LocalPlayer 0x738
//   - puente del useItemOn: el hook de GameMode::useItemOn avisa a
//     CrystalPlacer y CrystalHelper. Son callbacks de intencion: dentro del
//     hook nunca se coloca ni se golpea, para no volver a entrar
//   - spoof de rotacion para el PlayerAuthInputPacket (pitch 0x2C, yaw 0x30,
//     cabeza 0x40: el layout que ya verifico GatoAuraX)
//   - snapshots para el render overlay: el hilo de ImGui nunca toca punteros
//     del juego, solo copias bajo mutex
//
// ARM64-first: todos los offsets son del build arm64 de 1.21.111. En los demas
// ABIs cada funcion de esta capa queda en no-op con la misma firma, y cada
// modulo lleva su propio no-op, asi que main.cpp no guarda diferencias.

#if defined(__aarch64__)
#include <unordered_map>
#include <algorithm>

// ---------------------------------------------------------------------------
// Offsets nuevos de la suite (arm64)
// ---------------------------------------------------------------------------
namespace Ac {
	enum : uintptr_t {
		GameModeBuildBlock = 0xCCC22A4,   // GameMode::buildBlock(BlockPos const&, unsigned char, bool)
		ActorGetRuntimeID = 0xC938058,    // Actor::getRuntimeID()
		// MobEquipmentPacket(ActorRuntimeID, ItemStack const&, int slot, int selected, ContainerID):
		// arma el NetworkItemStackDescriptor en +0x38 y los campos de slot en +0x88..+0x93
		MobEquipmentCtor = 0xA847DD0,
		// ActorEquipmentComponent del EntityContext (Actor + 8): {offhand*, armadura*}.
		// Es el mismo getter que usa Actor::getOffhandSlot (0xC9486C8).
		ActorEquipment = 0xCDD7CF8,
		NameEndCrystal = 0x1071F6A0,      // VanillaItemNames::EndCrystal
		NameObsidian = 0x10745510,        // VanillaItemNames::Obsidian
		NameNetheriteBlock = 0x10745218,  // VanillaItemNames::NetheriteBlock (SelfTrap; misma construccion estatica que Obsidian)

		// La "comida" del gate de comer: manzanas doradas (y enchanted), que es
		// lo que se consume en medio de un pelea. getItemUseDuration sigue
		// pendiente de verificar; mientras tanto la ventana se arma cuando el
		// hook de useItemOn ve estos items en la mano.
		NameGoldenApple = 0x1071FCB8,     // VanillaItemNames::GoldenApple
		NameEnchantedApple = 0x1071F678,  // VanillaItemNames::EnchantedGoldenApple
	};
	static const int kPlayerInventory = 1392;       // Player -> PlayerInventory* (el del Offhand)
	static const int kSelectedSlot = 16;            // PlayerInventory + 0x10: slot elegido
	static const int kMobEquipmentSize = 0xA8;      // sizeof del paquete (0x94), con margen
	static const int kEquipmentArmor = 8;           // ActorEquipmentComponent + 8: contenedor de armadura
	static const int kItemArmorValue = 57;          // vtable de Item: getArmorValue() (ArmorItem lee +460)
	static const int kItemFullName = 0xE8 + 8;      // Item: std::string del nombre completo ("minecraft:trident")
}

static const int kAcHotbarSlots = 9;  // el slot elegido solo puede ser de la hotbar (0..8)

static const int kAcCrystalType = 71;   // ActorType del EndCrystal (vanilla)
static const int kAcStackCount = 0x22;  // ItemStackBase: cantidad (el offset del ItemCounter)
static const int kAcStackAux = 0x20;    // ItemStackBase: valor auxiliar (int16, justo antes de la cantidad)

// Reloj propio: GatyoNow vive en visuals_gatyo.h, que se incluye despues de
// esta capa, y la capa comun no depende de el.
static double AcNow() {
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Contador de ticks de la suite. Los modulos lo avanzan al entrar al tick;
// como los seis pueden correr en el mismo frame, avanza una sola vez cada
// ~50 ms. Solo lo toca el hilo del juego (los snapshots si llevan mutex).
static uint32_t g_acTick = 0;
static double g_acLastAdv = 0.0;

static uint32_t AcAdvanceTick() {
	double now = AcNow();
	if (now - g_acLastAdv >= 0.048) {
		g_acLastAdv = now;
		g_acTick++;
	}
	return g_acTick;
}

static int32_t AcTicksSince(uint32_t then) {
	return (int32_t)(g_acTick - then);
}

static float AcWrap180(float a) {
	while (a > 180.0f) a -= 360.0f;
	while (a < -180.0f) a += 360.0f;
	return a;
}
static float AcWrap90(float a) {
	a = AcWrap180(a);
	return a > 90.0f ? 90.0f : (a < -90.0f ? -90.0f : a);
}

static float AcDist3F(float ax, float ay, float az, float bx, float by, float bz) {
	float dx = ax - bx, dy = ay - by, dz = az - bz;
	return sqrtf(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------------------
// Actor
// ---------------------------------------------------------------------------
static uint64_t AcRuntimeId(Actor* a) {
	return OffhandFn<uint64_t (*)(Actor*)>(Ac::ActorGetRuntimeID)(a);
}

static bool AcIsCrystal(Actor* a) {
	return a && a->getEntityTypeId() == kAcCrystalType && GaxValid(a);
}

// ---------------------------------------------------------------------------
// Bloques: nombre, aire, material
// ---------------------------------------------------------------------------
// Nombre del bloque: la std::string que vive en BlockType + 8 ("tile.obsidian"),
// leida igual que el BypassFly y el Xray: como std::string, porque el juego y
// el cliente comparten la libc++ del NDK. (Decodificarla a mano con otro layout
// fallaba con SSO y ningun bloque salia como base.)
// Se compara el nombre exacto tras el ultimo '.' o ':' para que
// "crying_obsidian" no cuente como obsidiana.
static bool AcBlockIs(Block* block, const char* id) {
	if (!block) return false;
	BlockType* type = block->getBlockTypeClassCustom();
	if (!type) return false;
	const std::string& name = *(const std::string*)((char*)type + 8);
	size_t cut = name.find_last_of(".:");
	return strcmp(name.c_str() + (cut == std::string::npos ? 0 : cut + 1), id) == 0;
}

static bool AcIsAir(Block* block) { return !block || block->isAir(); }

static Block* AcBlockAt(BlockSource* region, const vec3_ti& pos) {
	return region ? region->getBlock(pos) : nullptr;
}
static Block* AcBlockAt(BlockSource* region, int x, int y, int z) {
	vec3_ti p = {x, y, z};
	return AcBlockAt(region, p);
}

static bool AcIsObsidian(Block* block) { return AcBlockIs(block, "obsidian"); }
static bool AcIsBedrock(Block* block)  { return AcBlockIs(block, "bedrock"); }
static bool AcIsBase(Block* block)     { return AcIsObsidian(block) || AcIsBedrock(block); }

// ---------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------
// isInstance contra un VanillaItemNames; el End Crystal de Android es el item
// 814, no el 758 de PC, asi que nada se compara por ID numerico.
static bool AcStackIs(void* stack, uintptr_t nameAddr) {
	if (!stack) return false;
	if (OffhandFn<bool (*)(void*)>(GatyoInventory::ItemStackIsNull)(stack)) return false;
	return OffhandFn<bool (*)(void*, void*, bool)>(GatyoInventory::ItemStackIsInstance)(
		stack, (void*)(OffhandBase() + nameAddr), false);
}

// La cantidad es un byte: el de al lado (+0x23) es la marca de valido.
static int AcStackCount(void* stack) {
	if (!stack) return 0;
	return (int)*(uint8_t*)((char*)stack + kAcStackCount);
}

// Nombre del item sin el "minecraft:" ("diamond_sword"), "" si el slot esta
// vacio. Item::getFullItemName (0xD421C78) devuelve el HashedString de
// Item + 0xE8; su std::string va 8 bytes despues del hash (ver
// ItemStackBase::isInstance, 0xD3E11AC). Sirve para los items que no tienen
// un VanillaItemNames ubicado (armaduras, espadas, tridente...).
static const char* AcItemName(void* stack) {
	if (!stack || OffhandFn<bool (*)(void*)>(GatyoInventory::ItemStackIsNull)(stack)) return "";
	void** weak = *(void***)((char*)stack + 8);
	char* item = weak ? (char*)*weak : nullptr;
	if (!item) return "";
	const std::string& full = *(const std::string*)(item + Ac::kItemFullName);
	size_t cut = full.find(':');
	return full.c_str() + (cut == std::string::npos ? 0 : cut + 1);
}

// Valor auxiliar (el tipo de pocion, el color de la cama...).
static int AcStackAux(void* stack) {
	return stack ? (int)*(int16_t*)((char*)stack + kAcStackAux) : 0;
}

static bool AcIsEndCrystalItem(void* stack) { return AcStackIs(stack, Ac::NameEndCrystal); }
static bool AcIsObsidianItem(void* stack)   { return AcStackIs(stack, Ac::NameObsidian); }

// Armadura puesta: puntos (Item::getArmorValue de cada pieza) y piezas, lo que
// pide la reduccion de AutoCrystalX. El Item vive en ItemStackBase + 8 (un
// WeakPtr: puntero al contador, cuyo primer campo es el Item*). Solo para
// actores reales: el actor del jugador falso no tiene EntityContext.
static void AcArmorOf(Actor* a, int& points, int& pieces) {
	points = 0;
	pieces = 0;
	if (!a) return;
	char* equip = OffhandFn<char* (*)(void*)>(Ac::ActorEquipment)((char*)a + 8);
	void* armor = equip ? *(void**)(equip + Ac::kEquipmentArmor) : nullptr;
	if (!armor) return;
	for (int slot = 0; slot < 4; slot++) {
		void* stack = OffhandSlotItem(armor, slot);
		if (!stack || OffhandFn<bool (*)(void*)>(GatyoInventory::ItemStackIsNull)(stack)) continue;
		void** weak = *(void***)((char*)stack + 8);
		void* item = weak ? *weak : nullptr;
		if (!item) continue;
		points += ((int (*)(void*))(*(void***)item)[Ac::kItemArmorValue])(item);
		pieces++;
	}
}

// ---------------------------------------------------------------------------
// Inventario
// ---------------------------------------------------------------------------
static void* AcContainer(LocalPlayer* lp) { return OffhandContainer(lp); }

// Slot elegido del inventario (cliente): los mismos punteros del Offhand.
static int AcClientSlot(LocalPlayer* lp) {
	char* pi = *(char**)((char*)lp + Ac::kPlayerInventory);
	if (!pi) return -1;
	return *(int*)(pi + Ac::kSelectedSlot);
}

static void AcWriteClientSlot(LocalPlayer* lp, int slot) {
	char* pi = *(char**)((char*)lp + Ac::kPlayerInventory);
	if (!pi) return;
	*(int*)(pi + Ac::kSelectedSlot) = slot;
}

static void* AcItemInSlot(void* container, int slot) {
	return (container && slot >= 0 && slot < kOffhandSlots) ? OffhandSlotItem(container, slot) : nullptr;
}

// La "mano" es el item del slot elegido del inventario. getCarriedItem no esta
// verificado, y para lo que hacemos (colocar, golpear, cambiar) el estado que
// manda es el mismo slot que anuncia el MobEquipment.
static void* AcHeldItem(LocalPlayer* lp) {
	int slot = AcClientSlot(lp);
	return slot >= 0 ? AcItemInSlot(AcContainer(lp), slot) : nullptr;
}

// Solo la hotbar: escribir un slot de 9..35 como seleccionado deja la mano
// apuntando a un slot que el servidor no acepta.
static int AcFindHotbarItem(LocalPlayer* lp, uintptr_t nameAddr) {
	void* container = AcContainer(lp);
	if (!container) return -1;
	for (int i = 0; i < kAcHotbarSlots; i++) {
		if (AcStackIs(AcItemInSlot(container, i), nameAddr)) return i;
	}
	return -1;
}

static bool AcHasItem(LocalPlayer* lp, uintptr_t nameAddr) { return AcFindHotbarItem(lp, nameAddr) >= 0; }

// ---------------------------------------------------------------------------
// Envio de paquetes y cambio de slot
// ---------------------------------------------------------------------------
// Via probada del modulo Chat: la vtable del ClientInstance en +2400 trae el
// PacketSender y el send vive en la vtable del propio sender (+24). Si el CI
// no responde, fallback por el slot 0x738 de la vtable de LocalPlayer.
static void AcSendPacket(LocalPlayer* lp, void* ci, void* packet) {
	if (ci) {
		void** vt = *(void***)ci;
		void* sender = ((void* (*)(void*))vt[2400 / 8])(ci);
		if (sender) {
			((void (*)(void*, void*))(*(void***)sender)[24 / 8])(sender, packet);
			return;
		}
	}
	if (!lp) return;
	void** lvt = *(void***)lp;
	((void (*)(void*, void*))lvt[0x738 / 8])(lp, packet);
}

// MobEquipmentPacket en la pila con el constructor completo del juego, igual
// que lo arma el propio juego (0x5B41118): (this, runtime ID, ItemStack del
// slot, slot, seleccionado, contenedor 0 = inventario). El constructor deja la
// vtable, el descriptor de red del item y los campos de slot; al terminar se
// llama al destructor (vtable[0], sin delete) para soltar el descriptor.
static bool AcSendMobEquipment(LocalPlayer* lp, void* ci, int slot) {
	if (!lp || slot < 0 || slot >= kAcHotbarSlots) return false;
	alignas(16) uint8_t packet[Ac::kMobEquipmentSize];
	memset(packet, 0, sizeof(packet));
	void* stack = AcItemInSlot(AcContainer(lp), slot);
	void* item = stack ? stack : (void*)(OffhandBase() + GatyoInventory::ItemStackEmpty);
	OffhandFn<void* (*)(void*, uint64_t, void*, int, int, int)>(Ac::MobEquipmentCtor)(
		packet, AcRuntimeId(lp), item, slot, slot, 0);
	AcSendPacket(lp, ci, packet);
	((void (*)(void*))(*(void***)packet)[0])(packet);
	return true;
}

// Estado del cambio de slot compartido entre los modulos que colocan
// (AutoCrystal y AutoCrystalX; si los dos van a la vez, el ultimo manda).
struct AcSlots {
	int original = -1;    // slot del cliente al abrir el tick
	int server = -1;      // ultimo slot anunciado al servidor
	int switched = -1;    // slot del cambio de este tick (-1 = sin cambio)
	bool wroteClient = false;
};
static AcSlots g_acSlots;

enum AcSwitchMode {
	AC_SW_NONE = 0,    // solo usa el slot de la mano si ya tiene el item
	AC_SW_NORMAL = 1,  // slot del cliente + MobEquipment
	AC_SW_SILENT = 2,  // solo slot del cliente (el servidor no se entera)
	AC_SW_PACKET = 3,  // solo MobEquipment (en la practica tambien escribe el
					   // slot del cliente: el fallback de buildBlock lee la mano)
};

// Cada gesto parte del slot que el jugador tiene ahora: AcSlotsEnd deja
// cliente y servidor iguales, y si el jugador cambia de slot a mano el juego
// ya lo anuncia por su cuenta. (Guardar el original una sola vez hacia que
// cada restauracion volviera al slot del primer tick.)
static void AcSlotsBegin(LocalPlayer* lp) {
	int client = AcClientSlot(lp);
	g_acSlots.original = client;
	g_acSlots.server = client;
	g_acSlots.switched = -1;
	g_acSlots.wroteClient = false;
}

// Cambia al slot con el item pedido. Devuelve el slot (-1: no lo hay, o el
// modo NONE no lo tiene en la mano). SILENT solo escribe el slot del cliente,
// igual que en PC; PACKET anuncia con MobEquipment y tambien escribe el
// cliente, porque la colocacion por fallback (buildBlock) lee la mano.
static int AcSwitchSlot(LocalPlayer* lp, void* ci, int mode, uintptr_t nameAddr) {
	int client = AcClientSlot(lp);
	// si la mano ya lo tiene no se cambia nada (y NONE funciona aunque haya
	// otro stack del mismo item antes en la hotbar)
	bool inHand = client >= 0 && client < kAcHotbarSlots && AcStackIs(AcItemInSlot(AcContainer(lp), client), nameAddr);
	int slot = inHand ? client : AcFindHotbarItem(lp, nameAddr);
	if (slot < 0) return -1;
	if (mode == AC_SW_NONE) return client == slot ? slot : -1;
	if (client != slot) {
		AcWriteClientSlot(lp, slot);
		g_acSlots.wroteClient = true;
	}
	g_acSlots.switched = slot;
	if (mode != AC_SW_SILENT && g_acSlots.server != slot) {
		AcSendMobEquipment(lp, ci, slot);
		g_acSlots.server = slot;
	}
	return slot;
}

// Devuelve el slot al estado previo: tanto el cliente como el anuncio al
// servidor, para que no queden desincronizados al terminar el gesto.
static void AcSlotsEnd(LocalPlayer* lp, void* ci) {
	AcSlots& s = g_acSlots;
	int target = s.original >= 0 ? s.original : s.server;
	if (target >= 0) {
		if (s.wroteClient && AcClientSlot(lp) != target) AcWriteClientSlot(lp, target);
		if (s.server >= 0 && s.server != target) {
			AcSendMobEquipment(lp, ci, target);
			s.server = target;
		}
	}
	s.switched = -1;
	s.wroteClient = false;
}

// Si un modulo queda colgado (apagado, mundo cambiado) su reset llama a esto.
static void AcSlotsForceRestore(LocalPlayer* lp, void* ci) {
	int now = AcClientSlot(lp);
	if (now >= 0 && g_acSlots.server >= 0 && g_acSlots.server != now) AcSendMobEquipment(lp, ci, now);
	g_acSlots.server = now;
	g_acSlots.original = now;
	g_acSlots.switched = -1;
	g_acSlots.wroteClient = false;
}

// ---------------------------------------------------------------------------
// Ventana de "comiendo": mientras algo entra por la boca, la suite deja de
// actuar. Sin getItemUseDuration verificado, la ventana se arma cuando el hook
// de useItemOn ve manzanas doradas (o enchanted) en la mano.
// ---------------------------------------------------------------------------
static std::atomic<uint32_t> g_acUseTick{0};

static bool AcHoldingFood(LocalPlayer* lp) {
	void* held = AcHeldItem(lp);
	return AcStackIs(held, Ac::NameGoldenApple) || AcStackIs(held, Ac::NameEnchantedApple);
}

static bool AcEatingActive(LocalPlayer* lp) {
	if (!AcHoldingFood(lp)) return false;
	return AcTicksSince(g_acUseTick.load()) <= 30;  // ~1.5 s de ventana
}

// Lo llama el hook del useItemOn (FastBridge en ARM64).
static void AcNoteItemUse(void* stack) {
	if (!localPlayer) return;
	if (AcStackIs(stack, Ac::NameGoldenApple) || AcStackIs(stack, Ac::NameEnchantedApple)) {
		g_acUseTick = g_acTick;
	}
}

// ---------------------------------------------------------------------------
// KA Sync: el golpe del usuario abre la ventana de los modulos. Lo llama el
// hook de GameMode::attack (visuals_gatyo.h). Los golpes internos de la suite
// se marcan con g_acInternalAttack para que no se suscriban a si mismos.
// ---------------------------------------------------------------------------
static std::atomic<bool> g_acInternalAttack{false};
static std::atomic<uint32_t> g_acUserAttackTick{0};

static void AcNoteUserAttack(void* actor) {
	(void)actor;
	if (g_acInternalAttack.load()) return;
	g_acUserAttackTick = g_acTick;
}

static bool AcSyncOpen(int postDelay) {
	return AcTicksSince(g_acUserAttackTick.load()) <= postDelay;
}

// Golpe interno de la suite (protege el KA Sync de si mismo).
static void AcAttack(GameMode* gm, Actor* target) {
	g_acInternalAttack = true;
	gm->attack(target, true);
	g_acInternalAttack = false;
}

// ---------------------------------------------------------------------------
// Rotacion: los modulos apuntan pitch/yaw y el hook del PlayerAuthInputPacket
// los escribe en el paquete. Layout verificado en GatoAuraX (0x2C/0x30/0x40).
// ---------------------------------------------------------------------------
static std::atomic<bool> g_crystalAimOn{false};
static std::atomic<float> g_crystalPitch{0.0f};
static std::atomic<float> g_crystalYaw{0.0f};
static std::atomic<uint32_t> g_crystalAimTick{0};

// Desde la posicion del jugador (en Bedrock ya esta a la altura de los ojos)
// hacia el punto a mirar. El punto llega como glm::vec3 (explosiones y celdas
// de colocacion viven en ese tipo).
static void AcAimAt(const GaxV3& eye, const glm::vec3& point) {
	float dx = point.x - eye.x, dy = point.y - eye.y, dz = point.z - eye.z;
	float dist = sqrtf(dx * dx + dy * dy + dz * dz);
	if (dist < 0.01f) return;
	g_crystalPitch = AcWrap90(asinf(dy / dist) * -57.295776f);
	g_crystalYaw = AcWrap180(-atan2f(dx, dz) * 57.295776f);
	g_crystalAimTick = g_acTick;
	g_crystalAimOn = true;
}

static void AcAimClear() { g_crystalAimOn = false; }

// Desde el hook del PlayerAuthInputPacket, despues del spoof de GatoAuraX.
static void CrystalApplySpoof(void* packet) {
	if (!g_crystalAimOn.load()) return;
	if (AcTicksSince(g_crystalAimTick.load()) > 2) {
		g_crystalAimOn = false;
		return;
	}
	*(float*)((char*)packet + 0x2C) = g_crystalPitch.load();
	*(float*)((char*)packet + 0x30) = g_crystalYaw.load();
	*(float*)((char*)packet + 0x40) = g_crystalYaw.load();
}

// ---------------------------------------------------------------------------
// Objetivos y entidades (mismas reglas de filtro que el KillAura de Gatyo)
// ---------------------------------------------------------------------------
struct AcEntity {
	Actor* actor = nullptr;   // puntero del juego, valido dentro del tick
	uint64_t rid = 0;         // runtime ID, vale entre ticks y para el render
	GaxV3 pos = {0, 0, 0};
	GaxBox box = {{0, 0, 0}, {0, 0, 0}};
	int health = 0;
	int armor = 0;            // puntos de armadura (reduccion de AutoCrystalX)
	int armorPieces = 0;
	bool fake = false;
	bool self = false;
	float dist = 0.0f;
};

// Armadura del jugador falso virtual: la del ajuste del FakePlayer (nada,
// hierro, diamante, netherite), las cuatro piezas puestas.
static void AcFakeArmor(AcEntity& e) {
	static const int kPoints[4] = {0, 15, 20, 20};
	int set = ImClamp(fakePlayerArmor, 0, 3);
	e.armor = kPoints[set];
	e.armorPieces = set > 0 ? 4 : 0;
}

// Entrada del propio jugador (danio propio y objetivo con acSelf / acxSelf).
static AcEntity AcSelfEntity(LocalPlayer* lp) {
	AcEntity e;
	e.actor = lp;
	e.rid = AcRuntimeId(lp);
	e.pos = GaxPos(lp);
	e.box = GaxAABB(lp);
	e.health = lp->getHealth();
	AcArmorOf(lp, e.armor, e.armorPieces);
	e.self = true;
	return e;
}

static float AcDistTo(const GaxV3& l, const GaxV3& t) {
	return sqrtf((l.x - t.x) * (l.x - t.x) + (l.y - t.y) * (l.y - t.y) + (l.z - t.z) * (l.z - t.z));
}

// Filtro jugador por jugador: copia fiel del cuerpo del GaxCollectTargets.
static bool AcCheckPlayer(Player* p, LocalPlayer* lp, AcEntity& out) {
	if ((Actor*)p == (Actor*)lp || !GaxValid(p)) return false;
	const std::string& name = p->getName();
	bool npc = FakePlayerIsNpc(p);
	if (antiBot && !npc) {
		vec2_t dims = *p->getAABBDim();
		if (name.length() <= 1 || p->getNameTag().length() <= 1) return false;
		if (dims.y < 0.05f || dims.x < 0.05f || dims.y > 2.1f || dims.x > 10.9f) return false;
	}
	if (std::find(botList.begin(), botList.end(), name) != botList.end()) return false;
	if (std::find(friendList.begin(), friendList.end(), name) != friendList.end()) return false;
	if (!p->isAttackableGamemode() || (npc ? FakePlayerHealth() : p->getHealth()) == 0) return false;
	out.actor = p;
	out.rid = AcRuntimeId(p);  // clave del cache de exposicion y del EMA de ACX
	if (npc) AcFakeArmor(out);  // el NPC lleva la vida y la armadura simuladas
	else AcArmorOf(p, out.armor, out.armorPieces);
	out.pos = GaxPos(p);
	out.box = GaxAABB(p);
	out.health = npc ? FakePlayerHealth() : p->getHealth();
	out.fake = npc || GaxIsFake(p);
	out.self = false;
	out.dist = 0.0f;
	return true;
}

// Objetivo = jugador + junto al jugador falso; los cristales van por su lista.
static std::vector<AcEntity> AcCollectTargets(LocalPlayer* lp, Level* level, float range, bool includeSelf) {
	std::vector<AcEntity> targets;
	if (!lp || !level || range <= 0.0f) return targets;
	GaxV3 l = GaxPos(lp);
	std::vector<Player*> players;
	level->getGameplayUserManager()->forEachActivePlayer([&](Player& player) {
		players.push_back(&player);
		return true;
	}, true);
	AcEntity e;
	for (Player* player : players) {
		if (!GaxValid(player)) continue;
		if ((Actor*)player == (Actor*)lp) {
			// El propio jugador solo entra como objetivo cuando un modulo lo
			// permite (acSelf / acxSelf: cristales para usted mismo).
			if (!includeSelf) continue;
			AcEntity me = AcSelfEntity(lp);
			if (me.health > 0) targets.push_back(me);
			continue;
		}
		if (!AcCheckPlayer(player, lp, e)) continue;
		e.dist = AcDistTo(l, e.pos);
		if (e.dist > range) continue;
		e.self = false;
		targets.push_back(e);
	}
	Actor* fake = FakePlayerActor();
	if (fake && std::find(friendList.begin(), friendList.end(), std::string(kFakePlayerName)) == friendList.end()) {
		e.actor = fake;
		e.rid = 0;  // los runtime ID reales nunca son 0
		AcFakeArmor(e);
		e.pos = GaxPos(fake);
		e.box = GaxAABB(fake);
		e.health = FakePlayerHealth();
		e.fake = true;
		e.self = false;
		e.dist = AcDistTo(l, e.pos);
		if (e.health > 0 && e.dist <= range) targets.push_back(e);
	}
	return targets;
}

// Cristales cerca del jugador (para romper).
static std::vector<AcEntity> AcCollectCrystals(LocalPlayer* lp, Level* level, float range) {
	std::vector<AcEntity> crystals;
	if (!lp || !level) return crystals;
	GaxV3 l = GaxPos(lp);
	for (Mob* mob : level->getRuntimeActorList()) {
		if (!AcIsCrystal(mob) || (Actor*)mob == (Actor*)lp) continue;
		AcEntity e;
		e.actor = mob;
		e.rid = AcRuntimeId(mob);
		e.pos = GaxPos(mob);
		e.box = GaxAABB(mob);
		e.dist = AcDistTo(l, e.pos);
		if (e.dist > range) continue;
		crystals.push_back(e);
	}
	return crystals;
}

// AABBs de todos los actores (sin cristales) para las revisiones de
// superposicion de la jaula del cristal. El jugador local cuenta, como en el
// canPlace del original: un cristal no entra donde uno esta parado.
static std::vector<GaxBox> AcCollectBoxes(Level* level, LocalPlayer* lp) {
	std::vector<GaxBox> boxes;
	if (!level) return boxes;
	(void)lp;
	for (Mob* mob : level->getRuntimeActorList()) {
		if (!GaxValid(mob)) continue;
		if (AcIsCrystal(mob)) continue;
		boxes.push_back(GaxAABB(mob));
	}
	return boxes;
}

// ---------------------------------------------------------------------------
// Exposicion: reimplementacion del getSeenPercent del juego (esta inlinado)
// ---------------------------------------------------------------------------
struct AcExpKey {
	uint64_t rid;
	int32_t x, y, z;
	bool operator==(const AcExpKey& o) const { return rid == o.rid && x == o.x && y == o.y && z == o.z; }
};
struct AcExpKeyHash {
	size_t operator()(const AcExpKey& k) const {
		size_t h = std::hash<uint64_t>()(k.rid);
		h ^= std::hash<int32_t>()(k.x) * 73856093u;
		h ^= std::hash<int32_t>()(k.y) * 19349663u;
		h ^= std::hash<int32_t>()(k.z) * 83492791u;
		return h;
	}
};
static std::unordered_map<AcExpKey, float, AcExpKeyHash> g_acExpCache;
static void AcClearExpCache() { g_acExpCache.clear(); }

// Raycast a celdas del punto de muestreo hacia el centro de la explosion, a
// saltos de 0.25. La celda de la explosion queda libre (el cristal no es un
// bloque); cualquier otra celda con bloque la tapa, obsidiana incluida.
static bool AcRayClear(BlockSource* region, const glm::vec3& from, const glm::vec3& to) {
	if (!region) return true;
	glm::vec3 dir = to - from;
	float dist = glm::length(dir);
	if (dist < 0.1f) return true;
	dir /= dist;
	int steps = (int)(dist / 0.25f);
	if (steps < 1) steps = 1;
	float stepLen = dist / (float)steps;
	glm::vec3 explosionCell = glm::floor(to);
	// el ultimo paso cae sobre la explosion misma, que esta justo en la cara
	// de arriba del bloque base: con el redondeo podia caer en el base
	// (y = 63.9999) y tapar la muestra. Se corta un paso antes.
	for (int i = 1; i < steps; i++) {
		glm::vec3 p = from + dir * (stepLen * (float)i);
		glm::vec3 cell = glm::floor(p);
		if (cell == explosionCell) continue;
		Block* b = region->getBlock(vec3_ti{(int)cell.x, (int)cell.y, (int)cell.z});
		if (!AcIsAir(b)) return false;
	}
	return true;
}

// 27 muestras dentro del AABB del objetivo hacia el centro de la explosion;
// el porcentaje de lineas limpias es la exposicion del original.
static float AcSeenPercent(BlockSource* region, const GaxBox& box, const glm::vec3& explosion) {
	glm::vec3 lo(box.lower.x, box.lower.y, box.lower.z);
	glm::vec3 hi(box.upper.x, box.upper.y, box.upper.z);
	int seen = 0;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			for (int k = 0; k < 3; k++) {
				glm::vec3 p(
					lo.x + (hi.x - lo.x) * (0.25f + 0.25f * i),
					lo.y + (hi.y - lo.y) * (0.25f + 0.25f * j),
					lo.z + (hi.z - lo.z) * (0.25f + 0.25f * k));
				if (AcRayClear(region, p, explosion)) seen++;
			}
		}
	}
	return (float)seen / 27.0f;
}

// Cache por explosion: los 27 rayos por objetivo se repiten entre candidatos,
// asi que se indexan por (rid del objetivo, celda de la explosion).
static float AcSeenPercentCached(uint64_t rid, BlockSource* region, const GaxBox& box, const glm::vec3& explosion) {
	AcExpKey key{rid, (int32_t)floorf(explosion.x), (int32_t)floorf(explosion.y), (int32_t)floorf(explosion.z)};
	auto it = g_acExpCache.find(key);
	if (it != g_acExpCache.end()) return it->second;
	float value = AcSeenPercent(region, box, explosion);
	g_acExpCache[key] = value;
	return value;
}

// ---------------------------------------------------------------------------
// Danio
// ---------------------------------------------------------------------------
// Centro de la explosion del cristal que iria sobre el bloque base dado.
static glm::vec3 AcExplosionOf(const vec3_ti& base) {
	return glm::vec3((float)base.x + 0.5f, (float)base.y + 1.0f, (float)base.z + 0.5f);
}

// Daнo de AutoCrystal: ((impact^2 + impact) * 0.5) * 8 con la exposicion del
// seenPercent, truncado a cero y a entero como en PC.
static float AcDamage(const GaxBox& box, const glm::vec3& explosion, float exposure) {
	float cx = (box.lower.x + box.upper.x) * 0.5f;
	float cy = (box.lower.y + box.upper.y) * 0.5f;
	float cz = (box.lower.z + box.upper.z) * 0.5f;
	float d = sqrtf((cx - explosion.x) * (cx - explosion.x) + (cy - explosion.y) * (cy - explosion.y) + (cz - explosion.z) * (cz - explosion.z));
	float v = d / 12.0f;
	if (v >= 1.0f) return 0.0f;
	float impact = (1.0f - v) * exposure;
	float damage = ((impact * impact + impact) * 0.5f) * 8.0f;
	return damage > 0.0f ? (float)(int)damage : 0.0f;
}

// Danio de AutoCrystalX: el damageFromNorm del original. Crudo con el paso a
// entero (48 * (i^2 + i) + 1, hasta ~97) y despues la reduccion: 4% por punto
// de armadura y la proteccion estimada del original (5 por pieza, tope 25,
// mitad redondeada arriba, 4% por punto). Sin la reduccion el danio propio
// pasaba siempre el tope y ACX no colocaba ni rompia nada.
static float AcxDamage(const GaxBox& box, const glm::vec3& explosion, float exposure, int armor, int pieces) {
	float cx = (box.lower.x + box.upper.x) * 0.5f;
	float cy = (box.lower.y + box.upper.y) * 0.5f;
	float cz = (box.lower.z + box.upper.z) * 0.5f;
	float d = sqrtf((cx - explosion.x) * (cx - explosion.x) + (cy - explosion.y) * (cy - explosion.y) + (cz - explosion.z) * (cz - explosion.z));
	float v = d / 12.0f;
	if (v > 1.0f) return 0.0f;
	float impact = (1.0f - v) * exposure;
	if (impact <= 0.0f) return 0.0f;
	float raw = (float)(int)(((impact * impact + impact) * 0.5f) * 8.0f * 12.0f + 1.0f);
	float afterArmor = raw - ((float)ImClamp(armor, 0, 255) * raw) * 0.04f;
	float prot = ImMin(20.0f, ceilf(ImMin(25.0f, (float)(pieces * 5)) * 0.5f));
	return ImMax(afterArmor - (prot * afterArmor) * 0.04f, 0.0f);
}

// ---------------------------------------------------------------------------
// Colocaciones
// ---------------------------------------------------------------------------
// El bloque base debe ser obsidiana o bedrock, con aire encima (dos bloques en
// vanilla, uno en el modo Java de ACX). Las entidades y el objetivo los
// revisa cada modulo con sus reglas (fase y demas).
static bool AcCanPlaceAt(BlockSource* region, const vec3_ti& pos, bool javaMode) {
	if (!AcIsBase(AcBlockAt(region, pos))) return false;
	if (!AcIsAir(AcBlockAt(region, pos.x, pos.y + 1, pos.z))) return false;
	if (!javaMode && !AcIsAir(AcBlockAt(region, pos.x, pos.y + 2, pos.z))) return false;
	return true;
}

// Superposicion con entidades: la jaula del cristal es el bloque de aire en
// x, y+1, z. Los cristales no bloquean (AcCollectBoxes ya los excluye) y
// "skip" deja pasar a un objetivo concreto (la fase de ACX). El skip apunta a
// la box del AcEntity, no a la del vector, asi que se compara por valor.
static bool AcSameBox(const GaxBox& a, const GaxBox& b) {
	return a.lower.x == b.lower.x && a.lower.y == b.lower.y && a.lower.z == b.lower.z &&
		   a.upper.x == b.upper.x && a.upper.y == b.upper.y && a.upper.z == b.upper.z;
}

static bool AcCageFree(const std::vector<GaxBox>& boxes, const vec3_ti& pos, const GaxBox* skip) {
	float x0 = (float)pos.x + 0.001f, x1 = (float)pos.x + 0.999f;
	float y0 = (float)pos.y + 1.001f, y1 = (float)pos.y + 1.999f;
	float z0 = (float)pos.z + 0.001f, z1 = (float)pos.z + 0.999f;
	for (const GaxBox& b : boxes) {
		if (skip && AcSameBox(b, *skip)) continue;
		if (b.lower.x >= x1 || b.upper.x <= x0) continue;
		if (b.lower.y >= y1 || b.upper.y <= y0) continue;
		if (b.lower.z >= z1 || b.upper.z <= z0) continue;
		return false;
	}
	return true;
}

// Clave entera de una celda para los mapas por bloque (21 bits por eje con
// signo). La usan la lista negra de AutoCrystal y las colas de AutoCrystalX.
static uint64_t AcPackCell(int x, int y, int z) {
	return (uint64_t)(uint32_t)(x & 0x1FFFFF) |
		   ((uint64_t)(uint32_t)(z & 0x1FFFFF) << 21) |
		   ((uint64_t)(uint32_t)(y & 0x1FFFFF) << 42);
}

struct AcCellKey {
	uint64_t v;
	AcCellKey() = default;
	AcCellKey(const vec3_ti& p) : v(AcPackCell(p.x, p.y, p.z)) {}
	bool operator==(const AcCellKey& o) const { return v == o.v; }
};
struct AcCellHash { size_t operator()(const AcCellKey& k) const { return (size_t)(k.v * 0x9E3779B97F4A7C15ull); } };

// ---------------------------------------------------------------------------
// Snapshot de marcas para el render overlay (el hilo de ImGui no toca nada)
// ---------------------------------------------------------------------------
struct AcMark {
	float x, y, z;  // centro del bloque base
	float enemy;    // danio al objetivo
	float self;     // danio propio
	bool flat;      // render plano (solo la cara superior)
};

// Dos canales con la misma manga: uno por modulo que coloca.
struct AcMarkChannel {
	std::vector<AcMark> shared;
	std::mutex mutex;
};
static AcMarkChannel g_acMarks;    // lo escribe AutoCrystal
static AcMarkChannel g_acxMarks;   // lo escribe AutoCrystalX

// Copia el listado de marcas propio del modulo al snapshot compartido.
static void AcStoreMarks(AcMarkChannel& ch, const std::vector<AcMark>& marks) {
	std::lock_guard<std::mutex> lock(ch.mutex);
	ch.shared = marks;
}
static void AcClearMarks(AcMarkChannel& ch) {
	std::lock_guard<std::mutex> lock(ch.mutex);
	ch.shared.clear();
}
// Copia para el overlay (hilo de ImGui). Solo la copia, nunca el original.
static void AcTakeMarks(AcMarkChannel& ch, std::vector<AcMark>& out) {
	std::lock_guard<std::mutex> lock(ch.mutex);
	out = ch.shared;
}

static float AcApproach(float cur, float target, float speed, float dt) {
	float diff = target - cur;
	float step = ImClamp(speed * dt, 0.0f, fabsf(diff));
	return cur + (diff >= 0.0f ? step : -step);
}

#else
// ---------------------------------------------------------------------------
// Otros ABIs: solo los helpers que otros archivos llaman (el resto de la
// suite lleva su propio no-op en cada cabecera).
// ---------------------------------------------------------------------------
static void CrystalApplySpoof(void*) {}
static void AcNoteUserAttack(void*) {}
static void AcNoteItemUse(void*) {}
#endif
