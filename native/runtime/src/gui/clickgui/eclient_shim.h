#pragma once
// E-Client shim for Zeff's clickgui — provides missing Apollon globals as E-Client equivalents.
// This header must be included BEFORE variables.h / menu.h / menu_utils.h

#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <map>
#include <variant>
#include <random>

// ---------- String obfuscation (original ENCRYPT headers) -> identity for E-Client ----------
#define OBFUSCATE(x) (x)
inline std::string oxorany(const char* s) { return std::string(s); }
inline std::string oxorany(const std::string& s) { return s; }

// ---------- ImGui internal (g_window) ----------
#include "imgui.h"
#include "imgui_internal.h" // for ImGuiWindow* g_window

// ---------- Screen globals (originally defined in main.cpp) ----------
inline int screenWidth = 0;
inline int screenHeight = 0;
inline float scaleGlobal = 1.0f;
inline float calcResX = 1.0f;
inline ImGuiWindow* g_window = nullptr;
inline std::string GameName = "com.mojang.minecraftpe";
inline const char* MineLib = "libminecraftpe.so";

// ---------- Minimal nlohmann::json stub (originally Includes/nlohmann/json.hpp) ----------
// We only need: json cfg;  cfg["key"]=val;  cfg.value("key",def);  cfg.dump();  json::accept(str);  json::parse(str);
// For E-Client we don't actually persist config via this mechanism — saveConfig/loadConfig will be no-ops
// that log instead. So we provide a tiny stub that satisfies the compiler.
namespace nlohmann {
struct json {
    std::map<std::string, std::string> _data;
    // assignment e.g. cfg["key"] = value;  use proxy
    struct Proxy {
        json* j; std::string k;
        template<typename T> Proxy& operator=(const T& v) { j->_data[k] = std::to_string(v); return *this; }
        Proxy& operator=(const std::string& v) { j->_data[k] = v; return *this; }
        Proxy& operator=(const char* v) { j->_data[k] = v; return *this; }
        Proxy& operator=(bool v) { j->_data[k] = v?"1":"0"; return *this; }
    };
    Proxy operator[](const std::string& k) { return {this,k}; }
    Proxy operator[](const char* k) { return {this,std::string(k)}; }
    template<typename T> T value(const std::string& k, const T& def) const {
        auto it=_data.find(k); if(it==_data.end()) return def;
        // very crude: try to parse stored string as T
        if constexpr (std::is_same_v<T, std::string>) return it->second;
        if constexpr (std::is_same_v<T, bool>) return it->second=="1" || it->second=="true";
        if constexpr (std::is_same_v<T, int>) { try{ return std::stoi(it->second);}catch(...){ return def; } }
        if constexpr (std::is_same_v<T, float>) { try{ return std::stof(it->second);}catch(...){ return def; } }
        if constexpr (std::is_same_v<T, double>) { try{ return std::stod(it->second);}catch(...){ return def; } }
        return def;
    }
    // vector<string> specialization
    std::vector<std::string> value(const std::string& k, const std::vector<std::string>& def) const { return def; }
    std::string dump(int = -1) const { return "{}"; }
    static bool accept(const std::string&) { return false; }
    static json parse(const std::string&) { return {}; }
};
}
using json = nlohmann::json;

// ---------- GLM stubs (original used glm for ESP) ----------
namespace glm {
    struct vec2 { float x,y; vec2(float x=0,float y=0):x(x),y(y){} };
    struct vec3 { float x,y,z; vec3(float x=0,float y=0,float z=0):x(x),y(y),z(z){} };
    struct vec4 { float x,y,z,w; };
    struct mat4 {};
    inline vec2 operator+(const vec2& a, const vec2& b){return {a.x+b.x,a.y+b.y};}
}

// ---------- stb_image stub (menu_utils.h needs STBI_rgb_alpha, stbi_load/free) ----------
#define STBI_rgb_alpha 4
inline unsigned char* stbi_load(const char*, int*, int*, int*, int){ return nullptr; }
inline void stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int){}
inline void stbi_image_free(void*) {}

// ---------- Minecraft SDK forward decls (originally hook_classes.h) ----------
// Minimal implementations to satisfy menu.h ESP code (CIA->getViewMatrix etc.)
struct LevelRendererCamera {
    vec3_t getCameraPos(){ return vec3_t(0,0,0); }
    glm::vec2 getFov(){ return glm::vec2(70,70); }
};
struct LevelRenderer {
    LevelRendererCamera* getLevelRendererPlayer(){ static LevelRendererCamera c; return &c; }
};
struct ClientInstance {
    glm::mat4 getViewMatrix(){ return glm::mat4{}; }
    LevelRenderer* getLevelRenderer(){ static LevelRenderer r; return &r; }
    std::string getScreenName(){ return \"\"; }
};
struct LocalPlayer {};
struct Player {};
struct Mob {};
struct Actor {};

// Chams.h globals that variables.h expects (originally defined in Chams.h before variables.h)
inline bool panic = false;
inline bool osmtBoost = false;
inline bool enableRainbow = false;
inline bool enableRainbowWall = false;
inline bool xrayCaves = false;
inline int chamsint = 0;
inline ImVec4 inWallColor = ImVec4(1,0,0,1);
inline ImVec4 visibleColor = ImVec4(0,1,0,1);

// ---------- Vector types expected by menu_utils.h (originally Vector.h) ----------
struct vec2_t { float x,y; vec2_t(float _x=0,float _y=0):x(_x),y(_y){} };
struct vec3_t { float x,y,z; vec3_t(float _x=0,float _y=0,float _z=0):x(_x),y(_y),z(_z){} vec3_t(const vec2_t& v,float zz=0):x(v.x),y(v.y),z(zz){} };
struct vec3_ti { int x,y,z; vec3_ti(int _x=0,int _y=0,int _z=0):x(_x),y(_y),z(_z){} };

// ---------- Minimal stubs for functions that menu.h/menu_utils.h expect but E-Client doesn't yet implement ----------
// These are defined as inline no-ops so the clickgui can compile and render, even if ESP/XRay aren't fully hooked.
inline float distanceTo(const vec3_t* a, const vec3_t* b){ if(!a||!b) return 0; float dx=a->x-b->x,dy=a->y-b->y,dz=a->z-b->z; return std::sqrt(dx*dx+dy*dy+dz*dz); }
inline bool OWorldToScreen(const glm::vec3& camPos, const glm::vec3& worldPos, glm::vec2& out, const glm::vec2& fov, const glm::vec2& displaySize, const glm::mat4& viewMatrix){ return false; }
inline void insertionSortPlayers(std::vector<struct PlayerStruct>& v){}

// ---------- Patch bridging: Zeff's Patches() will be implemented in InGameGui.cpp to call PatchManager ----------
inline void EClient_Patches(); // forward decl — implemented in InGameGui.cpp via PatchManager
#define Patches EClient_Patches

// Note: RenderESP/RenderOreESP/RenderDeathPosition/DrawBackgroundText are defined in menu_utils.h — do NOT stub here
