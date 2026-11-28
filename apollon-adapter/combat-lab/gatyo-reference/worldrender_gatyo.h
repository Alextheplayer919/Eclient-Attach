#pragma once
// Render dentro del juego: geometria propia dibujada en el contexto GL de
// Minecraft, en medio del pase del mundo, con la misma proyeccion y el mismo
// buffer de profundidad. Asi los bloques la tapan, a diferencia del overlay de
// ImGui (otra ventana, sin profundidad).
//
// Como funciona: el hook de glDrawElementsInstanced (Chams.h) llama a
// WorldRenderOnDraw en cada dibujo del juego. En el primer dibujo opaco del
// terreno de cada frame (programa de RenderChunk: tiene u_proj, u_view,
// ViewPositionAndTime y s_LightMapTexture) se leen esas matrices del programa
// actual, se dibuja lo nuestro y se restaura todo el estado GL que se toco
// (bgfx cachea su estado; si algo queda distinto, el juego dibuja mal).
//
// Espacio de render: los shaders del juego hacen gl_Position = u_proj * u_view
// * pos, con pos relativo a un origen cercano a la camara, y la camara esta en
// ViewPositionAndTime.xyz. Un punto del mundo p va a (p - camara) + ViewPos.
// En el juego medido ViewPos es 0: el mundo se dibuja con la camara en el
// origen.
//
// La camara tiene que ser la de este frame exacto. LevelRendererPlayer la da
// adelantada o atrasada un frame (el juego ya prepara el siguiente mientras se
// dibuja este), y eso hacia temblar lo nuestro al mover la camara. El u_model
// del chunk que se esta dibujando es su esquina (multiplo de 16) menos la
// camara del frame: redondeando se recupera la camara exacta.
//
// Corre en el hilo de render del juego (bgfx), no en el de NormalTick: lo que
// viene del juego se lee de g_worldShared con g_worldMutex.

#include "Images/steve_skin.h"
#include "glm/gtc/type_ptr.hpp"

static std::atomic<double> g_worldRenderLast{-100.0};  // ultima vez que se dibujo dentro del juego
static std::atomic<int> g_gameRenderTid{0};
static std::atomic<unsigned> g_gameFrame{0};

// Dibujo dentro del juego activo hace poco: el overlay deja de dibujar su
// version sin profundidad.
bool WorldRenderActive() {
	return GatyoNow() - g_worldRenderLast.load() < 0.5;
}

// eglSwapBuffers del hilo de render del juego marca el fin de cada frame. El
// overlay de Gatyo tambien pasa por aca desde su hilo; esos no cuentan.
EGLBoolean (*old_eglSwapBuffers)(EGLDisplay display, EGLSurface surface);
EGLBoolean new_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
	if (gettid() == g_gameRenderTid.load()) g_gameFrame++;
	return old_eglSwapBuffers(display, surface);
}

void WorldRenderInstall() {
	void* swap = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
	if (swap) DobbyHook(swap, (void*)new_eglSwapBuffers, (void**)&old_eglSwapBuffers);
}

// ---------------------------------------------------------------------------
// Estado propio (solo en el hilo de render del juego)
// ---------------------------------------------------------------------------
struct WrProgramInfo {
	GLint proj = -1, view = -1, viewPos = -1, tileLight = -1, model = -1;
	bool terrain = false;
};
static std::unordered_map<GLint, WrProgramInfo> g_wrPrograms;
static unsigned g_wrDrawnFrame = ~0u, g_wrLightFrame = ~0u;
static double g_wrDrawnTime = -100.0;
static EGLContext g_wrContext = EGL_NO_CONTEXT;
static EGLContext g_wrFailedContext = EGL_NO_CONTEXT;  // no compilo en este contexto: no reintentar cada frame
static GLuint g_wrProgram = 0, g_wrVao = 0, g_wrVbo = 0, g_wrSkin = 0;
static GLint g_wrLocMvp = -1, g_wrLocTex = -1, g_wrLocOverlay = -1, g_wrLocLight = -1;
static float g_wrLight[3] = {1.0f, 1.0f, 1.0f};
static int g_wrProbeCount = 0;

struct WrVertex {
	float x, y, z, u, v;
	uint8_t r, g, b, a;
};

static const WrProgramInfo& WrProgram(GLint program) {
	auto it = g_wrPrograms.find(program);
	if (it != g_wrPrograms.end()) return it->second;
	WrProgramInfo info;
	info.proj = glGetUniformLocation(program, "u_proj");
	info.view = glGetUniformLocation(program, "u_view");
	info.viewPos = glGetUniformLocation(program, "ViewPositionAndTime");
	info.tileLight = glGetUniformLocation(program, "TileLightColor");
	info.model = glGetUniformLocation(program, "u_model");
	bool lightMap = glGetUniformLocation(program, "s_LightMapTexture") >= 0;
	bool skinned = old_glGetAttribLocation(program, "a_indices") >= 0;
	info.terrain = info.proj >= 0 && info.view >= 0 && info.viewPos >= 0 && info.model >= 0 && lightMap && !skinned;
	return g_wrPrograms.emplace(program, info).first->second;
}

static GLuint WrCompile(GLenum type, const char* source) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512] = {};
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		__android_log_print(ANDROID_LOG_ERROR, "Gatyo", "worldrender: shader: %s", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

// ---------------------------------------------------------------------------
// CrystalChams: cristales rellenos dentro del juego. El modulo (crystalchams
// en el hilo del juego) publica el listado en g_worldShared.crystals; aca el
// hilo de GL los dibuja con la profundidad del mundo. Si el contexto no sale
// (g_wrChamsAvailable = false) o el pase del mundo no deja lugar despues del
// terreno (g_wrChamsNoSlot), el overlay de ImGui dibuja el fallback.
//
// Van despues del terreno opaco, no en su primer dibujo como el jugador falso:
// son transparentes y no escriben profundidad, asi que dibujados antes el
// terreno de atras los tapaba y solo se veian contra el cielo. Ahora el
// terreno ya esta en el color y en la profundidad: el de adelante los tapa y
// el de atras se ve a traves.
//
// La animacion (giro, bob, fade al aparecer y al romperse) corre en el hilo de
// render, igual que la interpolacion del jugador falso.
// ---------------------------------------------------------------------------
static std::atomic<bool> g_crystalChamsWorldReady{false};  // el modulo tiene (o tuvo hace poco) cristales
static std::atomic<bool> g_wrChamsAvailable{true};         // el contexto compilo los shaders
static std::atomic<bool> g_wrChamsNoSlot{false};           // ningun dibujo despues del terreno sirvio

// Estado del overlay para decidir si dibuja su fallback (crystalchams lo lee).
bool CrystalChamsWorldActive() {
	return crystalChams && g_wrChamsAvailable.load() && !g_wrChamsNoSlot.load() && g_crystalChamsWorldReady.load();
}

// Capas del cristal, como el modelo del juego (ender_crystal.geo.json): vidrio
// de afuera, vidrio de adentro y nucleo, cada una girando sobre la anterior e
// inclinada 39.2 grados en X y en Z (asi queda parada sobre una punta). Las
// usan el render del mundo y el overlay.
static const int kCrystalShells = 3;
static const float kCrystalShellSide[kCrystalShells] = {1.0f, 0.78f, 0.52f};  // lado en bloques, a escala 1
static const float kCrystalCenterY = 1.2f;  // centro sobre los pies del cristal (el del juego va de 1 a 2)

struct CrystalShell {
	glm::vec3 corners[8];  // orden de GatyoBoxCorners
	glm::mat3 rot;
};

static void CrystalShells(glm::vec3 center, float scale, float spin, CrystalShell out[kCrystalShells]) {
	static const glm::mat3 tilt(glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(39.2f), glm::vec3(1, 0, 0)), glm::radians(-39.2f), glm::vec3(0, 0, 1)));
	static const float sx[8] = {-1, 1, 1, -1, -1, 1, 1, -1}, sy[8] = {-1, -1, -1, -1, 1, 1, 1, 1}, sz[8] = {-1, -1, 1, 1, -1, -1, 1, 1};
	glm::mat3 spinY(glm::rotate(glm::mat4(1.0f), spin, glm::vec3(0, 1, 0)));
	glm::mat3 r(1.0f);
	for (int i = 0; i < kCrystalShells; i++) {
		r = r * spinY * tilt;
		float half = 0.5f * kCrystalShellSide[i] * scale;
		out[i].rot = r;
		for (int k = 0; k < 8; k++) out[i].corners[k] = center + r * (glm::vec3(sx[k], sy[k], sz[k]) * half);
	}
}

// Normal hacia afuera de cada cara de kGatyoBoxFaces, antes de girar.
static const glm::vec3 kCrystalFaceNormals[6] = {
	{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0},
};

// Fase propia de cada cristal (el juego tambien le da un desfase al azar).
static float CrystalPhase(uintptr_t id) {
	uint32_t h = (uint32_t)(id ^ (id >> 32)) * 2654435761u;
	return (float)(h >> 8) / (float)(1u << 24) * 6.2831853f;
}

// Animacion de un cristal: aparece creciendo y se encoge al romperse (como el
// original), con la ultima posicion vista para poder dibujarlo mientras se
// desvanece.
struct CrystalAnim {
	glm::vec3 feet = glm::vec3(0.0f);
	float spin = 0.0f, bob = 0.0f, alpha = 0.0f;
	double lastSeen = -1000.0;
	bool live = false;
};

// Avanza las animaciones con el listado del frame. Devuelve false si ya no
// queda nada para dibujar.
static bool CrystalAnimate(std::unordered_map<uintptr_t, CrystalAnim>& anims, const std::vector<EntityStruct>& crystals, double now, float dt) {
	for (const EntityStruct& e : crystals) {
		auto it = anims.find(e.id);
		if (it == anims.end()) {
			CrystalAnim fresh;
			fresh.spin = CrystalPhase(e.id);
			fresh.bob = fresh.spin * 1.7f;
			it = anims.emplace(e.id, fresh).first;
		}
		it->second.feet = glm::vec3(e.position.x, e.position.y, e.position.z);
		it->second.lastSeen = now;
	}
	float fadeOut = ImMax(0.1f, (float)crystalChamsFadeDuration * 0.05f);
	for (auto it = anims.begin(); it != anims.end();) {
		CrystalAnim& a = it->second;
		a.live = a.lastSeen >= now;
		a.spin += dt * ImMax(0.0f, crystalChamsRotateSpeed);
		a.bob += dt * ImMax(0.0f, crystalChamsBobSpeed);
		if (a.spin > 62.831853f) a.spin -= 62.831853f;
		if (a.bob > 62.831853f) a.bob -= 62.831853f;
		if (a.live) {
			a.alpha = ImMin(1.0f, a.alpha + dt / 0.18f);
		} else if (crystalChamsFade) {
			a.alpha -= dt / fadeOut;
		} else {
			a.alpha = 0.0f;
		}
		if (a.alpha <= 0.0f && !a.live) it = anims.erase(it);
		else ++it;
	}
	if (anims.size() > 512) anims.clear();
	return !anims.empty();
}

static glm::vec3 CrystalCenter(const CrystalAnim& a) {
	float bob = (sinf(a.bob) * 0.5f + 0.5f) * crystalChamsBobAmount;
	return a.feet + glm::vec3(0.0f, kCrystalCenterY + bob, 0.0f);
}

static float CrystalScale(const CrystalAnim& a) {
	return crystalChamsScale * (a.live ? 0.75f + 0.25f * a.alpha : 0.8f + 0.2f * a.alpha);
}

struct WrChamsVertex {
	float x, y, z;
	float nx, ny, nz;
	float u, v;
	float fade, edge;  // fundido del cristal y fuerza del borde de la capa
	uint8_t r, g, b, a;
};
static GLuint g_wrChamsProgram = 0, g_wrChamsVao = 0, g_wrChamsVbo = 0;
static GLint g_wrChamsLocMvp = -1, g_wrChamsLocEye = -1, g_wrChamsLocLine = -1, g_wrChamsLocWidth = -1, g_wrChamsLocTime = -1, g_wrChamsLocFancy = -1;
static std::unordered_map<uintptr_t, CrystalAnim> g_wrChamsAnim;
static double g_wrChamsLastTime = 0.0;

// Lo que el terreno opaco del frame deja para dibujar los cristales despues.
struct WrChamsPass {
	bool pending = false;
	unsigned frame = 0;
	double time = 0.0;
	glm::mat4 mvp = glm::mat4(1.0f);
	glm::vec3 cam = glm::vec3(0.0f), viewPos = glm::vec3(0.0f);
	GLint program = 0, fbo = 0, depthFunc = GL_LEQUAL;
	GLint viewport[4] = {0, 0, 0, 0};
};
static WrChamsPass g_wrChamsPass;
static int g_wrChamsMisses = 0;
static int g_wrChamsMissLogs = 0;

// Charms: van en el mismo pase que los cristales, con el mismo programa.
bool CharmsWorldActive();                              // charms_gatyo.h
static void CharmsDrawWorld(const WrChamsPass& pass);  // charms_gatyo.h

// Dibujo con el estado del pase opaco del terreno (escribe color y profundidad,
// sin mezcla). En un prepase de profundidad o con GL_EQUAL despues no sirve.
static bool WrOpaqueState() {
	GLboolean blend = glIsEnabled(GL_BLEND), depthTest = glIsEnabled(GL_DEPTH_TEST), depthMask = GL_FALSE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
	if (blend || !depthTest || !depthMask) return false;
	GLboolean colorMask[4] = {GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE};
	GLint depthFunc = 0;
	glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
	glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
	return colorMask[0] && depthFunc != GL_EQUAL;
}

// El frame termino el terreno sin un dibujo despues que sirviera. Muchos
// seguidos: el overlay toma el relevo.
static void WrChamsMiss(const char* why) {
	g_wrChamsPass.pending = false;
	// GATYO_PROBE: las primeras veces deja el motivo en logcat
	if (g_wrChamsMissLogs < 4) {
		g_wrChamsMissLogs++;
		__android_log_print(ANDROID_LOG_INFO, "Gatyo", "worldrender: chams sin lugar despues del terreno: %s", why);
	}
	if (++g_wrChamsMisses == 90) {
		g_wrChamsNoSlot = true;
		__android_log_print(ANDROID_LOG_WARN, "Gatyo", "worldrender: chams pasan al overlay (%s)", why);
	}
}

// Las 6 caras de una capa, con normal, coordenadas de cara (para el borde del
// shader) y color. Solo las de atras o solo las de adelante (`front`).
static void WrChamsEmitShell(std::vector<WrChamsVertex>& out, const CrystalShell& s, const glm::vec3& eye, bool front,
							 float fade, float edge, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	static const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	static const int order[6] = {0, 1, 2, 0, 2, 3};
	for (int f = 0; f < 6; f++) {
		const int* idx = kGatyoBoxFaces[f];
		glm::vec3 n = s.rot * kCrystalFaceNormals[f];
		glm::vec3 faceCenter = (s.corners[idx[0]] + s.corners[idx[1]] + s.corners[idx[2]] + s.corners[idx[3]]) * 0.25f;
		if ((glm::dot(n, eye - faceCenter) > 0.0f) != front) continue;
		for (int k : order) {
			const glm::vec3& p = s.corners[idx[k]];
			out.push_back({p.x, p.y, p.z, n.x, n.y, n.z, uv[k][0], uv[k][1], fade, edge, r, g, b, a});
		}
	}
}

// Dibuja los cristales del snapshot (y los que se estan desvaneciendo), de
// atras hacia adelante. En cada cristal: caras de atras de afuera hacia
// adentro y caras de adelante de adentro hacia afuera, para que la mezcla
// salga en orden sin buffer de profundidad propio.
static void WrDrawChams(const WrChamsPass& pass, const std::vector<EntityStruct>& crystals) {
	if (!g_wrChamsProgram) return;
	double now = GatyoNow();
	float dt = (float)ImClamp(now - g_wrChamsLastTime, 0.0, 0.1);
	g_wrChamsLastTime = now;
	if (!CrystalAnimate(g_wrChamsAnim, crystals, now, dt)) return;

	// el ojo en espacio de render es ViewPos (la camara va al origen)
	const glm::vec3& eye = pass.viewPos;
	struct Item { const CrystalAnim* anim; glm::vec3 center; float distance; };
	std::vector<Item> items;
	items.reserve(g_wrChamsAnim.size());
	for (const auto& kv : g_wrChamsAnim) {
		if (kv.second.alpha <= 0.0f) continue;
		// la resta entre coordenadas grandes va primero, para no perder precision
		glm::vec3 center = (CrystalCenter(kv.second) - pass.cam) + pass.viewPos;
		items.push_back({&kv.second, center, glm::length(center - eye)});
	}
	if (items.empty()) return;
	std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.distance > b.distance; });

	const float* fill = crystalChamsFillColor;
	auto byte = [](float v) { return (uint8_t)(ImSaturate(v) * 255.0f); };
	// vidrio mas transparente y nucleo mas denso y claro
	static const float kShellAlpha[kCrystalShells] = {0.6f, 0.8f, 1.6f};
	static const float kShellLight[kCrystalShells] = {0.0f, 0.1f, 0.3f};
	static const float kShellEdge[kCrystalShells] = {1.0f, 0.7f, 0.9f};
	std::vector<WrChamsVertex> verts;
	verts.reserve(items.size() * kCrystalShells * 36);
	for (const Item& it : items) {
		CrystalShell shells[kCrystalShells];
		CrystalShells(it.center, CrystalScale(*it.anim), it.anim->spin, shells);
		float fade = it.anim->alpha;
		for (int pass2 = 0; pass2 < 2; pass2++) {
			bool front = pass2 == 1;
			for (int k = 0; k < kCrystalShells; k++) {
				int i = front ? kCrystalShells - 1 - k : k;
				float lift = kShellLight[i];
				uint8_t r = byte(fill[0] + (1.0f - fill[0]) * lift), g = byte(fill[1] + (1.0f - fill[1]) * lift), b = byte(fill[2] + (1.0f - fill[2]) * lift);
				WrChamsEmitShell(verts, shells[i], eye, front, fade, kShellEdge[i], r, g, b, byte(fill[3] * kShellAlpha[i] + (i == 2 ? 0.1f : 0.0f)));
			}
		}
	}
	if (verts.empty()) return;

	glBindVertexArray(g_wrChamsVao);
	glBindBuffer(GL_ARRAY_BUFFER, g_wrChamsVbo);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(WrChamsVertex), verts.data(), GL_STREAM_DRAW);
	glUseProgram(g_wrChamsProgram);
	glUniformMatrix4fv(g_wrChamsLocMvp, 1, GL_FALSE, &pass.mvp[0][0]);
	glUniform3f(g_wrChamsLocEye, eye.x, eye.y, eye.z);
	glUniform4f(g_wrChamsLocLine, crystalChamsLineColor[0], crystalChamsLineColor[1], crystalChamsLineColor[2], crystalChamsLineColor[3]);
	glUniform1f(g_wrChamsLocWidth, 1.3f * ImMax(1.0f, (float)pass.viewport[3] / 900.0f));
	glUniform1f(g_wrChamsLocTime, (float)fmod(now, 1000.0));
	glUniform1f(g_wrChamsLocFancy, crystalChamsFancy ? 1.0f : 0.0f);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
}

// Primer dibujo del juego despues del terreno opaco: si sigue en el mismo
// framebuffer y con profundidad, se dibujan los cristales ahi mismo, con el
// estado del juego guardado y devuelto tal cual (bgfx lo tiene cacheado).
static void WrFlushChams() {
	WrChamsPass& pass = g_wrChamsPass;
	pass.pending = false;
	GLint fbo = 0, viewport[4] = {0, 0, 0, 0};
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
	glGetIntegerv(GL_VIEWPORT, viewport);
	if (fbo != pass.fbo) return WrChamsMiss("otro framebuffer");
	if (memcmp(viewport, pass.viewport, sizeof(viewport)) != 0) return WrChamsMiss("otro viewport");
	if (!glIsEnabled(GL_DEPTH_TEST)) return WrChamsMiss("sin profundidad");
	g_wrChamsMisses = 0;

	std::vector<EntityStruct> crystals;
	{
		std::lock_guard<std::mutex> lock(g_worldMutex);
		crystals = g_worldShared.crystals;
	}

	GLint prevProgram = 0, prevVao = 0, prevArray = 0, depthFunc = 0;
	GLint srcRgb = 0, dstRgb = 0, srcAlpha = 0, dstAlpha = 0, eqRgb = 0, eqAlpha = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArray);
	glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
	glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
	glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
	glGetIntegerv(GL_BLEND_EQUATION_RGB, &eqRgb);
	glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &eqAlpha);
	GLboolean depthMask = GL_TRUE, colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
	glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
	GLboolean blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE), stencil = glIsEnabled(GL_STENCIL_TEST);
	GLboolean offset = glIsEnabled(GL_POLYGON_OFFSET_FILL), coverage = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);

	glEnable(GL_BLEND);
	glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
	// el alfa del destino queda igual: el juego puede componer este buffer despues
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
	glDepthMask(GL_FALSE);
	glDepthFunc(pass.depthFunc);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	if (CrystalChamsWorldActive()) WrDrawChams(pass, crystals);
	if (CharmsWorldActive()) CharmsDrawWorld(pass);

	glUseProgram(prevProgram);
	glBindVertexArray(prevVao);
	glBindBuffer(GL_ARRAY_BUFFER, prevArray);
	glDepthFunc(depthFunc);
	glDepthMask(depthMask);
	glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
	glBlendEquationSeparate(eqRgb, eqAlpha);
	glBlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
	if (blend) glEnable(GL_BLEND);
	else glDisable(GL_BLEND);
	if (cull) glEnable(GL_CULL_FACE);
	if (stencil) glEnable(GL_STENCIL_TEST);
	if (offset) glEnable(GL_POLYGON_OFFSET_FILL);
	if (coverage) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
}


// Recursos GL en el contexto del juego. Si el juego recrea el contexto (al
// volver de segundo plano) los ids viejos no sirven y se vuelven a crear.
static bool WrEnsureResources() {
	EGLContext context = eglGetCurrentContext();
	if (context == g_wrContext && g_wrProgram) return true;
	if (context == g_wrFailedContext) return false;
	g_wrContext = context;
	g_wrProgram = g_wrVao = g_wrVbo = g_wrSkin = 0;
	g_wrChamsProgram = g_wrChamsVao = g_wrChamsVbo = 0;
	g_wrPrograms.clear();
	g_wrChamsAvailable = true;  // en el contexto nuevo se reintenta el CrystalChams
	g_wrChamsNoSlot = false;
	g_wrChamsMisses = 0;
	g_wrChamsPass.pending = false;

	static const char* vs =
		"#version 300 es\n"
		"uniform mat4 u_mvp;\n"
		"in vec3 a_pos;\n"
		"in vec2 a_uv;\n"
		"in vec4 a_color;\n"
		"out vec2 v_uv;\n"
		"out vec4 v_color;\n"
		"void main() {\n"
		"  v_uv = a_uv;\n"
		"  v_color = a_color;\n"
		"  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
		"}\n";
	static const char* fs =
		"#version 300 es\n"
		"precision mediump float;\n"
		"uniform sampler2D u_tex;\n"
		"uniform vec4 u_overlay;\n"
		"uniform vec3 u_light;\n"
		"in vec2 v_uv;\n"
		"in vec4 v_color;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"  vec4 tex = texture(u_tex, v_uv);\n"
		"  if (tex.a < 0.1) discard;\n"
		"  vec3 c = tex.rgb * v_color.rgb * u_light;\n"
		"  fragColor = vec4(mix(c, u_overlay.rgb, u_overlay.a), 1.0);\n"
		"}\n";
	GLuint v = WrCompile(GL_VERTEX_SHADER, vs), f = WrCompile(GL_FRAGMENT_SHADER, fs);
	if (!v || !f) {
		g_wrFailedContext = context;
		return false;
	}
	GLuint program = glCreateProgram();
	glAttachShader(program, v);
	glAttachShader(program, f);
	glBindAttribLocation(program, 0, "a_pos");
	glBindAttribLocation(program, 1, "a_uv");
	glBindAttribLocation(program, 2, "a_color");
	glLinkProgram(program);
	glDeleteShader(v);
	glDeleteShader(f);
	GLint linked = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		glDeleteProgram(program);
		g_wrFailedContext = context;
		return false;
	}
	g_wrProgram = program;
	g_wrLocMvp = glGetUniformLocation(program, "u_mvp");
	g_wrLocTex = glGetUniformLocation(program, "u_tex");
	g_wrLocOverlay = glGetUniformLocation(program, "u_overlay");
	g_wrLocLight = glGetUniformLocation(program, "u_light");

	glGenVertexArrays(1, &g_wrVao);
	glGenBuffers(1, &g_wrVbo);
	glBindVertexArray(g_wrVao);
	glBindBuffer(GL_ARRAY_BUFFER, g_wrVbo);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WrVertex), (void*)offsetof(WrVertex, x));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(WrVertex), (void*)offsetof(WrVertex, u));
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(WrVertex), (void*)offsetof(WrVertex, r));

	// la subida de la textura no debe leer de un PBO del juego ni de su row length
	GLint unpackBuffer = 0, rowLength = 0, skipRows = 0, skipPixels = 0, alignment = 4;
	glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
	glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
	glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows);
	glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	int w = 0, h = 0, channels = 0;
	unsigned char* pixels = stbi_load_from_memory(SteveSkinPng, sizeof(SteveSkinPng), &w, &h, &channels, STBI_rgb_alpha);
	if (pixels) {
		glGenTextures(1, &g_wrSkin);
		glBindTexture(GL_TEXTURE_2D, g_wrSkin);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		stbi_image_free(pixels);
	}
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, skipRows);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, skipPixels);
	glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);

	// Cristales del CrystalChams: programa aparte (sin textura). El relleno
	// lleva luz fija y brillo en los bordes de la silueta (fresnel); el borde
	// de cada cara sale en el fragment, suavizado, con un resplandor hacia
	// adentro. "Brillo extra" suma un reflejo que barre las caras. Si falla, el
	// mundo sigue sin el y el overlay de ImGui dibuja el fallback.
	if (g_wrChamsAvailable && !g_wrChamsProgram) {
		static const char* cvs =
			"#version 300 es\n"
			"uniform mat4 u_mvp;\n"
			"in vec3 a_pos;\n"
			"in vec3 a_normal;\n"
			"in vec2 a_uv;\n"
			"in vec2 a_extra;\n"
			"in vec4 a_color;\n"
			"out vec3 v_pos;\n"
			"out vec3 v_normal;\n"
			"out vec2 v_uv;\n"
			"out vec2 v_extra;\n"
			"out vec4 v_color;\n"
			"void main() {\n"
			"  v_pos = a_pos;\n"
			"  v_normal = a_normal;\n"
			"  v_uv = a_uv;\n"
			"  v_extra = a_extra;\n"
			"  v_color = a_color;\n"
			"  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
			"}\n";
		static const char* cfs =
			"#version 300 es\n"
			"precision highp float;\n"
			"uniform vec3 u_eye;\n"
			"uniform vec4 u_line;\n"
			"uniform float u_lineWidth;\n"
			"uniform float u_time;\n"
			"uniform float u_fancy;\n"
			"in vec3 v_pos;\n"
			"in vec3 v_normal;\n"
			"in vec2 v_uv;\n"
			"in vec2 v_extra;\n"
			"in vec4 v_color;\n"
			"out vec4 fragColor;\n"
			"void main() {\n"
			"  vec3 n = normalize(v_normal);\n"
			"  vec3 view = normalize(u_eye - v_pos);\n"
			"  float rim = pow(1.0 - abs(dot(n, view)), 2.0);\n"
			"  float light = 0.62 + 0.38 * max(dot(n, normalize(vec3(0.35, 1.0, 0.25))), 0.0);\n"
			"  vec2 e = min(v_uv, 1.0 - v_uv);\n"
			"  float d = min(e.x, e.y);\n"
			"  float px = max(fwidth(d), 1e-5);\n"
			"  float edge = 1.0 - smoothstep(u_lineWidth * px, (u_lineWidth + 1.5) * px, d);\n"
			"  float glow = exp(-d / clamp(px * 7.0, 0.02, 0.12)) * (0.3 + 0.35 * u_fancy);\n"
			"  float sheen = u_fancy * pow(max(sin((v_uv.x - v_uv.y) * 2.6 + u_time * 1.7), 0.0), 14.0) * 0.5;\n"
			"  vec3 col = v_color.rgb * light * (1.0 + rim * (0.45 + 0.55 * u_fancy)) + vec3(sheen);\n"
			"  float a = v_color.a * (0.7 + 0.6 * rim) + sheen * 0.4;\n"
			"  float k = clamp(edge + glow, 0.0, 1.0) * v_extra.y;\n"
			"  col = mix(col, u_line.rgb, k);\n"
			"  a = mix(a, u_line.a, k);\n"
			"  fragColor = vec4(col, clamp(a, 0.0, 1.0) * v_extra.x);\n"
			"}\n";
		GLuint cv = WrCompile(GL_VERTEX_SHADER, cvs), cf = WrCompile(GL_FRAGMENT_SHADER, cfs);
		GLuint cprog = 0;
		if (cv && cf) {
			cprog = glCreateProgram();
			glAttachShader(cprog, cv);
			glAttachShader(cprog, cf);
			glBindAttribLocation(cprog, 0, "a_pos");
			glBindAttribLocation(cprog, 1, "a_normal");
			glBindAttribLocation(cprog, 2, "a_uv");
			glBindAttribLocation(cprog, 3, "a_extra");
			glBindAttribLocation(cprog, 4, "a_color");
			glLinkProgram(cprog);
			GLint clinked = 0;
			glGetProgramiv(cprog, GL_LINK_STATUS, &clinked);
			if (!clinked) {
				char log[512] = {};
				glGetProgramInfoLog(cprog, sizeof(log), nullptr, log);
				__android_log_print(ANDROID_LOG_ERROR, "Gatyo", "worldrender: chams link: %s", log);
				glDeleteProgram(cprog);
				cprog = 0;
			}
		}
		if (cv) glDeleteShader(cv);
		if (cf) glDeleteShader(cf);
		if (cprog) {
			g_wrChamsProgram = cprog;
			g_wrChamsLocMvp = glGetUniformLocation(cprog, "u_mvp");
			g_wrChamsLocEye = glGetUniformLocation(cprog, "u_eye");
			g_wrChamsLocLine = glGetUniformLocation(cprog, "u_line");
			g_wrChamsLocWidth = glGetUniformLocation(cprog, "u_lineWidth");
			g_wrChamsLocTime = glGetUniformLocation(cprog, "u_time");
			g_wrChamsLocFancy = glGetUniformLocation(cprog, "u_fancy");
			glGenVertexArrays(1, &g_wrChamsVao);
			glGenBuffers(1, &g_wrChamsVbo);
			glBindVertexArray(g_wrChamsVao);
			glBindBuffer(GL_ARRAY_BUFFER, g_wrChamsVbo);
			for (GLuint i = 0; i < 5; i++) glEnableVertexAttribArray(i);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WrChamsVertex), (void*)offsetof(WrChamsVertex, x));
			glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(WrChamsVertex), (void*)offsetof(WrChamsVertex, nx));
			glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(WrChamsVertex), (void*)offsetof(WrChamsVertex, u));
			glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(WrChamsVertex), (void*)offsetof(WrChamsVertex, fade));
			glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(WrChamsVertex), (void*)offsetof(WrChamsVertex, r));
		} else {
			g_wrChamsAvailable = false;  // el overlay toma el relevo
		}
	}

	return g_wrSkin != 0;
}

// ---------------------------------------------------------------------------
// Modelo de jugador (formato de skin de 64x64). Las medidas van en pixeles de
// skin, con los pies en y = 0 y mirando a +Z; la derecha del personaje es -X.
// ---------------------------------------------------------------------------
struct WrModel {
	glm::vec3 base, right, up, forward;  // pies en espacio de render y ejes del personaje
	float scale;                         // bloques por pixel de skin
	bool lying;                          // muerto: tirado de costado
	std::vector<WrVertex>* out;
};

static void WrEmitQuad(const WrModel& m, const glm::vec3 p[4], const float uv[4][2], float shade) {
	uint8_t c = (uint8_t)(255.0f * shade);
	glm::vec3 world[4];
	for (int i = 0; i < 4; i++) {
		glm::vec3 q = p[i];
		if (m.lying) q = glm::vec3(q.y, 4.0f - q.x, q.z);  // de costado: el torso apoyado y la cabeza hacia su izquierda
		world[i] = m.base + m.right * (q.x * m.scale) + m.up * (q.y * m.scale) + m.forward * (q.z * m.scale);
	}
	static const int order[6] = {0, 1, 2, 0, 2, 3};
	for (int k : order) m.out->push_back({world[k].x, world[k].y, world[k].z, uv[k][0] / 64.0f, uv[k][1] / 64.0f, c, c, c, 255});
}

// Caja del modelo: `lo`/`hi` en pixeles (ya inflada si es una capa exterior),
// (u, v) y (w, h, d) del recuadro en la skin. Gira `angle` sobre X alrededor de
// `pivot` (balanceo de brazos y piernas).
static void WrCuboid(const WrModel& m, glm::vec3 lo, glm::vec3 hi, float u, float v, float w, float h, float d, glm::vec3 pivot, float angle) {
	float ca = cosf(angle), sa = sinf(angle);
	auto rot = [&](glm::vec3 p) {
		float y = p.y - pivot.y, z = p.z - pivot.z;
		return glm::vec3(p.x, pivot.y + y * ca - z * sa, pivot.z + y * sa + z * ca);
	};
	float x0 = lo.x, y0 = lo.y, z0 = lo.z, x1 = hi.x, y1 = hi.y, z1 = hi.z;
	struct Face { glm::vec3 p[4]; float uv[4][2]; float shade; };
	const Face faces[6] = {
		// frente (z1): la izquierda de la imagen es la derecha del personaje (-X)
		{{{x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}}, {{u + d, v + d}, {u + d + w, v + d}, {u + d + w, v + d + h}, {u + d, v + d + h}}, 0.82f},
		// espalda (z0)
		{{{x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}}, {{u + 2 * d + w, v + d}, {u + 2 * d + 2 * w, v + d}, {u + 2 * d + 2 * w, v + d + h}, {u + 2 * d + w, v + d + h}}, 0.82f},
		// costado derecho (x0)
		{{{x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}}, {{u, v + d}, {u + d, v + d}, {u + d, v + d + h}, {u, v + d + h}}, 0.62f},
		// costado izquierdo (x1)
		{{{x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}}, {{u + d + w, v + d}, {u + 2 * d + w, v + d}, {u + 2 * d + w, v + d + h}, {u + d + w, v + d + h}}, 0.62f},
		// arriba (y1)
		{{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}, {{u + d, v}, {u + d + w, v}, {u + d + w, v + d}, {u + d, v + d}}, 1.0f},
		// abajo (y0)
		{{{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}, {{u + d + w, v}, {u + d + 2 * w, v}, {u + d + 2 * w, v + d}, {u + d + w, v + d}}, 0.5f},
	};
	for (const Face& f : faces) {
		glm::vec3 p[4] = {rot(f.p[0]), rot(f.p[1]), rot(f.p[2]), rot(f.p[3])};
		WrEmitQuad(m, p, f.uv, f.shade);
	}
}

// Una parte con su capa exterior (sombrero, chaqueta, mangas, pantalon), que
// es la misma caja inflada medio pixel con otro recuadro de la skin.
static void WrPart(const WrModel& m, glm::vec3 lo, glm::vec3 size, float u, float v, float ou, float ov, glm::vec3 pivot, float angle, float inflate) {
	glm::vec3 hi = lo + size;
	WrCuboid(m, lo, hi, u, v, size.x, size.y, size.z, pivot, angle);
	glm::vec3 grow(inflate);
	WrCuboid(m, lo - grow, hi + grow, ou, ov, size.x, size.y, size.z, pivot, angle);
}

static void WrBuildPlayer(std::vector<WrVertex>& out, glm::vec3 base, float yawDeg, float limbPhase, float limbAmount, bool lying) {
	float yaw = yawDeg * (float)M_PI / 180.0f;
	WrModel m;
	m.base = base;
	m.forward = glm::vec3(-sinf(yaw), 0.0f, cosf(yaw));
	m.right = glm::vec3(cosf(yaw), 0.0f, sinf(yaw));
	m.up = glm::vec3(0.0f, 1.0f, 0.0f);
	m.scale = 0.9375f / 16.0f;
	m.lying = lying;
	m.out = &out;
	float swing = sinf(limbPhase) * limbAmount;
	glm::vec3 hip(0.0f, 12.0f, 0.0f), shoulder(0.0f, 22.0f, 0.0f), none(0.0f);
	// cabeza y sombrero
	WrPart(m, {-4, 24, -4}, {8, 8, 8}, 0, 0, 32, 0, none, 0.0f, 0.5f);
	// torso y chaqueta
	WrPart(m, {-4, 12, -2}, {8, 12, 4}, 16, 16, 16, 32, none, 0.0f, 0.25f);
	// brazos (el derecho en -X) y mangas, balanceo opuesto a las piernas
	WrPart(m, {-8, 12, -2}, {4, 12, 4}, 40, 16, 40, 32, shoulder, swing, 0.25f);
	WrPart(m, {4, 12, -2}, {4, 12, 4}, 32, 48, 48, 48, shoulder, -swing, 0.25f);
	// piernas y pantalon
	WrPart(m, {-4, 0, -2}, {4, 12, 4}, 0, 16, 0, 32, hip, -swing, 0.25f);
	WrPart(m, {0, 0, -2}, {4, 12, 4}, 16, 48, 0, 48, hip, swing, 0.25f);
}

// ---------------------------------------------------------------------------
// Jugador falso: la posicion llega a 20 ticks por segundo; aca se interpola
// para que camine suave a la tasa de frames.
// ---------------------------------------------------------------------------
struct WrSmooth {
	glm::vec3 prev, cur;
	float prevYaw = 0.0f, curYaw = 0.0f;
	double changed = 0.0;
	bool valid = false;
	float walked = 0.0f;  // distancia caminada, para la fase de las piernas
	glm::vec3 lastShown;
};
static WrSmooth g_wrFake;

static float WrLerpAngle(float a, float b, float t) {
	float d = fmodf(b - a + 540.0f, 360.0f) - 180.0f;
	return a + d * t;
}

static void WrDrawScene(const glm::mat4& mvp, const glm::vec3& cam, const glm::vec3& viewPos) {
	PlayerStruct fake;
	bool found = false;
	{
		std::lock_guard<std::mutex> lock(g_worldMutex);
		for (const PlayerStruct& p : g_worldShared.players) {
			if (p.nickname == kFakePlayerName) {
				fake = p;
				found = true;
				break;
			}
		}
	}
	if (!found) {
		g_wrFake.valid = false;
		return;
	}
	double now = GatyoNow();
	glm::vec3 feet(fake.position.x, fake.position.y - 1.62f, fake.position.z);
	float yaw = g_fakeYaw.load();
	if (!g_wrFake.valid || glm::length(feet - g_wrFake.cur) > 8.0f) {
		g_wrFake.prev = g_wrFake.cur = g_wrFake.lastShown = feet;
		g_wrFake.prevYaw = g_wrFake.curYaw = yaw;
		g_wrFake.changed = now;
		g_wrFake.valid = true;
	} else if (feet != g_wrFake.cur || yaw != g_wrFake.curYaw) {
		g_wrFake.prev = g_wrFake.cur;
		g_wrFake.prevYaw = g_wrFake.curYaw;
		g_wrFake.cur = feet;
		g_wrFake.curYaw = yaw;
		g_wrFake.changed = now;
	}
	float t = ImSaturate((float)((now - g_wrFake.changed) / 0.05));
	glm::vec3 shown = g_wrFake.prev + (g_wrFake.cur - g_wrFake.prev) * t;
	float shownYaw = WrLerpAngle(g_wrFake.prevYaw, g_wrFake.curYaw, t);
	float step = glm::length(glm::vec2(shown.x - g_wrFake.lastShown.x, shown.z - g_wrFake.lastShown.z));
	g_wrFake.walked += step;
	g_wrFake.lastShown = shown;
	bool moving = glm::length(g_wrFake.cur - g_wrFake.prev) > 0.01f;

	std::vector<WrVertex> verts;
	verts.reserve(864);
	// pies en espacio de render: primero la resta entre coordenadas grandes, para no perder precision
	WrBuildPlayer(verts, (shown - cam) + viewPos, shownYaw, g_wrFake.walked * 2.2f, moving ? 0.7f : 0.0f, fake.health <= 0);

	glBindVertexArray(g_wrVao);
	glBindBuffer(GL_ARRAY_BUFFER, g_wrVbo);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(WrVertex), verts.data(), GL_STREAM_DRAW);
	glUseProgram(g_wrProgram);
	glUniformMatrix4fv(g_wrLocMvp, 1, GL_FALSE, &mvp[0][0]);
	glUniform1i(g_wrLocTex, 0);
	bool hurt = fake.hurtTime > 0 || fake.health <= 0;
	glUniform4f(g_wrLocOverlay, 1.0f, 0.0f, 0.0f, hurt ? 0.45f : 0.0f);
	glUniform3f(g_wrLocLight, g_wrLight[0], g_wrLight[1], g_wrLight[2]);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_wrSkin);
	glBindSampler(0, 0);
	glDisable(GL_CULL_FACE);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
}

// Camara exacta del frame a partir de la traslacion del chunk (ver arriba). Si
// el redondeo no cierra (el chunk no esta alineado a 16 o la estimacion esta
// lejos) queda la de LevelRendererPlayer.
static glm::vec3 WrExactCamera(const float* model, const float* viewPos, const vec3_t& approx) {
	glm::vec3 t(model[12] - viewPos[0], model[13] - viewPos[1], model[14] - viewPos[2]);
	glm::vec3 corner = t + glm::vec3(approx.x, approx.y, approx.z);
	glm::vec3 snapped(roundf(corner.x / 16.0f) * 16.0f, roundf(corner.y / 16.0f) * 16.0f, roundf(corner.z / 16.0f) * 16.0f);
	glm::vec3 residual = glm::abs(corner - snapped);
	bool aligned = residual.x < 1.5f && residual.y < 1.5f && residual.z < 1.5f;
	// GATYO_PROBE: las primeras veces deja la comparacion en logcat
	if (g_wrProbeCount < 4) {
		g_wrProbeCount++;
		__android_log_print(ANDROID_LOG_INFO, "Gatyo", "worldrender: chunk %.3f %.3f %.3f cam %.3f %.3f %.3f residual %.3f %.3f %.3f %s",
			t.x, t.y, t.z, approx.x, approx.y, approx.z, residual.x, residual.y, residual.z, aligned ? "ok" : "FALLBACK");
	}
	if (!aligned) return glm::vec3(approx.x, approx.y, approx.z);
	return snapped - t;
}

// Desde el hook de glDrawElementsInstanced, antes de cada dibujo del juego.
void WorldRenderOnDraw(unsigned int mode) {
	// el jugador falso (modo virtual: el NPC lo dibuja el juego), los
	// cristales del CrystalChams y los efectos de los Charms; si no hay nada de
	// eso, no se toca el estado
	bool fakeVisible = FakePlayerVirtualVisible();
	bool wantCrystals = CrystalChamsWorldActive();
	bool wantCharms = CharmsWorldActive();
	if (!wantCrystals && !g_wrChamsAnim.empty()) g_wrChamsAnim.clear();
	if (!wantCrystals && !wantCharms) g_wrChamsPass.pending = false;
	if (!(fakeVisible || wantCrystals || wantCharms) || mode != GL_TRIANGLES) return;
	g_gameRenderTid = gettid();
	GLint program = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	if (!program) return;
	const WrProgramInfo& info = WrProgram(program);
	unsigned frame = g_gameFrame.load();

	// cristales pendientes del terreno de este frame: van en el primer dibujo
	// que ya no es terreno opaco
	if (g_wrChamsPass.pending) {
		bool sameFrame = frame > 0 ? frame == g_wrChamsPass.frame : GatyoNow() - g_wrChamsPass.time < 0.008;
		// los chunks del mismo programa siguen siendo el pase opaco (sin repetir
		// todas las consultas de estado en cada uno)
		bool opaqueTerrain = info.terrain && (program == g_wrChamsPass.program ? !glIsEnabled(GL_BLEND) : WrOpaqueState());
		if (!sameFrame) WrChamsMiss("termino el frame");
		else if (!opaqueTerrain) WrFlushChams();
	}

	// luz de las entidades del frame anterior, para que el jugador falso no brille de noche
	if (info.tileLight >= 0 && frame != g_wrLightFrame) {
		g_wrLightFrame = frame;
		float light[4];
		glGetUniformfv(program, info.tileLight, light);
		for (int i = 0; i < 3; i++) g_wrLight[i] = ImClamp(light[i], 0.05f, 1.0f);
	}

	if (!info.terrain) return;
	// un dibujo por frame; si el hook de eglSwapBuffers no llego a contar, se separan por tiempo
	double now = GatyoNow();
	bool newFrame = g_gameFrame.load() > 0 ? frame != g_wrDrawnFrame : now - g_wrDrawnTime > 0.010;
	if (!newFrame) return;
	// solo el pase opaco escribe la profundidad que necesitamos, y tiene que
	// escribir color: en un prepase de profundidad (o con GL_EQUAL despues)
	// haria un hueco
	if (!WrOpaqueState()) return;
	if (inMenu || panic) return;
	g_wrDrawnFrame = frame;
	g_wrDrawnTime = now;

	float proj[16], view[16], viewPos[4], model[16];
	glGetUniformfv(program, info.proj, proj);
	glGetUniformfv(program, info.view, view);
	glGetUniformfv(program, info.viewPos, viewPos);
	glGetUniformfv(program, info.model, model);
	ClientInstance* ci = CI;
	if (!ci) return;
	LevelRenderer* lr = ci->getLevelRenderer();
	LevelRendererCamera* lrp = lr ? lr->getLevelRendererPlayer() : nullptr;
	if (!lrp) return;
	glm::vec3 cam = WrExactCamera(model, viewPos, lrp->getCameraPos());

	// guardar lo que se va a tocar
	GLint prevProgram = program, prevVao = 0, prevArray = 0, prevActive = 0, prevTex = 0, prevSampler = 0;
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArray);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
	glGetIntegerv(GL_SAMPLER_BINDING, &prevSampler);
	GLboolean cull = glIsEnabled(GL_CULL_FACE), stencil = glIsEnabled(GL_STENCIL_TEST);
	glDisable(GL_STENCIL_TEST);

	if (WrEnsureResources()) {
		glm::mat4 mvp = glm::make_mat4(proj) * glm::make_mat4(view);
		glm::vec3 vp(viewPos[0], viewPos[1], viewPos[2]);
		if (fakeVisible) WrDrawScene(mvp, cam, vp);
		if ((wantCrystals || wantCharms) && g_wrChamsProgram) {
			WrChamsPass& pass = g_wrChamsPass;
			pass.pending = true;
			pass.program = program;
			pass.frame = frame;
			pass.time = now;
			pass.mvp = mvp;
			pass.cam = cam;
			pass.viewPos = vp;
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &pass.fbo);
			glGetIntegerv(GL_VIEWPORT, pass.viewport);
			glGetIntegerv(GL_DEPTH_FUNC, &pass.depthFunc);
		}
		g_worldRenderLast = GatyoNow();
	}

	glUseProgram(prevProgram);
	glBindVertexArray(prevVao);
	glBindBuffer(GL_ARRAY_BUFFER, prevArray);
	glBindTexture(GL_TEXTURE_2D, prevTex);
	glBindSampler(0, prevSampler);
	glActiveTexture(prevActive);
	if (cull) glEnable(GL_CULL_FACE);
	else glDisable(GL_CULL_FACE);
	if (stencil) glEnable(GL_STENCIL_TEST);
}
