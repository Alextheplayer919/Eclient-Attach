#pragma once
// Included after Apollon's variables and Fly2 adapter. All toggles reference the
// existing module variables; category selection never toggles a module.
namespace eclient_module_menu {
struct Module { const char* name; bool* toggle; int settings; };
enum { SIMPLE, FLY2, BYPASS_FLY, GATYO_AURA, AURA, MOBAURA, HITBOX, AUTOCLICK, XRAY, ESP, FOV, POSITION, PLAYERS, SPEED, WATER, LAVA, JUMP, GRAVITY, CHAMS, FOG, FONT, LAN, CONFIG, SHORTCUTS, SHORTCUT_UP, SHORTCUT_DOWN, SCHEMATIC_PREVIEW, QUICK_COMMANDS };
static Module movement[] = {
    {"Fly2",nullptr,FLY2},{"Bypass Fly",nullptr,BYPASS_FLY},{"Speed",nullptr,SPEED},{"Water Speed",nullptr,WATER},{"Lava Speed",nullptr,LAVA},{"Jump",nullptr,JUMP},{"Gravity",nullptr,GRAVITY},{"Creative Fly",&creativeFly,SIMPLE},{"Force Creative Fly",&forceCreativeFly,SIMPLE},
    {"Ladder Fly",&ladderFly,SIMPLE},{"Damage Fly",&damageFly,SIMPLE},{"Always Sprint",&alwaysSprinting,SIMPLE},
    {"No SlowDown",&noSlowDown,SIMPLE},{"No Web",&offSlowdownTrigger,SIMPLE},{"No Jump Delay",&noJumpDelay,SIMPLE},
    {"Fast Fall",&fastFall,SIMPLE},{"Step",&step,SIMPLE},{"Noclip",&noclip,SIMPLE},{"No Water",&offWaterTrigger,SIMPLE},
    {"Strafe",&strafe,SIMPLE},{"No Drowning",&offDrown,SIMPLE}
};
static Module combat[] = {{"Gatyo KillAura",nullptr,GATYO_AURA},{"KillAura",&killAura,AURA},{"MobAura",&mobAura,MOBAURA},{"HitBox",&hitBox,HITBOX},
    {"Kill Near Items",&killItems,SIMPLE},{"Anti Fireball",&antiFireball,SIMPLE},
    {"Auto Clicker",&autoClicker,AUTOCLICK},{"Anti Knockback",&antiKnockback,SIMPLE},{"Anti Bot",&antiBot,SIMPLE}};
static Module building[] = {{"Schematic Preview",nullptr,SCHEMATIC_PREVIEW},{"Auto Mine",&clickAutoMine,SIMPLE},{"Auto Build",&fastBridge,SIMPLE},{"Block Reach",&blockReach,SIMPLE}};
static Module visuals[] = {{"Fog",nullptr,FOG},{"Font size",nullptr,FONT},{"Chams",nullptr,CHAMS},{"Xray",&xrayDefault,XRAY},
    {"ESP",&esp,ESP},{"FOV",&enableFov,FOV},{"FullBright",&fullBright,SIMPLE},{"FPS Boost",&osmtBoost,SIMPLE},
    {"Freelook",&look360,SIMPLE},{"No Hurt Cam",&noHurtCam,SIMPLE},{"No Cam Distortion",&noCamDist,SIMPLE},
    {"No Boat Rotation Limit",&noBoatRotation,SIMPLE},{"No Sleep Vignette",&noCamSleep,SIMPLE},
    {"Place Camera",&placeCam,SIMPLE},{"Anti Invisible",&antiInvis,SIMPLE},{"Derp",&derp,SIMPLE},
    {"No Blur",&noBlur,SIMPLE},{"Anti Blindness",&antiBlind,SIMPLE},{"Paperdoll",&showPaperdoll,SIMPLE},
    {"No Cave Vignette",&noCaveVignette,SIMPLE},{"Death Position",&deathPosition,SIMPLE},
    {"Show Position",&showPosition,POSITION},{"Near Players",&playerNear,PLAYERS}};
static Module other[] = {{"Quick Commands",nullptr,QUICK_COMMANDS},{"Shortcuts",nullptr,SHORTCUTS},{"LAN",nullptr,LAN},{"Config",nullptr,CONFIG},{"Marketplace",&unlockMarket,SIMPLE},{"Fast Drop",&fastDrop,SIMPLE},{"No Emote Cooldown",&noEmoteCooldown,SIMPLE}};
static Module actions[] = {{"Fly up (hold)",nullptr,SHORTCUT_UP},{"Fly down (hold)",nullptr,SHORTCUT_DOWN}};
static int selected[8] = {0,0,0,0,0,0,0,0};
static Module* list(int category, int& count) {
#define LIST(n, a) case n: count = IM_ARRAYSIZE(a); return a
    switch (category) { LIST(0,movement); LIST(1,combat); LIST(2,building); LIST(4,visuals); LIST(6,other); default: count=0; return nullptr; }
#undef LIST
}
static Module* shortcutList(int category,int& count) {
    if(category==99){count=IM_ARRAYSIZE(actions);return actions;}
    return list(category,count);
}
// Touch shortcuts follow Gatyo's model: one independent touch rectangle per
// floating ImGui button, with configurable modules, dragging and locking.
struct Shortcut {int category,index;float x,y;};
static std::vector<Shortcut> shortcuts;
static bool shortcutLock=false,shortcutLoaded=false,shortcutDragged=false;
static float shortcutScale=1.0f,shortcutOpacity=.85f;
static std::mutex shortcutRectsMutex;static std::string shortcutRects;
static std::string shortcutsPath(){return std::string("/data/data/")+GameName+"/xal/shortcuts.json";}
static bool shortcutSupported(const Module& m) {
    if(m.toggle)return true;
    if(m.settings==FLY2 || m.settings==BYPASS_FLY || m.settings==GATYO_AURA || m.settings==SHORTCUT_UP || m.settings==SHORTCUT_DOWN)return true;
#ifdef ECLIENT_COMBAT_LAB
    if(m.settings==CRYSTAL_AURA_TEST || m.settings==AUTO_TOTEM_TEST || m.settings==FAKE_PLAYER_TEST)return true;
#endif
    return false;
}
static bool shortcutState(const Module& m) {
    if(m.toggle)return *m.toggle;
    if(m.settings==FLY2)return eclient_fly2_adapter::enabled.load();
    if(m.settings==BYPASS_FLY)return eclient_bypass_fly::bypassFly.load();
    if(m.settings==GATYO_AURA)return eclient_gatyo_aura::enabled.load();
#ifdef ECLIENT_COMBAT_LAB
    if(m.settings==CRYSTAL_AURA_TEST)return eclient_gatyo::autoPlace.load();
    if(m.settings==AUTO_TOTEM_TEST)return eclient_gatyo::autoTotem.load();
    if(m.settings==FAKE_PLAYER_TEST)return eclient_fake::status.load()!=0;
#endif
    return false;
}
static void shortcutToggle(const Module& m) {
    if(m.toggle){*m.toggle=!*m.toggle;return;}
    bool value=!shortcutState(m);
    if(m.settings==FLY2)eclient_fly2_adapter::enabled.store(value);
    else if(m.settings==BYPASS_FLY)eclient_bypass_fly::bypassFly.store(value);
    else if(m.settings==GATYO_AURA)eclient_gatyo_aura::enabled.store(value);
#ifdef ECLIENT_COMBAT_LAB
    else if(m.settings==CRYSTAL_AURA_TEST){eclient_gatyo::autoPlace.store(value);eclient_combat_lab::breakAura.store(value);}
    else if(m.settings==AUTO_TOTEM_TEST)eclient_gatyo::autoTotem.store(value);
    else if(m.settings==FAKE_PLAYER_TEST)eclient_fake::request.store(value?1:2);
#endif
}
static void saveShortcuts(){
    try {json root;root["locked"]=shortcutLock;root["scale"]=shortcutScale;root["opacity"]=shortcutOpacity;root["buttons"]=json::array();
        for(const auto& b:shortcuts){int count=0;Module* modules=shortcutList(b.category,count);if(modules && b.index>=0 && b.index<count)root["buttons"].push_back({b.category,modules[b.index].name,b.x,b.y});}
        std::error_code ec;fs::create_directories(std::string("/data/data/")+GameName+"/xal",ec);
        std::ofstream out(shortcutsPath(),std::ios::trunc);if(out)out<<root.dump(2);
    }catch(const std::exception&){}
}
static void loadShortcuts(){
    if(shortcutLoaded)return;shortcutLoaded=true;
    try {std::ifstream in(shortcutsPath());if(!in)return;json root;in>>root;
        shortcutLock=root.value("locked",false);shortcutScale=std::clamp(root.value("scale",1.f),.6f,2.f);shortcutOpacity=std::clamp(root.value("opacity",.85f),.2f,1.f);
        if(!root.contains("buttons") || !root["buttons"].is_array())return;
        for(auto& value:root["buttons"]) {if(shortcuts.size()>=12)break;if(!value.is_array() || value.size()!=4)continue;
            int category=value[0].get<int>();std::string name=value[1].get<std::string>();float x=value[2].get<float>(),y=value[3].get<float>();
            if(!std::isfinite(x)||!std::isfinite(y))continue;int count=0;auto modules=shortcutList(category,count);for(int i=0;modules && i<count;i++)if(name==modules[i].name && shortcutSupported(modules[i])){shortcuts.push_back({category,i,x,y});break;}
        }
    }catch(const std::exception&){}
}
static void shortcutSettings(){
    loadShortcuts();static int category=1,pick=0;const char* names[]={"Movement","Combat","Building","Unused","Visuals","Unused","Other","Actions"};
    if(ImGui::Combo("Category##shortcut",&category,names,8))pick=0;
    int actualCategory=category==7?99:category;int count=0;auto modules=shortcutList(actualCategory,count);
    if(modules && count){pick=std::clamp(pick,0,count-1);if(ImGui::BeginCombo("Module##shortcut",modules[pick].name)){
        for(int i=0;i<count;i++)if(shortcutSupported(modules[i]) && ImGui::Selectable(modules[i].name,pick==i))pick=i;
        ImGui::EndCombo();}
        if(shortcutSupported(modules[pick]) && shortcuts.size()<12 && ImGui::Button("Add shortcut")){
            shortcuts.push_back({actualCategory,pick,screenWidth*.72f,screenHeight*.20f+float(shortcuts.size()%8)*55.f});saveShortcuts();}
    }
    if(ImGui::Checkbox("Lock shortcut positions",&shortcutLock))saveShortcuts();
    if(ImGui::SliderFloat("Shortcut scale",&shortcutScale,.6f,2.f,"%.2fx"))saveShortcuts();
    if(ImGui::SliderFloat("Shortcut opacity",&shortcutOpacity,.2f,1.f,"%.2f"))saveShortcuts();
    ImGui::TextWrapped("Tap a shortcut to toggle. Drag it to move, or lock positions. Buttons appear only on the game HUD.");
    int remove=-1;for(size_t i=0;i<shortcuts.size();++i){int n=0;auto* items=shortcutList(shortcuts[i].category,n);if(!items || shortcuts[i].index<0 || shortcuts[i].index>=n)continue;
        ImGui::PushID(int(i));if(ImGui::Button("Remove"))remove=int(i);ImGui::SameLine();ImGui::TextUnformatted(items[shortcuts[i].index].name);ImGui::PopID();}
    if(remove>=0){shortcuts.erase(shortcuts.begin()+remove);saveShortcuts();}
}
static std::string getShortcutRects(){std::lock_guard<std::mutex> lock(shortcutRectsMutex);return shortcutRects;}
static void drawShortcuts(){
    loadShortcuts();std::string rects;bool holdUp=false,holdDown=false;
    if(!panic && inHudScreen && screenWidth>0 && screenHeight>0){
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))shortcutDragged=false;
        auto& io=ImGui::GetIO();
        for(size_t i=0;i<shortcuts.size();i++){
            auto& b=shortcuts[i];int count=0;auto* modules=shortcutList(b.category,count);if(!modules||b.index<0||b.index>=count)continue;auto& m=modules[b.index];
            float h=std::max(36.f,52.f*scaleGlobal*shortcutScale),w=std::max(h*1.4f,ImGui::CalcTextSize(m.name).x+24.f*scaleGlobal*shortcutScale);
            w=std::min(w,float(screenWidth));h=std::min(h,float(screenHeight));b.x=std::clamp(b.x,0.f,float(screenWidth)-w);b.y=std::clamp(b.y,0.f,float(screenHeight)-h);
            ImVec2 pos(b.x,b.y);char title[32];snprintf(title,sizeof(title),"##shortcut_%d",int(i));
            ImGui::SetNextWindowPos(pos,ImGuiCond_Always);ImGui::SetNextWindowSize(ImVec2(w,h),ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
            auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoFocusOnAppearing;
            if(ImGui::Begin(title,nullptr,flags)){
                ImGui::InvisibleButton("##tap",ImVec2(w,h));bool held=ImGui::IsItemActive();
                if(held && !shortcutLock && ImGui::IsMouseDragging(ImGuiMouseButton_Left,std::max(10.f,14.f*scaleGlobal))){shortcutDragged=true;b.x+=io.MouseDelta.x;b.y+=io.MouseDelta.y;}
                bool action=m.settings==SHORTCUT_UP || m.settings==SHORTCUT_DOWN;
                if(ImGui::IsItemDeactivated()){if(shortcutDragged)saveShortcuts();else if(!action && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))shortcutToggle(m);}
                if(action && held && !shortcutDragged){if(m.settings==SHORTCUT_UP)holdUp=true;else holdDown=true;}
                bool on=action ? held && !shortcutDragged : shortcutState(m);ImDrawList* dl=ImGui::GetWindowDrawList();ImVec2 end(pos.x+w,pos.y+h);
                dl->AddRectFilled(pos,end,ImGui::GetColorU32(ImVec4(on?.31f:.09f,on?.22f:.09f,on?.52f:.12f,shortcutOpacity)),4);
                dl->AddRect(pos,end,IM_COL32(190,160,230,220),4);auto sz=ImGui::CalcTextSize(m.name);dl->AddText(ImVec2(pos.x+(w-sz.x)*.5f,pos.y+(h-sz.y)*.5f),IM_COL32_WHITE,m.name);
            }ImGui::End();ImGui::PopStyleVar();
            char entry[80];snprintf(entry,sizeof(entry),"%s%d|%d|%d|%d",rects.empty()?"":";",int(pos.x),int(pos.y),int(w),int(h));rects+=entry;
        }
    }
    eclient_bypass_fly::g_flyHoldUp.store(holdUp);eclient_bypass_fly::g_flyHoldDown.store(holdDown);
    std::lock_guard<std::mutex> guard(shortcutRectsMutex);shortcutRects=rects;
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
    if (m.settings == QUICK_COMMANDS) eclient_quick_commands::ui();
    else if (m.settings == SCHEMATIC_PREVIEW) eclient_preview::settings();
    else if (m.settings == FLY2) eclient_fly2_adapter::draw();
    else if (m.settings == BYPASS_FLY) eclient_bypass_fly::ui();
    else if (m.settings == GATYO_AURA) eclient_gatyo_aura::ui();
    else if (m.settings == SHORTCUTS) shortcutSettings();
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
