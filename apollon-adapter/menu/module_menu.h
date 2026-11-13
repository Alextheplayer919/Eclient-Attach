#pragma once
// Included after Apollon's variables and Fly2 adapter. All toggles reference the
// existing module variables; category selection never toggles a module.
namespace eclient_module_menu {
struct Module { const char* name; bool* toggle; int settings; };
enum { SIMPLE, FLY2, AURA, MOBAURA, HITBOX, AUTOCLICK, XRAY, ESP, FOV, POSITION, PLAYERS, SPEED, WATER, LAVA, JUMP, GRAVITY, CHAMS, FOG, FONT, LAN, CONFIG, PRACTICE_TARGET, CRYSTAL_AURA };
static Module movement[] = {
    {"Fly2",nullptr,FLY2},{"Speed",nullptr,SPEED},{"Water Speed",nullptr,WATER},{"Lava Speed",nullptr,LAVA},{"Jump",nullptr,JUMP},{"Gravity",nullptr,GRAVITY},{"Creative Fly",&creativeFly,SIMPLE},{"Force Creative Fly",&forceCreativeFly,SIMPLE},
    {"Ladder Fly",&ladderFly,SIMPLE},{"Damage Fly",&damageFly,SIMPLE},{"Always Sprint",&alwaysSprinting,SIMPLE},
    {"No SlowDown",&noSlowDown,SIMPLE},{"No Web",&offSlowdownTrigger,SIMPLE},{"No Jump Delay",&noJumpDelay,SIMPLE},
    {"Fast Fall",&fastFall,SIMPLE},{"Step",&step,SIMPLE},{"Noclip",&noclip,SIMPLE},{"No Water",&offWaterTrigger,SIMPLE},
    {"Strafe",&strafe,SIMPLE},{"No Drowning",&offDrown,SIMPLE}
};
static Module combat[] = {{"Crystal Aura",nullptr,CRYSTAL_AURA},{"Practice target",nullptr,PRACTICE_TARGET},{"KillAura",&killAura,AURA},{"MobAura",&mobAura,MOBAURA},{"HitBox",&hitBox,HITBOX},
    {"Kill Near Items",&killItems,SIMPLE},{"Anti Fireball",&antiFireball,SIMPLE},
    {"Auto Clicker",&autoClicker,AUTOCLICK},{"Anti Knockback",&antiKnockback,SIMPLE},{"Anti Bot",&antiBot,SIMPLE}};
static Module building[] = {{"Auto Mine",&clickAutoMine,SIMPLE},{"Auto Build",&fastBridge,SIMPLE},{"Block Reach",&blockReach,SIMPLE}};
static Module visuals[] = {{"Fog",nullptr,FOG},{"Font size",nullptr,FONT},{"Chams",nullptr,CHAMS},{"Xray",&xrayDefault,XRAY},
    {"ESP",&esp,ESP},{"FOV",&enableFov,FOV},{"FullBright",&fullBright,SIMPLE},{"FPS Boost",&osmtBoost,SIMPLE},
    {"Freelook",&look360,SIMPLE},{"No Hurt Cam",&noHurtCam,SIMPLE},{"No Cam Distortion",&noCamDist,SIMPLE},
    {"No Boat Rotation Limit",&noBoatRotation,SIMPLE},{"No Sleep Vignette",&noCamSleep,SIMPLE},
    {"Place Camera",&placeCam,SIMPLE},{"Anti Invisible",&antiInvis,SIMPLE},{"Derp",&derp,SIMPLE},
    {"No Blur",&noBlur,SIMPLE},{"Anti Blindness",&antiBlind,SIMPLE},{"Paperdoll",&showPaperdoll,SIMPLE},
    {"No Cave Vignette",&noCaveVignette,SIMPLE},{"Death Position",&deathPosition,SIMPLE},
    {"Show Position",&showPosition,POSITION},{"Near Players",&playerNear,PLAYERS}};
static Module other[] = {{"LAN",nullptr,LAN},{"Config",nullptr,CONFIG},{"Marketplace",&unlockMarket,SIMPLE},{"Fast Drop",&fastDrop,SIMPLE},{"No Emote Cooldown",&noEmoteCooldown,SIMPLE}};
static int selected[8] = {0,0,0,0,0,0,0,0};
static Module* list(int category, int& count) {
#define LIST(n, a) case n: count = IM_ARRAYSIZE(a); return a
    switch (category) { LIST(0,movement); LIST(1,combat); LIST(2,building); LIST(4,visuals); LIST(6,other); default: count=0; return nullptr; }
#undef LIST
}
static void row(int category) {
    int count = 0; Module* modules = list(category, count);
    if (!modules) return;
    ImGui::BeginChild("##module-row", ImVec2(0, ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ScrollbarSize + 4), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (int i = 0; i < count; ++i) {
        if (i) ImGui::SameLine(); ImGui::PushID(i);
        if (ImGui::Selectable(modules[i].name, selected[category] == i, 0, ImVec2(ImGui::CalcTextSize(modules[i].name).x + ImGui::GetStyle().FramePadding.x * 2, ImGui::GetFrameHeight()))) selected[category] = i;
        ImGui::PopID();
    }
    ImGui::EndChild();
}
static void auraSettings(bool mob) {
    if (mob) {
        ImGui::SliderFloat("Mob range", &mobAuraRange, 1, 10, "%.1f");
        if (!useGeyserCooldown) ImGui::SliderInt("Mob delay", &maxMobAuraTick, 1, 150);
    } else {
        if (ImGui::SliderFloat("Kill range", &killRange, 1, 10, "%.1f") && killRange > rotationsRange) rotationsRange = killRange;
        ImGui::SliderFloat("Rotation range", &rotationsRange, 1, 100, "%.1f");
        if (rotationsRange < killRange) rotationsRange = killRange;
        if (!useGeyserCooldown) ImGui::SliderInt("Kill delay", &maxKillTick, 1, 150);
    }
    ImGui::Checkbox("Use Geyser cooldown", &useGeyserCooldown);
    if (!useGeyserCooldown && (mob || !emulateNativeAttack)) ImGui::Combo("Aura type", &auraType, AuraType, IM_ARRAYSIZE(AuraType));
    else { ImGui::Combo("Aura type", &auraType, AuraTypeGeyser, IM_ARRAYSIZE(AuraTypeGeyser)); if (auraType == 2) auraType = 1; }
    ImGui::Checkbox("Swing", &swing);
    if (mob || !emulateNativeAttack) {
        ImGui::Checkbox("Rotations", &rotations); ImGui::Checkbox("Target Lock", &targetLock);
        ImGui::TextDisabled("Shared with KillAura and MobAura");
    } else ImGui::TextWrapped("Native attack forces rotations and target lock on.");
    ImGui::Checkbox("Hurt Check", &hurtCheck);
    if (!mob) ImGui::Checkbox("Simulate Native Attack", &emulateNativeAttack);
    if (mob && ImGui::CollapsingHeader("Excluded mobs")) {
        ImGui::TextWrapped("Checked mobs are not attacked.");
        struct Filter { const char* name; bool* value; };
        Filter filters[] = {
            {"Horses",&noHorses},{"Villagers",&noVillagers},{"Snow golems",&noSnowMan},
            {"Iron golems",&noIronGolem},{"Donkeys",&noDonkeys},{"Allays",&noAllays},
            {"Armadillos",&noArmadillos},{"Axolotls",&noAxolotls},{"Bats",&noBats},
            {"Bees",&noBees},{"Camels",&noCamels},{"Cats",&noCats},
            {"Chickens",&noChickens},{"Cod",&noCods},{"Copper golems",&noCopperGolems},
            {"Cows",&noCows},{"Dolphins",&noDolphins},{"Foxes",&noFoxes},
            {"Frogs",&noFrogs},{"Glow squids",&noGlowSquids},{"Goats",&noGoats},
            {"Happy ghasts",&noHappyGhasts},{"Llamas",&noLlamas},{"Ocelots",&noOcelots},
            {"Pandas",&noPandas},{"Parrots",&noParrots},{"Pigs",&noPigs},
            {"Piglins",&noPiglins},{"Pufferfish",&noPufferfishes},{"Rabbits",&noRabbits},
            {"Salmon",&noSalmons},{"Sheep",&noSheeps},{"Sniffers",&noSniffers},
            {"Squid",&noSquids},{"Striders",&noStriders},{"Tadpoles",&noTadpoles},
            {"Tropical fish",&noTropicalfishes},{"Turtles",&noTurtles},{"Wandering traders",&noWanderingtraders},
            {"Wolves",&noWolfs},{"Zombie piglins",&noZombiepiglins},{"Zombie villagers",&noZombievillagers}
        };
        for (auto& filter : filters) ImGui::Checkbox(filter.name, filter.value);
    }
}
static bool page(int category) {
    int count = 0; Module* modules = list(category, count);
    if (!modules || selected[category] < 0 || selected[category] >= count) return false;
    Module& m = modules[selected[category]];
    ImGui::BeginChild("##module-settings", ImVec2(0,0), false);
    ImGui::TextUnformatted(m.name); ImGui::Separator();
    if (m.settings == FLY2) eclient_fly2_adapter::draw();
    else if (m.settings == PRACTICE_TARGET) eclient_practice_target::draw();
    else if (m.settings == CRYSTAL_AURA) {
        eclient_crystal_aura::draw();
        if (ImGui::CollapsingHeader("Diagnostics and tools")) {
            if (ImGui::TreeNode("Crystal test")) { eclient_combat_diagnostics::draw(); ImGui::TreePop(); }
            if (ImGui::TreeNode("Focused test")) { eclient_combat_diagnostics::drawFocused(); ImGui::TreePop(); }
            if (ImGui::TreeNode("Crystal break test")) { eclient_crystal_break_test::draw(); ImGui::TreePop(); }
        }
    }
    else if ((m.settings == SPEED || m.settings == WATER || m.settings == LAVA || m.settings == JUMP || m.settings == GRAVITY) && isLANGame) {
        ImGui::TextWrapped("Unavailable in LAN games.");
    } else if (m.settings == FOG) {
        ImGui::Combo("Fog mode", &fogState, FogStatement, IM_ARRAYSIZE(FogStatement));
    } else if (m.settings == FONT) {
        ImGui::SliderInt("Font size", &fontSize, 0, 10);
    } else if (m.settings == LAN) {
        ImGui::DragInt("Host max players", &myMaxPlayers, 0.05f, 1, 1000);
        ImGui::TextWrapped("Re-enter the world to apply.");
    } else if (m.settings == CONFIG) {
        const std::string path = oxorany("/sdcard/Download/Apollon.json");
        ImGui::TextWrapped("Config: /sdcard/Download/Apollon.json");
        if (ImGui::Button("Load")) {
            std::ifstream file(path);
            if (file.good()) { loadConfig(path); onFunctionSound = true; AddNotification(oxorany("Config Loaded"), 6.0f, ImVec4(0.70f, 0.00f, 1.00f, 0.75f)); }
            else { errorFunctionSound = true; AddNotification(oxorany("Config not found"), 6.0f, ImVec4(1.0f, 0.2f, 0.2f, 0.75f)); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save")) { saveConfig(path); onFunctionSound = true; AddNotification(oxorany("Config Saved"), 6.0f, ImVec4(0.70f, 0.00f, 1.00f, 0.75f)); }
    } else if (m.settings == SPEED || m.settings == WATER || m.settings == LAVA) {
        int* v = m.settings == SPEED ? &speedValue : m.settings == WATER ? &waterSpeedValue : &lavaSpeedValue;
        bool active = *v != 1;
        if (ImGui::Button(active ? "Disable" : "Enable")) *v = active ? 1 : 2;
        ImGui::SameLine(); ImGui::TextUnformatted(active ? "Enabled" : "Disabled");
        ImGui::SliderInt("Multiplier", v, 1, 50);
    } else if (m.settings == JUMP) {
        bool active = jumpValue != 1.0f;
        if (ImGui::Button(active ? "Disable" : "Enable")) jumpValue = active ? 1.0f : 2.0f;
        ImGui::SameLine(); ImGui::TextUnformatted(active ? "Enabled" : "Disabled");
        ImGui::SliderFloat("Multiplier", &jumpValue, 1, 50, "%.1f");
    } else if (m.settings == GRAVITY) {
        bool active = gravityValue != -0.08f;
        if (ImGui::Button(active ? "Disable" : "Enable")) gravityValue = active ? -0.08f : 0.0f;
        ImGui::SameLine(); ImGui::TextUnformatted(active ? "Enabled" : "Disabled");
        ImGui::SliderFloat("Gravity", &gravityValue, -0.08f, 0.08f, "%.2f");
    } else if (m.settings == CHAMS) {
        bool active = chamsint != 0;
        if (ImGui::Button(active ? "Disable" : "Enable")) chamsint = active ? 0 : 1;
        ImGui::SameLine(); ImGui::TextUnformatted(active ? "Enabled" : "Disabled");
        ImGui::Combo("Mode", &chamsint, Chams, IM_ARRAYSIZE(Chams));
        if (chamsint > 1) { ImGui::ColorEdit3("Visible color", (float*)&visibleColor); ImGui::Checkbox("Rainbow", &enableRainbow); }
        if (chamsint == 6 || chamsint == 7) { ImGui::ColorEdit3("Wall color", (float*)&inWallColor); ImGui::Checkbox("Wall rainbow", &enableRainbowWall); }
    } else {
        bool active = *m.toggle;
        if (ImGui::Button(active ? "Disable" : "Enable")) {
            *m.toggle = !active;
            if (m.settings == XRAY) rebuildChunkGeometry = true;
            if (m.toggle == &antiBlind && *m.toggle && fogState == 0) fogState = 1;
        }
        ImGui::SameLine(); ImGui::TextUnformatted(active ? "Enabled" : "Disabled");
        switch (m.settings) {
        case AURA: auraSettings(false); break;
        case MOBAURA: auraSettings(true); break;
        case HITBOX: ImGui::SliderFloat("Size", &hitBoxValue, 0.6f, 10.0f, "%.1f"); break;
        case AUTOCLICK: ImGui::SliderInt("Delay", &maxAutoClickTick, 1, 150); break;
        case FOV: ImGui::SliderFloat("FOV", &fov, -90, 90, "%.0f"); break;
        case POSITION: ImGui::SliderFloat("X", &offsetTextPosX,-1500,1500,"%.0f"); ImGui::SliderFloat("Y", &offsetTextPosY,-1000,1000,"%.0f"); break;
        case PLAYERS: ImGui::SliderFloat("X", &offsetTextPlayerNearPosX,-1500,1500,"%.0f"); ImGui::SliderFloat("Y", &offsetTextPlayerNearPosY,-1000,1000,"%.0f"); break;
        case ESP: ImGui::Checkbox("Box",&espBox); ImGui::Checkbox("Line",&espLine); ImGui::Checkbox("Distance",&espDistance); ImGui::Checkbox("Nametag",&espNametag); ImGui::Checkbox("Health",&espHealth); ImGui::Checkbox("Platform",&espPlatform); break;
        case XRAY: {
            ImGui::Checkbox("Xray Caves", &xrayCaves);
            ImGui::Checkbox("Xray Camera (Third Person)", &xrayCam);
            struct Filter { const char* name; bool* value; };
            Filter filters[] = {{"Diamond",&xrayDiamond},{"Iron",&xrayIron},{"Gold",&xrayGold},
                {"Coal",&xrayCoal},{"Copper",&xrayCopper},{"Lapis",&xrayLapis},{"Emerald",&xrayEmerald},
                {"Redstone",&xrayRedstone},{"Amethyst",&xrayAmethyst},{"Netherite",&xrayNetherite},
                {"Quartz",&xrayQuartz},{"Obsidian",&xrayObsidian},{"Barrel",&xrayBarrel}};
            ImGui::TextUnformatted("Visible ores and blocks:");
            for (auto& filter : filters) if (ImGui::Checkbox(filter.name, filter.value) && *filter.value) rebuildChunkGeometry = true;
            break;
        }
        default: ImGui::TextUnformatted("No additional settings."); break;
        }
    }
    ImGui::EndChild();
    return true;
}
}
