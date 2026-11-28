#pragma once
// PopCounter portado de GatoClient: cuenta los totems que usa cada jugador, lo
// avisa en tu chat y, al morir, avisa cuantos uso. Con "Mensaje al popear" o
// "Mensaje al morir" tambien lo escribe en el chat del servidor con uno de los
// mensajes de la lista (palabras clave del original: !player!, !pops!,
// !totem!, !clientname!; $ en lugar de §).
//
// Se detecta por las dos vias del original:
//  - el sonido del totem (LevelEventPacket SoundTotemUsed 1052), la opcion por
//    defecto de GatoClient: el servidor lo manda a todos los que estan cerca,
//    con la posicion del que lo uso, y se le asigna al jugador mas cercano.
//  - el ActorEventPacket TALISMAN_ACTIVATE (65), el que dibuja la animacion,
//    con el runtime id exacto. Varios servidores solo se lo mandan al que lo
//    uso, por eso no alcanza solo (asi no contaba los pops de los demas).
// Un pop que llega por las dos vias cuenta una vez. La muerte llega como
// ActorEvent DEATH (3) o con la vida en 0.

#include <unordered_map>

#if defined(__aarch64__)
namespace GatyoPop {
	enum : uintptr_t {
		HandleActorEvent = 0x6DBAFB0,  // ClientNetworkHandler::handle(NetworkIdentifier const&, ActorEventPacket const&)
		HandleLevelEvent = 0x6DBB6C0,  // ClientNetworkHandler::handle(NetworkIdentifier const&, LevelEventPacket const&) (slot 0x198)
		LevelUsable = 0xD6B0C28,       // lo que ese handler comprueba del Level antes de buscar el actor
	};
	static const int kHandlerReady = 144;              // ClientNetworkHandler: bandera que el handler mira primero
	static const int kHandlerLevel = 160;              // ClientNetworkHandler -> Level*
	static const int kLevelGetRuntimeEntity = 0x1E8;   // vtable de Level: getRuntimeEntity(ActorRuntimeID, bool)
	static const int kPacketRuntimeId = 48;            // ActorEventPacket: runtime id del actor
	static const int kPacketEvent = 56;                // ActorEventPacket: ActorEvent (uint8)
	static const uint8_t kEventDeath = 3;
	static const uint8_t kEventTotem = 65;             // TALISMAN_ACTIVATE
	static const int kTypePlayer = 319;                // getEntityTypeId() de un jugador
	static const int kLevelPacketEvent = 44;           // LevelEventPacket: LevelEvent (int)
	static const int kLevelPacketPos = 48;             // LevelEventPacket: posicion (vec3)
	static const int kLevelEventTotemSound = 1052;     // SoundTotemUsed
	static const float kTotemSoundRadius = 4.0f;       // bloques del sonido al jugador (la posicion remota llega con retraso)
	static const float kPlayerEyeHeight = 1.62f;       // getPosition() de un jugador esta a la altura de los ojos
}
#endif

static std::unordered_map<std::string, int> g_popCounts;  // nombre -> totems desde su ultima muerte
// ultimo pop por nombre: el sonido y el ActorEvent del mismo totem llegan juntos
static std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_popLastSignal;
static const auto kPopSignalMerge = std::chrono::milliseconds(200);
// el servidor manda el ActorEvent de los totems de otros: el sonido sobra
static bool g_popOthersByActorEvent = false;
static size_t g_popMessageNext = 0, g_popDeathNext = 0;

void VisualsOnPop(const std::string& name, bool self);  // visuals_gatyo.h (sesion)
void CharmsOnPop(const std::string& name, bool self);   // charms_gatyo.h (fantasma)
void CharmsOnDeath(const std::string& name, bool self); // charms_gatyo.h (efecto)
void NetCaptureContext(void* callback, void* source);  // netinject_gatyo.h
bool FakePlayerIsNpc(Actor* actor);                    // fakeplayer_gatyo.h

// La cuenta tambien la usan los nametags del ESP, el TargetHUD, la sesion y
// los Charms; los avisos al chat siguen siendo solo del PopCounter.
static bool PopTracking() {
	return popCounter || (esp && espPops) || targetHud || sessionHud || charms;
}

// Al salir del mundo.
void PopCounterReset() {
	g_popCounts.clear();
	g_popLastSignal.clear();
	g_popOthersByActorEvent = false;
}

static bool PopIsFriend(const std::string& name) {
	return std::find(friendList.begin(), friendList.end(), name) != friendList.end();
}

static const char* PopTotemWord(int pops) {
	return T(pops == 1 ? S_PopTotem : S_PopTotems);
}

static void PopShow(const std::string& text) {
	if (localPlayer) localPlayer->displayClientMessage(text, "");
}

// Uno de la lista (al azar o en orden), sin los vacios.
static std::string PopPickMessage(const std::vector<std::string>& list, size_t& next) {
	std::lock_guard<std::mutex> lock(g_textMutex);
	std::vector<const std::string*> usable;
	for (const std::string& message : list) {
		if (!message.empty()) usable.push_back(&message);
	}
	if (usable.empty()) return "";
	size_t pick;
	if (popRandomMessage) {
		static std::mt19937 rng{std::random_device{}()};
		pick = std::uniform_int_distribution<size_t>(0, usable.size() - 1)(rng);
	} else {
		pick = next++ % usable.size();
	}
	return *usable[pick];
}

static std::string PopFormat(std::string text, const std::string& name, int pops) {
	std::string clientName;
	{
		std::lock_guard<std::mutex> lock(g_textMutex);
		clientName = chatClientName.empty() ? "Gatyo" : chatClientName;
	}
	auto replaceAll = [&text](const std::string& from, const std::string& to) {
		for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
	};
	replaceAll("$", "§");
	replaceAll("!clientname!", clientName);
	replaceAll("!player!", name);
	replaceAll("!pops!", std::to_string(pops));
	replaceAll("!totem!", pops == 1 ? "totem" : "totems");
	return text;
}

static void PopSendPublic(const std::vector<std::string>& list, size_t& next, const std::string& name, int pops) {
	std::string message = PopPickMessage(list, next);
	if (!message.empty()) QueueSend(PopFormat(message, name, pops));
}

// `allowPublic` en false (jugador falso) nunca escribe en el chat del servidor.
static void PopCounterOnPop(const std::string& name, bool self, bool allowPublic = true) {
	if (name.empty()) return;
	int pops = ++g_popCounts[name];
	VisualsOnPop(name, self);
	CharmsOnPop(name, self);
	if (!popCounter) return;
	if (self) {
		if (popCountSelf) PopShow(TF(S_PopChatSelf, pops, PopTotemWord(pops)));
		return;
	}
	if (PopIsFriend(name)) {
		if (popCountFriends) PopShow(TF(S_PopChatPop, "§9", name.c_str(), pops, PopTotemWord(pops)));
		return;
	}
	PopShow(TF(S_PopChatPop, "§c", name.c_str(), pops, PopTotemWord(pops)));
	if (popSendPop && allowPublic) PopSendPublic(popMessages, g_popMessageNext, name, pops);
}

// Tambien desde NormalTick con la vida en 0: la cuenta se borra, asi que la
// segunda señal de la misma muerte no repite el aviso.
void PopCounterOnDeath(const std::string& name, bool self, bool allowPublic = true) {
	CharmsOnDeath(name, self);  // repite cada tick con la vida en 0: los Charms lo filtran
	auto it = g_popCounts.find(name);
	if (it == g_popCounts.end()) return;
	int pops = it->second;
	g_popCounts.erase(it);
	if (pops <= 0 || !popCounter) return;
	if (self) {
		if (popCountSelf) PopShow(TF(S_PopChatSelfDeath, pops, PopTotemWord(pops)));
		return;
	}
	bool isFriend = PopIsFriend(name);
	PopShow(TF(S_PopChatDeath, isFriend ? "§c[-]" : "§a[+]", isFriend ? "§9" : "§c", name.c_str(), pops, PopTotemWord(pops)));
	if (!isFriend && popSendDeath && allowPublic) PopSendPublic(popDeathMessages, g_popDeathNext, name, pops);
}

#if defined(__aarch64__)
// Un pop que llego del servidor (sonido o ActorEvent).
static void PopCounterOnNetPop(const std::string& name, bool self, bool allowPublic) {
	auto now = std::chrono::steady_clock::now();
	auto last = g_popLastSignal.find(name);
	if (last != g_popLastSignal.end() && now - last->second < kPopSignalMerge) return;
	g_popLastSignal[name] = now;
	PopCounterOnPop(name, self, allowPublic);
}

// Lo que el handler del juego comprueba antes de procesar el paquete.
static bool PopHandlerReady(char* handler) {
	uint8_t* ready = *(uint8_t**)(handler + GatyoPop::kHandlerReady);
	void* level = *(void**)(handler + GatyoPop::kHandlerLevel);
	if (!ready || !*ready || !level) return false;
	static auto usable = (bool (*)(void*))getAbsoluteAddress(MineLib, GatyoPop::LevelUsable);
	return usable(level);
}

static void PopCounterOnActorEvent(char* handler, char* packet) {
	uint8_t event = *(uint8_t*)(packet + GatyoPop::kPacketEvent);
	if (event != GatyoPop::kEventTotem && event != GatyoPop::kEventDeath) return;
	if (!PopHandlerReady(handler)) return;
	void* level = *(void**)(handler + GatyoPop::kHandlerLevel);
	typedef Actor* (*GetRuntimeEntity)(void*, uint64_t, bool);
	GetRuntimeEntity getEntity = (*(GetRuntimeEntity**)level)[GatyoPop::kLevelGetRuntimeEntity / 8];
	uint64_t runtimeId = *(uint64_t*)(packet + GatyoPop::kPacketRuntimeId);
	Actor* actor = getEntity(level, runtimeId, false);
	bool player = actor && actor->getEntityTypeId() == GatyoPop::kTypePlayer;
	std::string name = player ? ((Player*)actor)->getName() : "";
	__android_log_print(ANDROID_LOG_INFO, "Gatyo", "pop: ActorEvent %d rid=%llu -> %s", event,
		(unsigned long long)runtimeId, player ? name.c_str() : (actor ? "(no jugador)" : "(sin actor)"));
	if (!player) return;
	bool self = actor == (Actor*)localPlayer;
	bool allowPublic = name != kFakePlayerName;  // el jugador falso nunca sale al chat del servidor
	if (event == GatyoPop::kEventTotem && !self && allowPublic) g_popOthersByActorEvent = true;
	if (event == GatyoPop::kEventTotem) PopCounterOnNetPop(name, self, allowPublic);
	else PopCounterOnDeath(name, self, allowPublic);
}

// El sonido trae la posicion del que uso el totem (pies u ojos, segun el
// servidor): se elige el jugador cuyo cuerpo queda mas cerca. Si el servidor
// ya mando el ActorEvent de otro jugador, esa via es exacta y esta se apaga.
static void PopCounterOnLevelEvent(char* handler, char* packet) {
	if (*(int*)(packet + GatyoPop::kLevelPacketEvent) != GatyoPop::kLevelEventTotemSound) return;
	if (g_popOthersByActorEvent || !localPlayer || !PopHandlerReady(handler)) return;
	vec3_t at = *(vec3_t*)(packet + GatyoPop::kLevelPacketPos);
	if (at.x == 0.0f && at.y == 0.0f && at.z == 0.0f) return;  // el original tambien los ignora
	Level* level = localPlayer->getLevel();
	GameplayUserManager* users = level ? level->getGameplayUserManager() : nullptr;
	if (!users) return;
	Player* best = nullptr;
	float bestDist = GatyoPop::kTotemSoundRadius * GatyoPop::kTotemSoundRadius;
	users->forEachActivePlayer([&](Player& player) {
		if (FakePlayerIsNpc(&player)) return true;  // su pop lo simula fakeplayer_gatyo.h
		vec3_t* eye = player.getPosition();
		if (!eye) return true;
		float dx = eye->x - at.x, dz = eye->z - at.z;
		float dy = ImClamp(at.y, eye->y - GatyoPop::kPlayerEyeHeight, eye->y) - at.y;
		float dist = dx * dx + dy * dy + dz * dz;
		if (dist < bestDist) {
			bestDist = dist;
			best = &player;
		}
		return true;
	}, true);
	std::string name = best ? best->getName() : "";
	__android_log_print(ANDROID_LOG_INFO, "Gatyo", "pop: sonido (%.1f %.1f %.1f) -> %s a %.1f", at.x, at.y, at.z,
		best ? name.c_str() : "(nadie cerca)", best ? sqrtf(bestDist) : 0.0f);
	if (!best) return;
	PopCounterOnNetPop(name, best == (Player*)localPlayer, true);
}

void (*old_HandleActorEvent)(void* handler, void* source, void* packet);
void HandleActorEvent(void* handler, void* source, void* packet) {
	NetCaptureContext(handler, source);
	// antes del original: el actor todavia existe y su muerte no se proceso
	if (!panic && PopTracking()) PopCounterOnActorEvent((char*)handler, (char*)packet);
	old_HandleActorEvent(handler, source, packet);
}

void (*old_HandleLevelEvent)(void* handler, void* source, void* packet);
void HandleLevelEvent(void* handler, void* source, void* packet) {
	if (!panic && PopTracking()) PopCounterOnLevelEvent((char*)handler, (char*)packet);
	old_HandleLevelEvent(handler, source, packet);
}
#endif

// Ajustes (pestaña PopCounter de Combate y fila del ClickGUI). `narrow` pone
// cada control en su linea, para las ventanas del ClickGUI.
void DrawPopCounterSettings(float width, bool narrow) {
	ModToggle(MOD_popCountSelf);
	if (!narrow) ImGui::SameLine();
	ModToggle(MOD_popCountFriends);
	ModToggle(MOD_popSendPop);
	if (!narrow) ImGui::SameLine();
	ModToggle(MOD_popSendDeath);
	if (!popSendPop && !popSendDeath) return;
	ModToggle(MOD_popRandomMessage);
	MutedText(T(S_TipPopMessages));
	if (popSendPop) {
		if (narrow) MutedText(T(S_PopMessages));
		else SectionTitle(S_PopMessages);
		DrawTextList(popMessages, S_PopMessageTitle, width);
	}
	if (popSendDeath) {
		if (narrow) MutedText(T(S_PopDeathMessages));
		else SectionTitle(S_PopDeathMessages);
		DrawTextList(popDeathMessages, S_PopMessageTitle, width);
	}
}
