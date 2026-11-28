#pragma once
// Charms: efectos de pelea, al estilo de PopChams y DeathEffect de GatoClient
// mas particulas de golpe.
//
//   Pop:    cuando alguien usa un totem queda un fantasma con su forma que
//           sube, se abre y se desvanece (PopChams).
//   Muerte: un efecto donde muere un jugador: alma que sube, rayo, estallido,
//           helice o nova (DeathEffect, con cinco de sus modos).
//   Golpe:  particulas que saltan del objetivo hacia ti al pegarle.
//
// Los avisos llegan del hilo del juego: el pop del PopCounter (ActorEvent o
// sonido del totem), la muerte por la vida en 0 o el ActorEvent DEATH, y el
// golpe desde GameMode::attack (visuals_gatyo.h). Cada aviso se guarda como un
// CharmFx con su hora de inicio y una semilla; la forma de cada momento sale
// solo de (edad, semilla), sin simular nada por frame. Asi lo pueden dibujar
// los dos renders sin compartir estado: el del mundo (worldrender_gatyo.h,
// con la profundidad del juego, en el mismo pase que el CrystalChams) y el
// overlay de ImGui de fallback, cada uno desde su hilo.
//
// Todo se arma con cajas (CrystalShell) para usar el programa de los
// cristales: relleno con brillo en la silueta y borde por cara.

static const int kCharmsMaxFx = 48;       // efectos vivos a la vez (los mas viejos se van)
static const size_t kCharmsMaxBoxes = 1500;
static const int kCharmsTypePlayer = 319; // getEntityTypeId() de un jugador
static const int kCharmsTypeMob = 0x100;  // bit de Mob en ActorType (jugadores y criaturas)
static const float kCharmsEyeHeight = 1.62f;

enum CharmKind { CHARM_POP, CHARM_DEATH, CHARM_HIT };
enum CharmDeathMode { CHD_SOUL, CHD_LIGHTNING, CHD_BURST, CHD_HELIX, CHD_NOVA, CHD_COUNT };
enum CharmHitMode { CHH_CUBES, CHH_SPARKS, CHH_RING, CHH_COUNT };
// Color de borde de cada grupo: el programa del mundo lo lleva como uniforme.
enum CharmBatch { CHB_PARTICLE, CHB_POP, CHB_DEATH, CHB_COUNT };

struct CharmFx {
	int kind = CHARM_POP, mode = 0;
	glm::vec3 origin = glm::vec3(0.0f);  // pies (pop, muerte) o punto del golpe
	glm::vec3 dir = glm::vec3(0.0f);     // golpe: horizontal, del objetivo hacia el atacante
	float yaw = 0.0f, height = 1.8f;
	uint32_t seed = 0;
	double start = 0.0;
	float duration = 1.0f;
};

struct CharmBox {
	CrystalShell shell;  // esquinas en el orden de GatyoBoxCorners y giro
	glm::vec4 fill;
	float fade;          // multiplica relleno y borde
	float edge;          // fuerza del borde (0: sin borde)
	int batch;
};

static std::mutex g_charmsMutex;
static std::vector<CharmFx> g_charmsFx;
static std::atomic<double> g_charmsBusyUntil{0.0};  // hasta cuando hay algo para dibujar
static std::atomic<uint32_t> g_charmsCounter{1};

// Hay efectos y el pase del mundo los puede dibujar: el overlay no dibuja.
bool CharmsWorldActive() {
	return charms && !panic && g_wrChamsAvailable.load() && !g_wrChamsNoSlot.load() && GatyoNow() < g_charmsBusyUntil.load();
}

// ---------------------------------------------------------------------------
// Azar fijo por (semilla, indice): cada particula sale igual en cada frame
// ---------------------------------------------------------------------------
static uint32_t CharmsHash(uint32_t x) {
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

static float CharmsRand(uint32_t seed, int i, int k) {
	uint32_t h = CharmsHash(seed ^ CharmsHash((uint32_t)i * 0x9E3779B1u + (uint32_t)k * 0x85EBCA77u));
	return (float)(h >> 8) * (1.0f / 16777216.0f);
}

static glm::vec3 CharmsRandDir(uint32_t seed, int i) {
	float z = CharmsRand(seed, i, 0) * 2.0f - 1.0f, a = CharmsRand(seed, i, 1) * 6.2831853f;
	float r = sqrtf(ImMax(0.0f, 1.0f - z * z));
	return glm::vec3(r * cosf(a), z, r * sinf(a));
}

static float CharmsEaseOut(float t) {
	float u = 1.0f - ImSaturate(t);
	return 1.0f - u * u * u;
}

// Desvanecido con la edad, con una entrada corta de `in` segundos.
static float CharmsFade(float t, float age, float in, float power = 1.4f) {
	float a = powf(1.0f - ImSaturate(t), power);
	return in > 0.0f ? a * ImSaturate(age / in) : a;
}

// Posicion a los `s` segundos con frenado (por segundo) y gravedad.
static glm::vec3 CharmsBallistic(glm::vec3 p0, glm::vec3 v0, float s, float drag, float gravity) {
	float f = drag > 0.0f ? (1.0f - expf(-drag * s)) / drag : s;
	return p0 + v0 * f - glm::vec3(0.0f, 0.5f * gravity * s * s, 0.0f);
}

static glm::vec3 CharmsVelocity(glm::vec3 v0, float s, float drag, float gravity) {
	return v0 * expf(-drag * s) - glm::vec3(0.0f, gravity * s, 0.0f);
}

// Color del ajuste o, con Arcoiris, un tono que corre con el tiempo.
static glm::vec4 CharmsTint(const float* c, uint32_t seed, int i, double now) {
	if (!charmsRainbow) return glm::vec4(c[0], c[1], c[2], c[3]);
	float hue = fmodf((float)(seed & 1023) / 1024.0f + (float)i * 0.045f + (float)fmod(now * 0.35, 1.0), 1.0f);
	glm::vec4 out(0.0f, 0.0f, 0.0f, c[3]);
	ImGui::ColorConvertHSVtoRGB(hue, 0.62f, 1.0f, out.x, out.y, out.z);
	return out;
}

static glm::vec4 CharmsLighten(glm::vec4 c, float k) {
	return glm::vec4(c.x + (1.0f - c.x) * k, c.y + (1.0f - c.y) * k, c.z + (1.0f - c.z) * k, c.w);
}

// ---------------------------------------------------------------------------
// Piezas
// ---------------------------------------------------------------------------
static CrystalShell CharmsShell(glm::vec3 center, glm::vec3 half, const glm::mat3& rot) {
	static const float sx[8] = {-1, 1, 1, -1, -1, 1, 1, -1}, sy[8] = {-1, -1, -1, -1, 1, 1, 1, 1}, sz[8] = {-1, -1, 1, 1, -1, -1, 1, 1};
	CrystalShell s;
	s.rot = rot;
	for (int k = 0; k < 8; k++) s.corners[k] = center + rot * (glm::vec3(sx[k], sy[k], sz[k]) * half);
	return s;
}

static void CharmsCube(std::vector<CharmBox>& out, glm::vec3 center, float half, const glm::mat3& rot, glm::vec4 fill, float fade) {
	if (half <= 0.002f || fade <= 0.003f) return;
	out.push_back({CharmsShell(center, glm::vec3(half), rot), fill, fade, 0.0f, CHB_PARTICLE});
}

static glm::mat3 CharmsSpin(uint32_t seed, int i, float angle) {
	return glm::mat3(glm::rotate(glm::mat4(1.0f), angle, CharmsRandDir(seed, i + 7919)));
}

// Caja fina de `a` a `b` (rayos, chispas, peldanos de la helice).
static void CharmsBeam(std::vector<CharmBox>& out, glm::vec3 a, glm::vec3 b, float thick, glm::vec4 fill, float fade) {
	glm::vec3 d = b - a;
	float len = glm::length(d);
	if (len < 1e-4f || fade <= 0.003f) return;
	glm::vec3 y = d / len;
	glm::vec3 helper = fabsf(y.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
	glm::vec3 x = glm::normalize(glm::cross(helper, y));
	glm::vec3 z = glm::cross(x, y);
	out.push_back({CharmsShell((a + b) * 0.5f, glm::vec3(thick * 0.5f, len * 0.5f, thick * 0.5f), glm::mat3(x, y, z)), fill, fade, 0.0f, CHB_PARTICLE});
}

// Fantasma con la forma del jugador: las seis partes del modelo, en pixeles de
// skin como WrBuildPlayer (pies en y = 0, mirando a +Z, la derecha en -X).
// `grow` lo agranda desde el centro del cuerpo y `arms` abre los brazos.
static void CharmsGhost(std::vector<CharmBox>& out, glm::vec3 feet, float yawDeg, float height, float grow, float arms,
						glm::vec4 fill, float fade, int batch) {
	if (fade <= 0.003f) return;
	float yaw = glm::radians(yawDeg);
	glm::mat3 R(glm::vec3(cosf(yaw), 0.0f, sinf(yaw)), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(-sinf(yaw), 0.0f, cosf(yaw)));
	float px = ImClamp(height, 0.5f, 3.0f) / 32.0f;  // el modelo mide 32 pixeles
	struct Part { glm::vec3 lo, size, pivot; float roll; };
	const Part parts[6] = {
		{{-4, 24, -4}, {8, 8, 8}, {0, 24, 0}, 0.0f},     // cabeza
		{{-4, 12, -2}, {8, 12, 4}, {0, 12, 0}, 0.0f},    // torso
		{{-8, 12, -2}, {4, 12, 4}, {-6, 22, 0}, -arms},  // brazo derecho
		{{4, 12, -2}, {4, 12, 4}, {6, 22, 0}, arms},     // brazo izquierdo
		{{-4, 0, -2}, {4, 12, 4}, {-2, 12, 0}, 0.0f},    // pierna derecha
		{{0, 0, -2}, {4, 12, 4}, {2, 12, 0}, 0.0f},      // pierna izquierda
	};
	const glm::vec3 middle(0.0f, 16.0f, 0.0f);
	for (const Part& p : parts) {
		glm::mat3 roll(glm::rotate(glm::mat4(1.0f), p.roll, glm::vec3(0, 0, 1)));
		glm::vec3 center = p.pivot + roll * (p.lo + p.size * 0.5f - p.pivot);
		glm::vec3 local = (center - middle) * grow + middle;
		out.push_back({CharmsShell(feet + R * (local * px), p.size * (0.5f * px * grow), R * roll), fill, fade, 1.0f, batch});
	}
}

// ---------------------------------------------------------------------------
// Efectos (edad en segundos, base ya en el espacio del render que dibuja)
// ---------------------------------------------------------------------------
static void CharmsBuildPop(std::vector<CharmBox>& out, const CharmFx& f, glm::vec3 base, float age, double now) {
	float t = age / f.duration;
	float rise = charmsPopRise * CharmsEaseOut(t);
	glm::vec4 fill = CharmsTint(charmsPopFill, 0, 0, now);  // mismo tono que el borde (CharmsLineColor)
	CharmsGhost(out, base + glm::vec3(0.0f, rise, 0.0f), f.yaw, f.height, 1.0f + charmsPopExpand * t, 0.45f * CharmsEaseOut(t),
				fill, CharmsFade(t, age, 0.06f), CHB_POP);
}

static void CharmsBuildDeath(std::vector<CharmBox>& out, const CharmFx& f, glm::vec3 base, float age, double now) {
	float t = age / f.duration, S = charmsDeathSize, H = f.height;
	glm::vec4 c = CharmsTint(charmsDeathColor, f.seed, 0, now);
	switch (f.mode) {
		case CHD_SOUL: {
			// el alma sube girando con los brazos abiertos, entre luces que suben
			float e = CharmsEaseOut(t);
			glm::vec4 ghost = CharmsTint(charmsDeathColor, 0, 0, now);  // mismo tono que el borde
			ghost.w *= 0.4f;
			CharmsGhost(out, base + glm::vec3(0.0f, 2.6f * S * e, 0.0f), f.yaw + 220.0f * t, H, 1.0f + 0.12f * t, 1.1f * e,
						ghost, CharmsFade(t, age, 0.08f, 1.2f), CHB_DEATH);
			for (int i = 0; i < 14; i++) {
				float delay = CharmsRand(f.seed, i, 2) * 0.35f * f.duration, a = age - delay;
				if (a <= 0.0f) continue;
				float life = f.duration - delay, ti = a / life, ph = CharmsRand(f.seed, i, 3) * 6.2831853f;
				glm::vec3 start((CharmsRand(f.seed, i, 4) - 0.5f) * 0.9f, CharmsRand(f.seed, i, 5) * H, (CharmsRand(f.seed, i, 6) - 0.5f) * 0.9f);
				glm::vec3 p = base + start + glm::vec3(sinf(a * 3.0f + ph) * 0.25f, a * (0.9f + CharmsRand(f.seed, i, 7)) * S, cosf(a * 2.6f + ph) * 0.25f);
				CharmsCube(out, p, (0.035f + 0.025f * CharmsRand(f.seed, i, 8)) * S, CharmsSpin(f.seed, i, a * 3.0f),
						   CharmsTint(charmsDeathColor, f.seed, i, now), CharmsFade(ti, a, 0.1f));
			}
			break;
		}
		case CHD_LIGHTNING: {
			// rayo que chisporrotea (cambia de forma cada 70 ms) y el impacto en el piso
			float flash = ImMin(0.55f, f.duration * 0.5f);
			if (age < flash) {
				uint32_t bolt = f.seed + (uint32_t)(age / 0.07f) * 7919u;
				float flick = fmodf(age * 16.0f, 1.0f) < 0.72f ? 1.0f : 0.35f;
				float a = (1.0f - age / flash) * flick;
				glm::vec4 core = CharmsLighten(c, 0.55f);
				core.w = 1.0f;
				const int segs = 12;
				glm::vec3 top = base + glm::vec3((CharmsRand(bolt, 0, 0) - 0.5f) * 3.0f * S, 14.0f * S, (CharmsRand(bolt, 0, 1) - 0.5f) * 3.0f * S);
				glm::vec3 bottom = base + glm::vec3(0.0f, 0.05f, 0.0f);
				glm::vec3 prev = top;
				for (int j = 1; j <= segs; j++) {
					float k = (float)j / segs;
					glm::vec3 p = top + (bottom - top) * k;
					if (j < segs) {
						float jitter = 1.3f * S * (1.0f - 0.5f * k);
						p += glm::vec3((CharmsRand(bolt, j, 2) - 0.5f) * jitter, 0.0f, (CharmsRand(bolt, j, 3) - 0.5f) * jitter);
					}
					CharmsBeam(out, prev, p, 0.09f * S, core, a);
					// dos ramas que se abren hacia abajo
					if (j == 4 || j == 8) {
						glm::vec3 b = p, side = glm::normalize(glm::vec3(CharmsRand(bolt, j, 4) - 0.5f, 0.0f, CharmsRand(bolt, j, 5) - 0.5f) + glm::vec3(1e-3f));
						for (int m = 0; m < 4; m++) {
							glm::vec3 n = b + side * (0.45f * S) + glm::vec3((CharmsRand(bolt, j * 8 + m, 6) - 0.5f) * 0.4f * S, -0.65f * S, (CharmsRand(bolt, j * 8 + m, 7) - 0.5f) * 0.4f * S);
							CharmsBeam(out, b, n, 0.05f * S, c, a * 0.8f);
							b = n;
						}
					}
					prev = p;
				}
			}
			// chispas y anillo en el piso
			for (int i = 0; i < 18; i++) {
				float life = f.duration * (0.4f + 0.4f * CharmsRand(f.seed, i, 2)), ti = age / life;
				if (ti >= 1.0f) continue;
				glm::vec3 d = CharmsRandDir(f.seed, i);
				glm::vec3 v = glm::normalize(glm::vec3(d.x, fabsf(d.y) * 0.7f + 0.6f, d.z)) * (3.0f + 3.0f * CharmsRand(f.seed, i, 3)) * S;
				glm::vec3 p = CharmsBallistic(base + glm::vec3(0.0f, 0.1f, 0.0f), v, age, 2.0f, 9.0f);
				CharmsCube(out, p, (0.035f + 0.02f * CharmsRand(f.seed, i, 4)) * S * (1.0f - 0.5f * ti), CharmsSpin(f.seed, i, age * 8.0f),
						   CharmsLighten(CharmsTint(charmsDeathColor, f.seed, i, now), 0.3f), CharmsFade(ti, age, 0.0f));
			}
			float ring = 0.3f + 2.8f * S * CharmsEaseOut(t);
			for (int i = 0; i < 24; i++) {
				float ang = (float)i / 24.0f * 6.2831853f;
				glm::mat3 rot(glm::rotate(glm::mat4(1.0f), -ang, glm::vec3(0, 1, 0)));
				CharmsCube(out, base + glm::vec3(cosf(ang) * ring, 0.06f, sinf(ang) * ring), 0.045f * S * (1.0f - 0.5f * t), rot, c, CharmsFade(t, age, 0.0f, 1.5f));
			}
			break;
		}
		case CHD_BURST: {
			glm::vec3 center = base + glm::vec3(0.0f, H * 0.5f, 0.0f);
			for (int i = 0; i < 40; i++) {
				float life = f.duration * (0.55f + 0.45f * CharmsRand(f.seed, i, 2)), ti = age / life;
				if (ti >= 1.0f) continue;
				glm::vec3 d = CharmsRandDir(f.seed, i);
				glm::vec3 v = d * (2.5f + 3.5f * CharmsRand(f.seed, i, 3)) * S + glm::vec3(0.0f, 2.0f * S, 0.0f);
				glm::vec3 p = CharmsBallistic(center + d * 0.2f, v, age, 1.6f, 9.0f);
				glm::vec4 col = CharmsTint(charmsDeathColor, f.seed, i, now);
				float shade = 0.75f + 0.5f * CharmsRand(f.seed, i, 4);
				col = glm::vec4(ImSaturate(col.x * shade), ImSaturate(col.y * shade), ImSaturate(col.z * shade), col.w);
				CharmsCube(out, p, (0.03f + 0.04f * CharmsRand(f.seed, i, 5)) * S * (1.0f - 0.5f * ti),
						   CharmsSpin(f.seed, i, age * (4.0f + 6.0f * CharmsRand(f.seed, i, 6))), col, CharmsFade(ti, age, 0.0f, 1.2f));
			}
			break;
		}
		case CHD_HELIX: {
			// dos hebras que suben girando, con peldanos entre ellas
			const int n = 18;
			float fade = CharmsFade(t, age, 0.0f, 1.3f), r = 0.75f * S * (1.0f - 0.35f * t);
			for (int j = 0; j < n; j++) {
				float k = (float)j / n;
				if (t * 2.2f < k) continue;
				glm::vec3 p[2];
				for (int s = 0; s < 2; s++) {
					float ang = j * 0.55f + age * 4.5f + s * 3.1415927f;
					p[s] = base + glm::vec3(cosf(ang) * r, k * H * 1.35f + t * 1.2f * S, sinf(ang) * r);
					glm::vec4 col = CharmsTint(charmsDeathColor, f.seed, j + s * 9, now);
					if (s == 1) col = CharmsLighten(col, 0.35f);
					CharmsCube(out, p[s], 0.04f * S, CharmsSpin(f.seed, j * 2 + s, age * 3.0f), col, fade);
				}
				if (j % 3 == 0) {
					glm::vec4 rung = c;
					rung.w *= 0.5f;
					CharmsBeam(out, p[0], p[1], 0.025f * S, rung, fade);
				}
			}
			break;
		}
		case CHD_NOVA: {
			// dos anillos que se abren y una columna de luz
			float e = CharmsEaseOut(t), fade = CharmsFade(t, age, 0.0f);
			for (int ringId = 0; ringId < 2; ringId++) {
				int n = ringId == 0 ? 32 : 24;
				float radius = ringId == 0 ? 0.3f + 3.4f * S * e : 0.2f + 2.2f * S * e;
				float y = ringId == 0 ? 0.08f : H * 0.55f + 0.5f * t * S;
				for (int i = 0; i < n; i++) {
					float ang = (float)i / n * 6.2831853f + (ringId ? age * 1.5f : 0.0f);
					glm::mat3 rot(glm::rotate(glm::mat4(1.0f), -ang, glm::vec3(0, 1, 0)));
					CharmsCube(out, base + glm::vec3(cosf(ang) * radius, y, sinf(ang) * radius), 0.045f * S * (1.0f - 0.4f * t), rot,
							   CharmsTint(charmsDeathColor, f.seed, i + ringId * 40, now), fade);
				}
			}
			glm::vec4 pillar = CharmsLighten(c, 0.4f);
			pillar.w *= 0.55f;
			CharmsBeam(out, base, base + glm::vec3(0.0f, 5.0f * S * (0.3f + 0.7f * e), 0.0f), 0.22f * S * (1.0f - t), pillar, fade);
			break;
		}
	}
}

static void CharmsBuildHit(std::vector<CharmBox>& out, const CharmFx& f, glm::vec3 base, float age, double now) {
	float t = age / f.duration, S = charmsHitSize, sp = charmsHitSpeed;
	int n = ImClamp(charmsHitCount, 1, 40);
	const glm::vec3 up(0.0f, 1.0f, 0.0f);
	switch (f.mode) {
		case CHH_CUBES:
		case CHH_SPARKS: {
			bool sparks = f.mode == CHH_SPARKS;
			float drag = sparks ? 4.5f : 2.2f, gravity = sparks ? 2.5f : 8.0f;
			for (int i = 0; i < n; i++) {
				float life = f.duration * (sparks ? 0.4f + 0.4f * CharmsRand(f.seed, i, 2) : 0.55f + 0.45f * CharmsRand(f.seed, i, 2));
				float ti = age / life;
				if (ti >= 1.0f) continue;
				glm::vec3 d = CharmsRandDir(f.seed, i);
				glm::vec3 v = glm::normalize(d + f.dir * (sparks ? 1.3f : 1.1f) + up * (sparks ? 0.5f : 0.8f)) *
							  ((sparks ? 4.0f : 2.2f) + (sparks ? 4.0f : 2.6f) * CharmsRand(f.seed, i, 3)) * sp;
				glm::vec3 p = CharmsBallistic(base + d * 0.08f, v, age, drag, gravity);
				glm::vec4 col = CharmsTint(charmsHitColor, f.seed, i, now);
				float fade = CharmsFade(ti, age, 0.0f, 1.3f);
				if (sparks) {
					glm::vec3 vel = CharmsVelocity(v, age, drag, gravity);
					float speed = glm::length(vel);
					if (speed < 1e-3f) continue;
					float len = ImClamp(speed * 0.06f, 0.03f, 0.4f);
					CharmsBeam(out, p - vel / speed * len, p, S * 0.35f, CharmsLighten(col, 0.35f), fade);
				} else {
					CharmsCube(out, p, S * 0.5f * (0.6f + 0.7f * CharmsRand(f.seed, i, 4)) * (1.0f - 0.55f * ti),
							   CharmsSpin(f.seed, i, age * (5.0f + 6.0f * CharmsRand(f.seed, i, 5))), col, fade);
				}
			}
			break;
		}
		case CHH_RING: {
			int m = ImMax(8, n * 2);
			float radius = 0.15f + 1.1f * sp * CharmsEaseOut(t), fade = CharmsFade(t, age, 0.0f, 1.5f);
			for (int i = 0; i < m; i++) {
				float ang = (float)i / m * 6.2831853f;
				glm::mat3 rot(glm::rotate(glm::mat4(1.0f), -ang, up));
				CharmsCube(out, base + glm::vec3(cosf(ang) * radius, 0.25f * t, sinf(ang) * radius), S * 0.4f * (1.0f - 0.5f * t), rot,
						   CharmsTint(charmsHitColor, f.seed, i, now), fade);
			}
			break;
		}
	}
}

// Arma todas las cajas de este momento. `cam` y `viewPos` pasan del mundo al
// espacio del render (en el overlay van en cero: coordenadas del mundo).
static bool CharmsCollect(double now, glm::vec3 cam, glm::vec3 viewPos, std::vector<CharmBox>& out) {
	std::vector<CharmFx> fx;
	{
		std::lock_guard<std::mutex> lock(g_charmsMutex);
		g_charmsFx.erase(std::remove_if(g_charmsFx.begin(), g_charmsFx.end(), [now](const CharmFx& f) {
			return now - f.start >= f.duration;
		}), g_charmsFx.end());
		fx = g_charmsFx;
	}
	for (const CharmFx& f : fx) {
		if (out.size() > kCharmsMaxBoxes) break;
		float age = ImMax(0.0f, (float)(now - f.start));
		// la resta entre coordenadas grandes va primero, para no perder precision
		glm::vec3 base = (f.origin - cam) + viewPos;
		if (f.kind == CHARM_POP) CharmsBuildPop(out, f, base, age, now);
		else if (f.kind == CHARM_DEATH) CharmsBuildDeath(out, f, base, age, now);
		else CharmsBuildHit(out, f, base, age, now);
	}
	return !out.empty();
}

// De la mas lejana a la mas cercana (sin profundidad propia, la mezcla va en orden).
static void CharmsSort(std::vector<CharmBox>& boxes, glm::vec3 eye) {
	std::vector<std::pair<float, size_t>> order(boxes.size());
	for (size_t i = 0; i < boxes.size(); i++) {
		glm::vec3 center = (boxes[i].shell.corners[0] + boxes[i].shell.corners[6]) * 0.5f;
		glm::vec3 d = center - eye;
		order[i] = {glm::dot(d, d), i};
	}
	std::sort(order.begin(), order.end(), [](const std::pair<float, size_t>& a, const std::pair<float, size_t>& b) { return a.first > b.first; });
	std::vector<CharmBox> sorted;
	sorted.reserve(boxes.size());
	for (const auto& o : order) sorted.push_back(boxes[o.second]);
	boxes.swap(sorted);
}

// Color del borde de cada grupo.
static glm::vec4 CharmsLineColor(int batch, double now) {
	if (batch == CHB_POP) return CharmsTint(charmsPopLine, 0, 0, now);
	glm::vec4 c = CharmsTint(charmsDeathColor, 0, 0, now);
	return glm::vec4(c.x, c.y, c.z, ImMin(1.0f, c.w + 0.1f));
}

// ---------------------------------------------------------------------------
// Render del mundo (hilo de render del juego, desde WrFlushChams)
// ---------------------------------------------------------------------------
static void CharmsDrawWorld(const WrChamsPass& pass) {
	if (!g_wrChamsProgram) return;
	double now = GatyoNow();
	std::vector<CharmBox> boxes;
	boxes.reserve(256);
	if (!CharmsCollect(now, pass.cam, pass.viewPos, boxes)) return;
	// el ojo en espacio de render es ViewPos (la camara va al origen)
	const glm::vec3& eye = pass.viewPos;
	CharmsSort(boxes, eye);
	auto byte = [](float v) { return (uint8_t)(ImSaturate(v) * 255.0f); };
	std::vector<WrChamsVertex> verts[CHB_COUNT];
	for (const CharmBox& b : boxes) {
		std::vector<WrChamsVertex>& out = verts[b.batch];
		uint8_t r = byte(b.fill.x), g = byte(b.fill.y), bl = byte(b.fill.z), a = byte(b.fill.w);
		// caras de atras y despues las de adelante
		WrChamsEmitShell(out, b.shell, eye, false, b.fade, b.edge, r, g, bl, a);
		WrChamsEmitShell(out, b.shell, eye, true, b.fade, b.edge, r, g, bl, a);
	}

	glBindVertexArray(g_wrChamsVao);
	glBindBuffer(GL_ARRAY_BUFFER, g_wrChamsVbo);
	glUseProgram(g_wrChamsProgram);
	glUniformMatrix4fv(g_wrChamsLocMvp, 1, GL_FALSE, &pass.mvp[0][0]);
	glUniform3f(g_wrChamsLocEye, eye.x, eye.y, eye.z);
	glUniform1f(g_wrChamsLocWidth, 1.3f * ImMax(1.0f, (float)pass.viewport[3] / 900.0f));
	glUniform1f(g_wrChamsLocTime, (float)fmod(now, 1000.0));
	// fantasmas primero, las particulas encima
	static const int order[CHB_COUNT] = {CHB_DEATH, CHB_POP, CHB_PARTICLE};
	for (int batch : order) {
		if (verts[batch].empty()) continue;
		glm::vec4 line = CharmsLineColor(batch, now);
		glUniform4f(g_wrChamsLocLine, line.x, line.y, line.z, line.w);
		glUniform1f(g_wrChamsLocFancy, batch == CHB_DEATH || (batch == CHB_POP && charmsPopGlow) ? 1.0f : 0.0f);
		glBufferData(GL_ARRAY_BUFFER, verts[batch].size() * sizeof(WrChamsVertex), verts[batch].data(), GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts[batch].size());
	}
}

// ---------------------------------------------------------------------------
// Fallback por overlay (hilo de ImGui)
// ---------------------------------------------------------------------------
static void RenderCharmsOverlay(ImDrawList* dl) {
	if (!charms || !g_viewValid || CharmsWorldActive()) return;
	double now = GatyoNow();
	if (now >= g_charmsBusyUntil.load()) return;
	std::vector<CharmBox> boxes;
	if (!CharmsCollect(now, glm::vec3(0.0f), glm::vec3(0.0f), boxes)) return;
	CharmsSort(boxes, g_view.cam);
	glm::vec4 lines[CHB_COUNT];
	for (int i = 0; i < CHB_COUNT; i++) lines[i] = CharmsLineColor(i, now);
	float th = ImMax(1.0f, 1.2f * scaleGlobal);
	for (const CharmBox& b : boxes) {
		ImVec4 fill(b.fill.x, b.fill.y, b.fill.z, ImSaturate(b.fill.w * b.fade));
		ImU32 edge = 0;
		if (b.edge > 0.0f) {
			const glm::vec4& l = lines[b.batch];
			edge = GatyoU32(ImVec4(l.x, l.y, l.z, ImSaturate(l.w * b.fade * b.edge)));
		}
		GatyoSolidBox3D(dl, g_view, b.shell.corners, fill, edge, th);
	}
}

// ---------------------------------------------------------------------------
// Avisos (hilo del juego; la prueba llega del hilo de ImGui)
// ---------------------------------------------------------------------------
static void CharmsPush(CharmFx fx) {
	if (!charms || panic) return;
	double now = GatyoNow();
	fx.start = now;
	fx.seed = CharmsHash(g_charmsCounter.fetch_add(1));
	fx.duration = ImMax(0.1f, fx.duration);
	std::lock_guard<std::mutex> lock(g_charmsMutex);
	g_charmsFx.erase(std::remove_if(g_charmsFx.begin(), g_charmsFx.end(), [now](const CharmFx& f) {
		return now - f.start >= f.duration;
	}), g_charmsFx.end());
	if ((int)g_charmsFx.size() >= kCharmsMaxFx) g_charmsFx.erase(g_charmsFx.begin());
	g_charmsFx.push_back(fx);
	double until = now + fx.duration + 0.25;
	if (until > g_charmsBusyUntil.load()) g_charmsBusyUntil = until;
}

static void CharmsSpawnPop(glm::vec3 feet, float yaw, float height) {
	CharmFx fx;
	fx.kind = CHARM_POP;
	fx.origin = feet;
	fx.yaw = yaw;
	fx.height = height;
	fx.duration = charmsPopDuration;
	CharmsPush(fx);
}

static void CharmsSpawnDeath(glm::vec3 feet, float yaw, float height) {
	CharmFx fx;
	fx.kind = CHARM_DEATH;
	fx.mode = ImClamp(charmsDeathMode, 0, CHD_COUNT - 1);
	fx.origin = feet;
	fx.yaw = yaw;
	fx.height = height;
	fx.duration = charmsDeathDuration;
	CharmsPush(fx);
}

// `toward`: desde el objetivo hacia el atacante.
static void CharmsSpawnHit(glm::vec3 point, glm::vec3 toward) {
	CharmFx fx;
	fx.kind = CHARM_HIT;
	fx.mode = ImClamp(charmsHitMode, 0, CHH_COUNT - 1);
	fx.origin = point;
	glm::vec3 flat(toward.x, 0.0f, toward.z);
	fx.dir = glm::length(flat) > 1e-3f ? glm::normalize(flat) : glm::vec3(0.0f);
	fx.duration = charmsHitDuration;
	CharmsPush(fx);
}

// Lo ultimo que se vio de cada jugador (pops y muertes llegan entre ticks).
struct CharmsSeen {
	glm::vec3 feet;
	float yaw, height;
	int health;
	bool isFriend;
};
static std::unordered_map<std::string, CharmsSeen> g_charmsSeen;  // solo hilo del juego
static std::unordered_set<std::string> g_charmsDead;             // ya tuvieron su efecto hasta que revivan
static CharmsSeen g_charmsSelf = {glm::vec3(0.0f), 0.0f, 1.8f, 0, false};
static std::string g_charmsSelfName;
static uintptr_t g_charmsHitId = 0;
static double g_charmsHitTime = -100.0;

void CharmsReset() {
	g_charmsSeen.clear();
	g_charmsDead.clear();
	g_charmsSelf = {glm::vec3(0.0f), 0.0f, 1.8f, 0, false};
	g_charmsSelfName.clear();
	g_charmsHitId = 0;
	std::lock_guard<std::mutex> lock(g_charmsMutex);
	g_charmsFx.clear();
}

static const CharmsSeen* CharmsFind(const std::string& name, bool self) {
	if (self || (!g_charmsSelfName.empty() && name == g_charmsSelfName)) return charmsSelf && !g_charmsSelfName.empty() ? &g_charmsSelf : nullptr;
	auto it = g_charmsSeen.find(name);
	if (it == g_charmsSeen.end()) return nullptr;
	if (it->second.isFriend && !charmsFriends) return nullptr;
	return &it->second;
}

// PopCounter: un totem usado (ya sin repetir entre sonido y ActorEvent).
void CharmsOnPop(const std::string& name, bool self) {
	if (!charms || !charmsPop || panic) return;
	const CharmsSeen* s = CharmsFind(name, self);
	if (s) CharmsSpawnPop(s->feet, s->yaw, s->height);
}

// La vida en 0 o el ActorEvent DEATH: un efecto por muerte.
void CharmsOnDeath(const std::string& name, bool self) {
	if (!charms || !charmsDeath || panic || name.empty()) return;
	if (!g_charmsDead.insert(name).second) return;
	const CharmsSeen* s = CharmsFind(name, self);
	if (s) CharmsSpawnDeath(s->feet, s->yaw, s->height);
}

// Punto del golpe: a la altura del pecho, del lado del atacante.
static void CharmsHitAt(uintptr_t id, glm::vec3 feet, float height, int hurtTime) {
	LocalPlayer* lp = localPlayer;
	if (!lp || (charmsHitDamageOnly && hurtTime > 0)) return;
	// las auras y el toque pueden llamar a attack dos veces por golpe
	double now = GatyoNow();
	if (id == g_charmsHitId && now - g_charmsHitTime < 0.15) return;
	g_charmsHitId = id;
	g_charmsHitTime = now;
	vec3_t eyes = *lp->getPosition();
	glm::vec3 toward = glm::vec3(eyes.x, eyes.y, eyes.z) - feet;
	glm::vec3 flat(toward.x, 0.0f, toward.z);
	glm::vec3 side = glm::length(flat) > 1e-3f ? glm::normalize(flat) : glm::vec3(0.0f);
	CharmsSpawnHit(feet + glm::vec3(0.0f, height * 0.62f, 0.0f) + side * 0.3f, toward);
}

// GameMode::attack del jugador local (visuals_gatyo.h), antes de seguir al juego.
void CharmsOnAttack(Actor* target) {
	// el vuelo por daño se golpea a si mismo cada tick
	if (!charms || !charmsHit || panic || !target || target == (Actor*)localPlayer) return;
	int type = target->getEntityTypeId();
	bool player = type == kCharmsTypePlayer;
	if (!player && !(charmsHitMobs && (type & kCharmsTypeMob))) return;
	vec3_t* pos = target->getPosition();
	vec2_t* dims = target->getAABBDim();
	if (!pos) return;
	float height = dims && dims->y > 0.1f ? dims->y : 1.8f;
	bool npc = FakePlayerIsNpc(target);
	int hurt = npc ? FakePlayerHurtTime() : ((Mob*)target)->getHurtTime();
	CharmsHitAt((uintptr_t)target, glm::vec3(pos->x, pos->y - (player ? kCharmsEyeHeight : 0.0f), pos->z), height, hurt);
}

// Golpe al jugador falso en modo virtual (no pasa por GameMode::attack).
void CharmsOnFakeHit(const vec3_t& feet, int hurtTime) {
	if (!charms || !charmsHit || panic) return;
	CharmsHitAt(1, glm::vec3(feet.x, feet.y, feet.z), 1.8f, hurtTime);
}

// Desde NormalTick, con nearPlayerList ya armada (el jugador falso incluido).
void CharmsOnNormalTick(LocalPlayer* lp) {
	if (!charms || panic || !lp) {
		if (!g_charmsSeen.empty() || !g_charmsDead.empty()) {
			g_charmsSeen.clear();
			g_charmsDead.clear();
		}
		return;
	}
	std::unordered_map<std::string, CharmsSeen> seen;
	seen.reserve(nearPlayerList.size());
	for (const PlayerStruct& p : nearPlayerList) {
		if (p.isBot) continue;
		CharmsSeen s = {glm::vec3(p.position.x, p.position.y - kCharmsEyeHeight, p.position.z), p.yaw,
						p.hitBox.y > 0.1f ? p.hitBox.y : 1.8f, p.health, p.isFriend};
		auto prev = g_charmsSeen.find(p.nickname);
		seen[p.nickname] = s;
		if (p.health > 0) {
			g_charmsDead.erase(p.nickname);
		} else if (prev != g_charmsSeen.end() && prev->second.health > 0) {
			g_charmsSeen[p.nickname] = s;  // donde murio, no donde estaba el tick anterior
			CharmsOnDeath(p.nickname, false);
		}
	}
	g_charmsSeen.swap(seen);

	vec3_t eyes = *lp->getPosition();
	vec2_t* rot = lp->getRotation();
	int health = lp->getHealth();
	int prevHealth = g_charmsSelf.health;
	g_charmsSelfName = lp->getName();
	g_charmsSelf = {glm::vec3(eyes.x, eyes.y - kCharmsEyeHeight, eyes.z), rot ? rot->y : 0.0f, 1.8f, health, false};
	if (health > 0) g_charmsDead.erase(g_charmsSelfName);
	else if (prevHealth > 0) CharmsOnDeath(g_charmsSelfName, true);
}

// ---------------------------------------------------------------------------
// Ajustes (pagina Visuals y panel del ClickGUI)
// ---------------------------------------------------------------------------
// Prueba: los efectos encendidos, a tres bloques delante tuyo.
static void CharmsPreview() {
	if (!g_visuals.inWorld) return;
	float yaw = glm::radians(g_visuals.yaw);
	glm::vec3 feet(g_visuals.feet.x, g_visuals.feet.y, g_visuals.feet.z);
	glm::vec3 forward(-sinf(yaw), 0.0f, cosf(yaw)), right(cosf(yaw), 0.0f, sinf(yaw));
	glm::vec3 front = feet + forward * 3.5f;
	if (charmsPop) CharmsSpawnPop(front, g_visuals.yaw + 180.0f, 1.8f);
	if (charmsDeath) CharmsSpawnDeath(front + right * 2.5f, g_visuals.yaw + 180.0f, 1.8f);
	if (charmsHit) CharmsSpawnHit(front - right * 2.5f + glm::vec3(0.0f, 1.1f, 0.0f), -forward);
}

void DrawCharmsSettings(float width) {
	ImGui::PushItemWidth(width);
	ImGui::Checkbox(TL(S_CharmsSelf), &charmsSelf);
	Tip(S_TipCharmsSelf);
	ImGui::Checkbox(TL(S_CharmsFriends), &charmsFriends);
	ImGui::Checkbox(TL(S_CharmsRainbow), &charmsRainbow);
	Tip(S_TipChamsRgb);

	ImGui::Checkbox(TL(S_CharmsPop), &charmsPop);
	Tip(S_TipCharmsPop);
	if (charmsPop) {
		ImGui::PushID("charmsPop");
		ImGui::SliderFloat(TL(S_CharmsDuration), &charmsPopDuration, 0.3f, 5.0f, "%.1f");
		ImGui::SliderFloat(TL(S_CharmsRise), &charmsPopRise, 0.0f, 3.0f, "%.1f");
		ImGui::SliderFloat(TL(S_CharmsExpand), &charmsPopExpand, 0.0f, 1.0f, "%.2f");
		ImGui::Checkbox(TL(S_ChamsFancy), &charmsPopGlow);
		ImGui::ColorEdit4(TL(S_ChamsFillColor), charmsPopFill, ImGuiColorEditFlags_NoInputs);
		ImGui::ColorEdit4(TL(S_ChamsLineColor), charmsPopLine, ImGuiColorEditFlags_NoInputs);
		ImGui::PopID();
	}

	ImGui::Checkbox(TL(S_CharmsDeath), &charmsDeath);
	Tip(S_TipCharmsDeath);
	if (charmsDeath) {
		ImGui::PushID("charmsDeath");
		static const StrId modes[CHD_COUNT] = {S_CharmsSoul, S_CharmsLightning, S_CharmsBurst, S_CharmsHelix, S_CharmsNova};
		ComboT(TL(S_CharmsStyle), &charmsDeathMode, modes, IM_ARRAYSIZE(modes));
		ImGui::SliderFloat(TL(S_CharmsDuration), &charmsDeathDuration, 0.5f, 5.0f, "%.1f");
		ImGui::SliderFloat(TL(S_CharmsSize), &charmsDeathSize, 0.3f, 3.0f, "%.2f");
		ImGui::ColorEdit4(TL(S_Color), charmsDeathColor, ImGuiColorEditFlags_NoInputs);
		ImGui::PopID();
	}

	ImGui::Checkbox(TL(S_CharmsHit), &charmsHit);
	Tip(S_TipCharmsHit);
	if (charmsHit) {
		ImGui::PushID("charmsHit");
		static const StrId modes[CHH_COUNT] = {S_CharmsCubes, S_CharmsSparks, S_CharmsRing};
		ComboT(TL(S_CharmsStyle), &charmsHitMode, modes, IM_ARRAYSIZE(modes));
		ImGui::SliderInt(TL(S_CharmsCount), &charmsHitCount, 1, 40);
		ImGui::SliderFloat(TL(S_CharmsSize), &charmsHitSize, 0.03f, 0.3f, "%.2f");
		ImGui::SliderFloat(TL(S_CharmsSpeed), &charmsHitSpeed, 0.2f, 3.0f, "%.1f");
		ImGui::SliderFloat(TL(S_CharmsDuration), &charmsHitDuration, 0.2f, 2.0f, "%.1f");
		ImGui::Checkbox(TL(S_CharmsDamageOnly), &charmsHitDamageOnly);
		Tip(S_TipCharmsDamageOnly);
		ImGui::Checkbox(TL(S_CharmsMobs), &charmsHitMobs);
		ImGui::ColorEdit4(TL(S_Color), charmsHitColor, ImGuiColorEditFlags_NoInputs);
		ImGui::PopID();
	}
	ImGui::PopItemWidth();

	ImGui::BeginDisabled(!charms || !g_visuals.inWorld);
	if (ImGui::Button(TL(S_CharmsTest), ImVec2(width > 0.0f ? width : -1.0f, 0))) CharmsPreview();
	ImGui::EndDisabled();
	Tip(S_TipCharmsTest);
}
