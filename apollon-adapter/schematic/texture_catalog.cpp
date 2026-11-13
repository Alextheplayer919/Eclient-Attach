#include "texture_catalog.h"
#include <cctype>
#include <stdexcept>

namespace eclient::schematic {
namespace {
using Json=nlohmann::json;
constexpr std::size_t MAX_JSON=8u*1024u*1024u;
// Prevent unbounded recursion in the JSON library. Ignore delimiters inside
// strings and comments; the parser itself validates syntax and escapes.
void checkDepth(const std::string& s) {
    if(s.size()>MAX_JSON) throw std::runtime_error("resource JSON too large");
    bool quoted=false, escape=false, line=false, block=false; int depth=0;
    for(std::size_t i=0;i<s.size();i++) {
        char c=s[i], next=i+1<s.size()?s[i+1]:'\0';
        if(line) { if(c=='\n'||c=='\r') line=false; continue; }
        if(block) { if(c=='*'&&next=='/') {block=false;i++;} continue; }
        if(quoted) { if(escape) escape=false; else if(c=='\\') escape=true; else if(c=='"') quoted=false; continue; }
        if(c=='"') quoted=true;
        else if(c=='/' && next=='/') {line=true;i++;}
        else if(c=='/' && next=='*') {block=true;i++;}
        else if(c=='{'||c=='[') { if(++depth>64) throw std::runtime_error("resource JSON too deeply nested"); }
        else if(c=='}'||c==']') { if(--depth<0) throw std::runtime_error("invalid resource JSON nesting"); }
    }
    if(quoted||block||depth!=0) throw std::runtime_error("invalid resource JSON");
}
Json parse(const std::string& input) {
    if(input.empty()) return Json::object();
    checkDepth(input);
    try { auto j=Json::parse(input, nullptr, true, true); if(!j.is_object()) throw std::runtime_error("resource root must be object"); return j; }
    catch(const Json::exception&) { throw std::runtime_error("invalid resource JSON"); }
}
const Json* get(const Json& j,const std::string& k) {
    if(!j.is_object()) return nullptr;
    auto it=j.find(k);
    return it==j.end()?nullptr:&*it;
}
const Json* first(const Json* value,bool& fallback) {
    if(value && value->is_array()) { if(value->empty())return nullptr; fallback=true; return &(*value)[0]; }
    return value;
}
bool validPath(const std::string& p) {
    if(p.size()<=9 || p.size()>256 || p.compare(0,9,"textures/")!=0 || p.find("..")!=std::string::npos || p.back()=='/') return false;
    for(unsigned char c:p) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c=='/')) return false;
    return true;
}
}
void TextureCatalog::addPack(const std::string& blocksJson,const std::string& terrainJson) {
    auto blocks=parse(blocksJson), terrain=parse(terrainJson);
    const Json* data=get(terrain,"texture_data");
    if(!terrainJson.empty() && (!data||!data->is_object())) throw std::runtime_error("terrain texture_data missing");
    Json newBlocks=blocks_,newTextures=textures_;
    for(auto it=blocks.begin();it!=blocks.end();++it) if(it.value().is_object()) newBlocks[it.key()]=it.value();
    if(data)for(auto it=data->begin();it!=data->end();++it)newTextures[it.key()]=it.value();
    blocks_.swap(newBlocks);textures_.swap(newTextures);
}
TextureResult TextureCatalog::lookup(const std::string& blockId,const std::string& face) const {
    if(face!="up" && face!="down" && face!="north" && face!="south" && face!="east" && face!="west") return {};
    std::string key=blockId;
    if(key.compare(0,10,"minecraft:")==0) key.erase(0,10);
    else if(key.find(':')!=std::string::npos) return {};
    const Json* block=get(blocks_,key); if(!block)return {};
    const Json* selected=get(*block,"textures"); bool fallback=false;
    if(selected && selected->is_object()) {
        const Json* perFace=get(*selected,face);
        if(!perFace && face!="up" && face!="down") perFace=get(*selected,"side");
        if(!perFace) perFace=get(*selected,"all");
        selected=perFace;
    }
    selected=first(selected,fallback); if(!selected||!selected->is_string())return {};
    auto textureKey=selected->get<std::string>();
    const Json* entry=get(textures_,textureKey); if(!entry)return {};
    selected=first(get(*entry,"textures"),fallback);
    if(selected && selected->is_object()) selected=get(*selected,"path");
    if(!selected||!selected->is_string())return {};
    auto path=selected->get<std::string>(); if(!validPath(path))return {};
    return {path,fallback};
}
}
