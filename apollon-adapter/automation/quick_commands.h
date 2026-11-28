#pragma once
// Gatyo's MinecraftScreenModel::executeCommand adapter, pinned to 1.21.111.
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
namespace eclient_quick_commands {
static std::mutex mutex;
static std::deque<std::string> queue;
static std::vector<std::string> commands{"/spawn"};
static std::atomic<int> editRequest{-1};
static bool loaded=false;
static std::string path(){return std::string("/data/data/")+GameName+"/xal/quick-commands.json";}
static void load(){
    if(loaded)return;loaded=true;
    try{std::ifstream in(path());if(!in)return;json data;in>>data;if(!data.is_array())return;
        std::vector<std::string> found;for(auto& v:data){if(found.size()>=24)break;if(v.is_string()){auto t=v.get<std::string>();if(t.size()>1 && t.size()<=256 && t[0]=='/')found.push_back(t);}}commands=found;
    }catch(const std::exception&){}
}
static void save(){try{std::error_code ec;fs::create_directories(std::string("/data/data/")+GameName+"/xal",ec);std::ofstream out(path(),std::ios::trunc);if(out)out<<json(commands).dump(2);}catch(const std::exception&){} }
static void submitEdit(int index,const std::string& text){
    std::lock_guard<std::mutex> lock(mutex);load();
    if(text.size()<2 || text.size()>256 || text[0]!='/' || text.find_first_of("\r\n")!=std::string::npos)return;
    if(index>=0 && index<(int)commands.size())commands[index]=text;
    else if(index==24 && commands.size()<24)commands.push_back(text);else return;
    save();
}
static void clearQueue(){std::lock_guard<std::mutex> lock(mutex);queue.clear();}
static void flush(){
    std::string command;{std::lock_guard<std::mutex> lock(mutex);if(queue.empty())return;command=queue.front();queue.pop_front();}
#if defined(__aarch64__)
    if(!CI || !localPlayer)return;
    alignas(16) void* model[16]={};model[10]=CI;
    auto fn=reinterpret_cast<void(*)(void*,const std::string&)>(getAbsoluteAddress(MineLib,0x7029FC8));
    if(fn)fn(model,command);
#endif
}
static void ui(){
    std::lock_guard<std::mutex> lock(mutex);load();
    ImGui::TextWrapped("Saved slash commands. Send while in a world; the server decides whether it accepts them.");
    int remove=-1;
    for(int i=0;i<(int)commands.size();++i){ImGui::PushID(i);
        if(ImGui::Button("Send") && !inMenu && localPlayer && queue.size()<16)queue.push_back(commands[i]);
        ImGui::SameLine();if(ImGui::Button("Edit"))editRequest.store(i);
        ImGui::SameLine();if(ImGui::Button("Remove"))remove=i;
        ImGui::SameLine();ImGui::TextUnformatted(commands[i].c_str());ImGui::PopID();}
    if(remove>=0){commands.erase(commands.begin()+remove);save();}
    if(commands.size()<24 && ImGui::Button("Add command"))editRequest.store(24);
}
static std::string editText(int index){std::lock_guard<std::mutex> lock(mutex);load();return index>=0 && index<(int)commands.size()?commands[index]:"/";}
}
