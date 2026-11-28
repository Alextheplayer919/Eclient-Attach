#pragma once
// Adapted from Gatyo's fly_gatyo.h; all speeds are blocks per tick.
// The algorithm writes local velocity and filters outgoing sneak bits. It is
// experimental and does not guarantee bypass of any server's checks.
namespace eclient_bypass_fly {
static std::atomic<bool> bypassFly{false};
static bool gatoFly=false, flyDynamic=false, flyAntiKick=true, flyLookControl=false;
static float flyHSpeed=1.0f, flyVSpeed=.5f, flyGlide=0, flyHDynamic=1, flyVDynamic=1, flyKickInterval=1.2f, flyKickAmount=.06f;
static float bfInitialH=2.42f,bfInitialV=1.62f,bfInitBypH=1.1585f,bfInitBypV=1.086f;
static float bfFinalH=2.18f,bfFinalV=1.42f,bfFinalBypH=1.09f,bfFinalBypV=1.05f;
static float bfGlide=-.1266f,bfReleaseDelay=.09f,bfDescentSpeed=4.2997f,bfDescentHorizontal=1.43f,bfOnlyShift=3.f;

// Fly y BypassFly portados de GatoClient. Igual que alli, los dos solo
// reescriben la velocidad del jugador en cada normalTick (Actor+GetPosition
// apunta al StateVectorComponent: pos, posAnterior, velocidad).
//
// La diferencia es la entrada: GatoClient lee WASD/espacio/shift del teclado
// y en Android hay joystick. Como no hay offsets del componente de entrada,
// la direccion se deduce del propio juego: entre un normalTick y el siguiente
// el juego suma a la velocidad la aceleracion del joystick y mueve al
// jugador. Se mide de dos formas:
//  - en el hook de gravedad: velocidad ahi - velocidad que dejamos (exacta;
//    un choque deja ese eje en 0 exacto). No corre en agua ni en lava.
//  - por la posicion: lo que se movio - velocidad que dejamos. Sirve en
//    cualquier medio; pierde precision lejos del origen (floats).
// Subir y bajar: los botones de salto y de agacharse (o los de nadar). Se leen
// de las banderas del PlayerAuthInputPacket que el juego arma cada tick. Si no
// llegan paquetes (algunos mundos locales) o si se activa "Subir y bajar
// mirando", tambien vale mirar hacia arriba o abajo; se usa la mirada del
// jugador, no la de la camara: las auras la mueven (ver LookTrack*).

static const float kFlyInputDeadzone = 0.0006f; // aceleracion minima (comiendo el joystick rinde 0.2x)
static const float kMoveCollision = 0.3f;       // diferencia que ya es un choque o una correccion
static const float kFlyPitchThreshold = 35.0f;  // grados para subir/bajar mirando
static const float kFlySneakHeight = 1.7f;      // altura de la caja al agacharse (normal: 1.8)

struct FlyInput {
	bool moving;
	float dirX, dirZ;  // direccion normalizada del joystick en el mundo
	bool up, down;
};

// Lectura del joystick, compartida con GatoAuraX (sus teclas WASD/espacio).
// MoveInputRecord va justo antes del normalTick original, con la velocidad
// que va a usar el juego (la del vuelo si esta activo); MoveInputUpdate al
// empezar el siguiente normalTick.
static vec3_t g_moveRefPos, g_moveRefVelocity;
static bool g_moveHasRef = false;
static bool g_moveGravityPending = false;
static bool g_moveGravitySeen = false;
static float g_moveGravityX = 0.0f, g_moveGravityZ = 0.0f;
static float g_moveInputX = 0.0f, g_moveInputZ = 0.0f;
static float g_moveDeadzone = kFlyInputDeadzone;
static float g_flyKickTimer = 0.0f;
static uint64_t g_authInputBits[2] = {0, 0};  // banderas del ultimo PlayerAuthInputPacket
// botones en pantalla "Subir/Bajar (vuelo)": activos mientras se mantienen (hilo de render)
static std::atomic<bool> g_flyHoldUp(false), g_flyHoldDown(false);
static int g_authInputAge = 1000;             // ticks desde el ultimo paquete
static bool g_bypassPhase = false;
static std::chrono::steady_clock::time_point g_bypassPhaseStart;

static vec3_t* FlyStateVector(Actor* actor) {
	return actor->getPosition();
}

static vec3_t* FlyVelocity(Actor* actor) {
	return (vec3_t*)((char*)FlyStateVector(actor) + 24);
}

static bool FlyActive() {
	return (gatoFly || bypassFly.load()) && !eclient_fly2_adapter::enabled.load() && !panic && !inMenu;
}

static void MoveInputRecord(LocalPlayer* player) {
	g_moveRefPos = *FlyStateVector(player);
	g_moveRefVelocity = *FlyVelocity(player);
	g_moveHasRef = true;
	g_moveGravityPending = true;
	g_moveGravitySeen = false;
}

// Desde el hook de gravedad, con el StateVector que el juego esta procesando.
static void MoveInputOnGravity(void* stateVector) {
	if (!g_moveGravityPending || !localPlayer || stateVector != (void*)FlyStateVector(localPlayer)) return;
	g_moveGravityPending = false;
	g_moveGravitySeen = true;
	const vec3_t* velocity = (const vec3_t*)((char*)stateVector + 24);
	g_moveGravityX = velocity->x == 0.0f ? 0.0f : velocity->x - g_moveRefVelocity.x;
	g_moveGravityZ = velocity->z == 0.0f ? 0.0f : velocity->z - g_moveRefVelocity.z;
}

// Distancia entre floats consecutivos cerca de v: el error de la posicion.
static float MoveFloatStep(float v) {
	return fabsf(nextafterf(fabsf(v), INFINITY) - fabsf(v));
}

static void MoveInputUpdate(LocalPlayer* player) {
	if (g_authInputAge < 1000) g_authInputAge++;
	g_moveDeadzone = kFlyInputDeadzone;
	if (g_moveGravitySeen) {
		g_moveInputX = g_moveGravityX;
		g_moveInputZ = g_moveGravityZ;
	} else if (g_moveHasRef) {
		const vec3_t* pos = FlyStateVector(player);
		float ax = (pos->x - g_moveRefPos.x) - g_moveRefVelocity.x;
		float az = (pos->z - g_moveRefPos.z) - g_moveRefVelocity.z;
		// un choque, un empujon del agua o una correccion del servidor no son el joystick
		g_moveInputX = fabsf(ax) > kMoveCollision ? 0.0f : ax;
		g_moveInputZ = fabsf(az) > kMoveCollision ? 0.0f : az;
		g_moveDeadzone = ImMax(kFlyInputDeadzone, 4.0f * ImMax(MoveFloatStep(pos->x), MoveFloatStep(pos->z)));
	} else {
		g_moveInputX = g_moveInputZ = 0.0f;
	}
	g_moveHasRef = false;
	g_moveGravityPending = false;
	g_moveGravitySeen = false;
}

// ---------------------------------------------------------------------------
// Mirada del jugador. Las auras (KillAura con rotaciones, GatoAuraX con
// "Girar la camara", Mirar 360) escriben la rotacion en cada tick, asi que el
// pitch de la camara deja de ser el del jugador. Lo que el dedo mueve entre
// una escritura y la siguiente se acumula aparte: deslizar hacia arriba sigue
// sirviendo para subir aunque la camara apunte al objetivo.
// ---------------------------------------------------------------------------
static float g_lookUserPitch = 0.0f;
static float g_lookWrittenPitch = 0.0f;
static bool g_lookOverridden = false;

static void LookTrackBegin(Actor* player) {
	vec2_t* rot = player->getRotation();
	if (!rot) return;
	if (g_lookOverridden) g_lookUserPitch = ImClamp(g_lookUserPitch + (rot->x - g_lookWrittenPitch), -90.0f, 90.0f);
	else g_lookUserPitch = rot->x;
}

static void LookTrackEnd(Actor* player, bool overridden) {
	vec2_t* rot = player->getRotation();
	if (!rot) return;
	g_lookWrittenPitch = rot->x;
	g_lookOverridden = overridden;
}

// Banderas de entrada del PlayerAuthInputPacket (bitset de 65 en el paquete +136;
// mismo orden que PlayerAuthInputData del protocolo, verificado en su write()).
enum AuthInputBit {
	AUTH_ASCEND = 0, AUTH_DESCEND = 1, AUTH_JUMP_DOWN = 3, AUTH_SNEAKING = 8, AUTH_SNEAK_DOWN = 9, AUTH_WANT_UP = 16,
	AUTH_WANT_DOWN = 17, AUTH_WANT_DOWN_SLOW = 18, AUTH_SNEAK_TOGGLE_DOWN = 23, AUTH_PERSIST_SNEAK = 24,
	AUTH_START_SNEAKING = 27, AUTH_STOP_SNEAKING = 28, AUTH_JUMP_CURRENT_RAW = 61, AUTH_SNEAK_RELEASED_RAW = 62,
	AUTH_SNEAK_PRESSED_RAW = 63, AUTH_SNEAK_CURRENT_RAW = 64,
};
static const int kAuthInputBits = 136;  // PlayerAuthInputPacket: banderas de entrada (2 x uint64)
static const int kAuthInputTick = 168;  // PlayerAuthInputPacket: "Input tick" (uint64, del write())

static bool AuthBitOf(const uint64_t* bits, int bit) {
	return (bits[bit >> 6] >> (bit & 63)) & 1;
}

static void AuthMark(uint64_t* bits, int bit) {
	bits[bit >> 6] |= 1ull << (bit & 63);
}

// Desde el hook del PlayerAuthInputPacket.
static void MoveInputOnAuthPacket(void* packet) {
	const uint64_t* bits = (const uint64_t*)((char*)packet + kAuthInputBits);
	g_authInputBits[0] = bits[0];
	g_authInputBits[1] = bits[1];
	g_authInputAge = 0;
}

static bool AuthInput(int bit) {
	return AuthBitOf(g_authInputBits, bit);
}

// Con el vuelo activo el servidor no te ve agachado. En el tactil agacharse es
// el boton de bajar, pero el servidor que te ve agachado limita la velocidad
// horizontal a la de agachado y te devuelve (lagback) al bajar moviendote;
// bajar mirando hacia abajo no lo hacia. Las banderas se quitan del paquete
// despues de leerlas (MoveInputOnAuthPacket) y se devuelven tras escribirlo.
// Al empezar a ocultarlas se manda STOP_SNEAKING y, si sigues agachado al
// apagar el vuelo, START_SNEAKING, para que el servidor no quede desfasado.
static const int kAuthSneakBits[] = {
	AUTH_SNEAKING, AUTH_SNEAK_DOWN, AUTH_WANT_DOWN, AUTH_WANT_DOWN_SLOW, AUTH_SNEAK_TOGGLE_DOWN, AUTH_PERSIST_SNEAK,
	AUTH_START_SNEAKING, AUTH_STOP_SNEAKING, AUTH_SNEAK_RELEASED_RAW, AUTH_SNEAK_PRESSED_RAW, AUTH_SNEAK_CURRENT_RAW,
};
static bool g_serverSneaking = false;  // lo que el servidor cree, segun lo que le mandamos
// La decision se toma una vez por paquete (tick, vuelo y banderas): si el juego
// escribe el mismo paquete dos veces, el START/STOP agregado sale en las dos.
static uint64_t g_authKeyTick = ~0ull, g_authKeyBits[2] = {0, 0};
static bool g_authKeyFly = false;
static uint64_t g_authClear[2] = {0, 0}, g_authSet[2] = {0, 0};

// Al salir del mundo: el siguiente servidor empieza sin agacharse.
void FlyAuthReset() {
	g_serverSneaking = false;
	g_authKeyTick = ~0ull;
}

static void FlyDecideAuthBits(const uint64_t* in) {
	g_authClear[0] = g_authClear[1] = g_authSet[0] = g_authSet[1] = 0;
	if (FlyActive()) {
		for (int bit : kAuthSneakBits) AuthMark(g_authClear, bit);
		if (g_serverSneaking) AuthMark(g_authSet, AUTH_STOP_SNEAKING);
		g_serverSneaking = false;
		return;
	}
	bool sneaking = AuthBitOf(in, AUTH_SNEAKING) || AuthBitOf(in, AUTH_START_SNEAKING);
	if (AuthBitOf(in, AUTH_STOP_SNEAKING)) sneaking = false;
	if (sneaking && !g_serverSneaking && !AuthBitOf(in, AUTH_START_SNEAKING)) AuthMark(g_authSet, AUTH_START_SNEAKING);
	if (!sneaking && g_serverSneaking && !AuthBitOf(in, AUTH_STOP_SNEAKING)) AuthMark(g_authSet, AUTH_STOP_SNEAKING);
	g_serverSneaking = sneaking;
}

// Desde el hook del PlayerAuthInputPacket, justo antes de escribirlo. Deja en
// `saved` las banderas originales para FlyRestoreAuthBits.
static void FlyFilterAuthBits(void* packet, uint64_t saved[2]) {
	uint64_t* bits = (uint64_t*)((char*)packet + kAuthInputBits);
	saved[0] = bits[0];
	saved[1] = bits[1];
	uint64_t tick = *(const uint64_t*)((char*)packet + kAuthInputTick);
	bool fly = FlyActive();
	if (tick != g_authKeyTick || fly != g_authKeyFly || saved[0] != g_authKeyBits[0] || saved[1] != g_authKeyBits[1]) {
		g_authKeyTick = tick;
		g_authKeyFly = fly;
		g_authKeyBits[0] = saved[0];
		g_authKeyBits[1] = saved[1];
		FlyDecideAuthBits(saved);
	}
	bits[0] = (bits[0] & ~g_authClear[0]) | g_authSet[0];
	bits[1] = (bits[1] & ~g_authClear[1]) | g_authSet[1];
}

static void FlyRestoreAuthBits(void* packet, const uint64_t saved[2]) {
	uint64_t* bits = (uint64_t*)((char*)packet + kAuthInputBits);
	bits[0] = saved[0];
	bits[1] = saved[1];
}

static bool AuthInputLive() {
	return g_authInputAge <= 3;
}

static FlyInput FlyReadInput(LocalPlayer* player) {
	FlyInput input = {};
	float length = sqrtf(g_moveInputX * g_moveInputX + g_moveInputZ * g_moveInputZ);
	if (length > g_moveDeadzone) {
		input.moving = true;
		input.dirX = g_moveInputX / length;
		input.dirZ = g_moveInputZ / length;
	}
	bool buttons = AuthInputLive();
	bool upButton = buttons && (AuthInput(AUTH_JUMP_DOWN) || AuthInput(AUTH_JUMP_CURRENT_RAW) || AuthInput(AUTH_WANT_UP) || AuthInput(AUTH_ASCEND));
	// Agacharse en modo interruptor (el del tactil): un toque deja SNEAKING,
	// SNEAK_DOWN y WANT_DOWN activos hasta el siguiente toque, y el dedo casi
	// nunca sigue sobre el boton cuando se arma el paquete (SNEAK_CURRENT_RAW
	// solo aparece si el toque cruza un tick). Por eso se baja mientras siga
	// agachado: un toque empieza a bajar y otro lo para.
	bool downHeld = buttons && (AuthInput(AUTH_SNEAK_CURRENT_RAW) || AuthInput(AUTH_DESCEND));
	bool downButton = downHeld || (buttons && (AuthInput(AUTH_SNEAKING) || AuthInput(AUTH_SNEAK_DOWN) || AuthInput(AUTH_WANT_DOWN)));
	bool look = flyLookControl || !buttons;
	float pitch = g_lookUserPitch;
	bool sneaking = player->getAABBDim()->y < kFlySneakHeight;
	upButton = upButton || g_flyHoldUp;
	downHeld = downHeld || g_flyHoldDown;
	downButton = downButton || downHeld;
	input.up = upButton || (look && pitch < -kFlyPitchThreshold);
	input.down = downButton || (!buttons && sneaking) || (look && pitch > kFlyPitchThreshold);
	// saltar mientras sigues agachado (sin tocar el boton) sube; mantener
	// agacharse y saltar a la vez sigue bajando
	if (input.up && input.down) {
		if (upButton && !downHeld) input.down = false;
		else input.up = false;
	}
	return input;
}

// FlyUtil::irregularBob de GatoClient: oscilacion irregular para el anti-kick.
static float FlyIrregularBob(float phaseTime, float amount, float interval) {
	float ph = (phaseTime / interval) * 6.2831853f + 0.35f * sinf(phaseTime * 0.73f);
	float amp = amount * (1.0f + 0.25f * sinf(phaseTime * 0.19f));
	return sinf(ph) * amp;
}

static void FlySetVelocity(LocalPlayer* player, float x, float y, float z) {
	vec3_t* velocity = FlyVelocity(player);
	velocity->x = x;
	velocity->y = y;
	velocity->z = z;
}

static void TickGatoFly(LocalPlayer* player, const FlyInput& in) {
	float vy;
	if (flyAntiKick && !in.up && !in.down) {
		g_flyKickTimer += 1.0f / 20.0f;
		vy = FlyIrregularBob(g_flyKickTimer, flyKickAmount, flyKickInterval);
	} else {
		vy = flyGlide;
	}

	float hSpeed = flyHSpeed;
	float vSpeed = flyVSpeed;
	if (flyDynamic && in.up) {
		hSpeed = flyHDynamic;
		vSpeed = flyVDynamic;
	}
	if (in.up) vy += vSpeed;
	if (in.down) vy -= vSpeed;

	if (in.moving) FlySetVelocity(player, in.dirX * hSpeed, vy, in.dirZ * hSpeed);
	else FlySetVelocity(player, 0.0f, vy, 0.0f);
}

// Busca en un cubo de 5x5x5 alrededor del jugador: primero agua, despues
// cualquier bloque que no sea aire. Igual que el escaneo del BypassFly original.
static bool BypassFlyNearBlocks(LocalPlayer* player, const vec3_t& pos) {
	BlockSource* region = player->getDimensionBlockSource();
	if (!region) return false;
	bool solid = false;
	for (int dx = -2; dx <= 2; dx++) {
		for (int dy = -2; dy <= 2; dy++) {
			for (int dz = -2; dz <= 2; dz++) {
				vec3_ti blockPos = {(int)pos.x + dx, (int)pos.y + dy, (int)pos.z + dz};
				Block* block = region->getBlock(blockPos);
				if (!block || block->isAir()) continue;
				BlockType* type = block->getBlockTypeClassCustom();
				#if defined(__ARM_ARCH_7A__)
				const std::string* name = type ? (const std::string*)((char*)type + 4) : nullptr;
				#else
				const std::string* name = type ? (const std::string*)((char*)type + 8) : nullptr;
				#endif
				if (name && name->find("water") != std::string::npos) return true;
				solid = true;
			}
		}
	}
	return solid;
}

static void TickBypassFly(LocalPlayer* player, const FlyInput& in) {
	vec3_t pos = *FlyStateVector(player);

	// mezcla Initial -> Final segun la distancia al origen del mundo (5.5M bloques)
	float t = ImMin(1.0f, sqrtf(pos.z * pos.z + pos.x * pos.x) / 5500000.0f);
	float bypH = bfInitBypH + (bfFinalBypH - bfInitBypH) * t;
	float normH = bfInitialH + (bfFinalH - bfInitialH) * t;
	float normV = bfInitialV + (bfFinalV - bfInitialV) * t;
	float bypV = bfInitBypV + (bfFinalBypV - bfInitBypV) * t;

	// fase "bypass": subir mientras te mueves; dura Release Delay al soltar
	bool bypassActive = false;
	auto now = std::chrono::steady_clock::now();
	if (in.up && in.moving) {
		g_bypassPhase = true;
		g_bypassPhaseStart = now;
		bypassActive = true;
	} else if (g_bypassPhase) {
		float secs = std::chrono::duration<float>(now - g_bypassPhaseStart).count();
		bypassActive = secs < bfReleaseDelay;
		if (!bypassActive) g_bypassPhase = false;
	}

	float vy;
	if (in.down && !in.moving && !in.up) vy = -bfOnlyShift;
	else if (in.down) vy = -bfDescentSpeed;
	else {
		vy = bfGlide;
		if (in.up) vy += bypassActive ? bypV : normV;
	}

	if (!in.moving) {
		FlySetVelocity(player, 0.0f, vy, 0.0f);
		return;
	}

	float speed;
	if (in.down) speed = bfDescentHorizontal;
	else if (BypassFlyNearBlocks(player, pos)) speed = 2.0f;
	else if (bypassActive) speed = bypH;
	else speed = normH;
	FlySetVelocity(player, in.dirX * speed, vy, in.dirZ * speed);
}

// Desde normalTick, antes de llamar al original.
static void FlyOnNormalTick(LocalPlayer* player) {
	static bool wasActive = false;
	if (!FlyActive()) {
		if (wasActive) {
			g_bypassPhase = false;
			wasActive = false;
		}
		return;
	}
	if (!wasActive) {
		g_bypassPhase = false;
		wasActive = true;
	}
	FlyInput input = FlyReadInput(player);
	if (bypassFly) TickBypassFly(player, input);
	else TickGatoFly(player, input);
}

static void ui() {
    bool on=bypassFly.load();if(ImGui::Checkbox("Bypass Fly",&on)){bypassFly.store(on);}
    ImGui::Checkbox("Look up/down as vertical control",&flyLookControl);
    ImGui::TextWrapped("Gatyo's original velocity and packet method. Disable Fly2 and other flight modules first; server corrections or flags remain possible.");
}
static void reset() {g_moveHasRef=false;g_moveGravityPending=false;g_moveGravitySeen=false;g_authInputAge=1000;g_authInputBits[0]=g_authInputBits[1]=0;g_bypassPhase=false;FlyAuthReset();}
}
