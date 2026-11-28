#pragma once
// Paquetes inyectados al cliente como si vinieran del servidor, igual que el
// FakePlayer de GatoClient: el juego crea y maneja las entidades como las de
// un servidor real y el servidor nunca se entera.
//
// Cada paquete se crea con la funcion del juego que hace su make_shared (le
// deja su dispatcher en +0x20) y se entrega llamando al handle de ese
// dispatcher (slot 2) con el handler de red del cliente. El handler y el
// NetworkIdentifier se capturan enganchando el handle de paquetes que llegan
// seguido (tiempo, chunks, movimiento...) y el ActorEvent del PopCounter.
//
// Direcciones de libminecraftpe.so 1.21.111 arm64 (IDA). Los creadores se
// ubicaron por las vtables __shared_ptr_emplace<XPacket>; las estructuras, por
// el write() de cada paquete (nombres de campos) o por su handler en
// LegacyClientNetworkHandler, que es el que maneja AddPlayer (en
// ClientNetworkHandler ese slot esta vacio).

#if defined(__aarch64__)
namespace GatyoNet {
	enum : uintptr_t {
		CreateAddPlayer = 0x9C1AF18,
		CreateActorEvent = 0x9C1B9B8,
		CreateMoveActorAbsolute = 0x9C1AFC0,
		CreateRemoveActor = 0x9C1B274,
		CreateLevelSoundEvent = 0x9C1B520,
		LegacyClientNetworkHandlerVtable = 0x10046A10,  // punto de la vtable (_ZTV + 0x10)
	};
	// _ZTV de PacketHandlerDispatcherInstance<T, false> de paquetes frecuentes;
	// el slot 2 de todos es el mismo reenvio al handler de red.
	static const uintptr_t kCaptureDispatchers[] = {
		0x101FF488,  // SetTimePacket
		0x10202658,  // NetworkChunkPublisherUpdatePacket
		0x101FF908,  // MoveActorDeltaPacket
		0x101FF998,  // MovePlayerPacket
		0x10200738,  // SetActorDataPacket
		0x102007C8,  // SetActorMotionPacket
		0x101FF518,  // TextPacket
		0x101FFF18,  // LevelSoundEventPacket
		0x101FF638,  // AddActorPacket (AutoCrystalX: rompe el cristal al aparecer)
		0x101FFAB8,  // RemoveActorPacket (AutoCrystalX: coloca al desaparecer)
	};
	static const uintptr_t kAddActorDispatcher = 0x101FF638, kRemoveActorDispatcher = 0x101FFAB8;
	static const int kPacketDispatcher = 0x20;   // Packet: dispatcher de su tipo
	static const int kLevelGetRuntimeEntity = 0x1E8;  // vtable de Level: getRuntimeEntity(ActorRuntimeID, bool)

	// AddPlayerPacket (ver su write: "Player Name", "Actor Runtime ID", ...)
	static const int kAddName = 72, kAddUuidHi = 96, kAddUuidLo = 104, kAddUniqueId = 112, kAddRuntimeId = 120;
	static const int kAddPos = 152, kAddVel = 164, kAddRot = 176, kAddHeadYaw = 184;
	static const int kAddBuildPlatform = 1688, kAddGameType = 1692;
	// payloads (despues de los 48 bytes de Packet)
	static const int kPayload = 48;
	// MoveActorAbsoluteData: runtime id, flags (1 en el suelo, 2 teletransporte), pos, rotaciones en bytes
	static const int kMoveFlags = 8, kMovePos = 12, kMoveRot = 24;
	// ActorEventPacket: runtime id, evento (uint8), dato
	static const int kEventId = 8, kEventData = 12;
	// LevelSoundEventPacket (ver su handler)
	static const int kSoundEntity = 48, kSoundUniqueId = 72, kSoundEvent = 80, kSoundPos = 84, kSoundData = 96;
}

// ActorEvent y LevelSoundEvent del protocolo
enum : uint8_t { NET_EVENT_HURT = 2, NET_EVENT_DEATH = 3, NET_EVENT_TOTEM = 65 };
enum : uint32_t { NET_SOUND_DEATH = 14, NET_SOUND_HURT = 17, NET_SOUND_ATTACK_NODAMAGE = 42, NET_SOUND_ATTACK_STRONG = 43 };

typedef void (*NetHandleFn)(void* self, void* source, void* callback, void* packet);
using NetPacket = std::shared_ptr<char>;

static void* g_netCallback = nullptr;  // handler de red del cliente; cambia en cada conexion
static void* g_netSource = nullptr;
static const int kNetCaptureMax = sizeof(GatyoNet::kCaptureDispatchers) / sizeof(GatyoNet::kCaptureDispatchers[0]);
static uintptr_t g_netCaptureVtables[kNetCaptureMax];
static NetHandleFn g_netCaptureOriginal[kNetCaptureMax];
static uintptr_t g_netAddActorVt = 0, g_netRemoveActorVt = 0;
static int g_netCaptureCount = 0;
static bool g_netLogged = false;

// Desde los handles enganchados y desde el ActorEvent del PopCounter.
void NetCaptureContext(void* callback, void* source) {
	g_netCallback = callback;
	g_netSource = source;
	if (!g_netLogged && callback) {
		// GATYO_PROBE: que handler es (Legacy maneja AddPlayer)
		g_netLogged = true;
		uintptr_t vt = *(uintptr_t*)callback - getAbsoluteAddress(MineLib, 0);
		__android_log_print(ANDROID_LOG_INFO, "Gatyo", "net: handler vtable +0x%lx (%s)", (unsigned long)vt,
			vt == GatyoNet::LegacyClientNetworkHandlerVtable ? "Legacy" : "otro");
	}
}

// Al salir del mundo: el handler muere con la conexion.
void NetResetContext() {
	g_netCallback = nullptr;
	g_netSource = nullptr;
}

static void NetCaptureHandle(void* self, void* source, void* callback, void* packet) {
	NetCaptureContext(callback, source);
	uintptr_t vt = *(uintptr_t*)self;
	for (int i = 0; i < g_netCaptureCount; i++) {
		if (g_netCaptureVtables[i] == vt) {
			// los cristales que aparecen o desaparecen despiertan a AutoCrystalX
			// sin esperar su tick (el actor ya esta, o ya no, en el Level)
			if (vt == g_netRemoveActorVt) {
				int before = AcxCrystalCount();
				g_netCaptureOriginal[i](self, source, callback, packet);
				AcxOnNetActor(false, before);
			} else {
				g_netCaptureOriginal[i](self, source, callback, packet);
				if (vt == g_netAddActorVt) AcxOnNetActor(true, 0);
			}
			return;
		}
	}
}

void NetInstall() {
	for (uintptr_t ztv : GatyoNet::kCaptureDispatchers) {
		uintptr_t point = getAbsoluteAddress(MineLib, ztv + 0x10);
		uintptr_t slot = point + 2 * sizeof(void*);
		g_netCaptureVtables[g_netCaptureCount] = point;
		g_netCaptureOriginal[g_netCaptureCount] = *(NetHandleFn*)slot;
		void* replacement = (void*)NetCaptureHandle;
		MemoryPatch* patch = new MemoryPatch(slot, &replacement, sizeof(replacement));  // vive todo el proceso
		if (!patch->Modify()) continue;
		g_netCaptureCount++;
		if (ztv == GatyoNet::kAddActorDispatcher) g_netAddActorVt = point;
		if (ztv == GatyoNet::kRemoveActorDispatcher) g_netRemoveActorVt = point;
	}
}

static bool NetReady() {
	return g_netCallback != nullptr;
}

static NetPacket NetCreate(uintptr_t creator) {
	using CreateFn = NetPacket (*)();  // shared_ptr<Packet> por X8, mismo libc++ que el juego
	return ((CreateFn)getAbsoluteAddress(MineLib, creator))();
}

static void NetDeliver(NetPacket& packet) {
	if (!g_netCallback || !packet) return;
	void* dispatcher = *(void**)(packet.get() + GatyoNet::kPacketDispatcher);
	if (!dispatcher) return;
	NetHandleFn handle = (*(NetHandleFn**)dispatcher)[2];
	handle(dispatcher, g_netSource, g_netCallback, &packet);
}

static uint8_t NetAngleByte(float degrees) {
	return (uint8_t)(int)lroundf(degrees / 360.0f * 256.0f);
}

static void NetPutVec3(char* at, float x, float y, float z) {
	((float*)at)[0] = x;
	((float*)at)[1] = y;
	((float*)at)[2] = z;
}

void NetAddPlayer(uint64_t runtimeId, int64_t uniqueId, uint64_t uuidHi, uint64_t uuidLo, const std::string& name, const vec3_t& feet, float pitch, float yaw) {
	NetPacket p = NetCreate(GatyoNet::CreateAddPlayer);
	if (!p) return;
	char* d = p.get();
	*(std::string*)(d + GatyoNet::kAddName) = name;
	*(uint64_t*)(d + GatyoNet::kAddUuidHi) = uuidHi;
	*(uint64_t*)(d + GatyoNet::kAddUuidLo) = uuidLo;
	*(int64_t*)(d + GatyoNet::kAddUniqueId) = uniqueId;
	*(uint64_t*)(d + GatyoNet::kAddRuntimeId) = runtimeId;
	NetPutVec3(d + GatyoNet::kAddPos, feet.x, feet.y, feet.z);
	NetPutVec3(d + GatyoNet::kAddVel, 0, 0, 0);
	((float*)(d + GatyoNet::kAddRot))[0] = pitch;
	((float*)(d + GatyoNet::kAddRot))[1] = yaw;
	*(float*)(d + GatyoNet::kAddHeadYaw) = yaw;
	*(int*)(d + GatyoNet::kAddBuildPlatform) = 1;  // Android
	*(int*)(d + GatyoNet::kAddGameType) = 0;       // supervivencia: atacable
	NetDeliver(p);
}

// `pos` a la altura de los ojos, como manda el servidor para los jugadores.
void NetMoveActor(uint64_t runtimeId, const vec3_t& pos, float pitch, float yaw, bool onGround, bool teleport) {
	NetPacket p = NetCreate(GatyoNet::CreateMoveActorAbsolute);
	if (!p) return;
	char* d = p.get() + GatyoNet::kPayload;
	*(uint64_t*)d = runtimeId;
	*(uint8_t*)(d + GatyoNet::kMoveFlags) = (onGround ? 1 : 0) | (teleport ? 2 : 0);
	NetPutVec3(d + GatyoNet::kMovePos, pos.x, pos.y, pos.z);
	*(uint8_t*)(d + GatyoNet::kMoveRot) = NetAngleByte(pitch);
	*(uint8_t*)(d + GatyoNet::kMoveRot + 1) = NetAngleByte(yaw);
	*(uint8_t*)(d + GatyoNet::kMoveRot + 2) = NetAngleByte(yaw);
	NetDeliver(p);
}

void NetActorEvent(uint64_t runtimeId, uint8_t event, int data = 0) {
	NetPacket p = NetCreate(GatyoNet::CreateActorEvent);
	if (!p) return;
	char* d = p.get() + GatyoNet::kPayload;
	*(uint64_t*)d = runtimeId;
	*(uint8_t*)(d + GatyoNet::kEventId) = event;
	*(int*)(d + GatyoNet::kEventData) = data;
	NetDeliver(p);
}

void NetRemoveActor(int64_t uniqueId) {
	NetPacket p = NetCreate(GatyoNet::CreateRemoveActor);
	if (!p) return;
	*(int64_t*)(p.get() + GatyoNet::kPayload) = uniqueId;
	NetDeliver(p);
}

void NetPlayerSound(uint32_t sound, const vec3_t& pos) {
	NetPacket p = NetCreate(GatyoNet::CreateLevelSoundEvent);
	if (!p) return;
	char* d = p.get();
	*(std::string*)(d + GatyoNet::kSoundEntity) = "minecraft:player";
	*(int64_t*)(d + GatyoNet::kSoundUniqueId) = -1;
	*(uint32_t*)(d + GatyoNet::kSoundEvent) = sound;
	NetPutVec3(d + GatyoNet::kSoundPos, pos.x, pos.y, pos.z);
	*(int*)(d + GatyoNet::kSoundData) = -1;
	NetDeliver(p);
}

Actor* NetRuntimeEntity(Level* level, uint64_t runtimeId) {
	if (!level) return nullptr;
	typedef Actor* (*GetRuntimeEntity)(Level*, uint64_t, bool);
	return (*(GetRuntimeEntity**)level)[GatyoNet::kLevelGetRuntimeEntity / 8](level, runtimeId, false);
}
#else
void NetCaptureContext(void*, void*) {}
void NetResetContext() {}
void NetInstall() {}
static bool NetReady() { return false; }
#endif
