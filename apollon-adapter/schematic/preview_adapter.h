#pragma once
// Experimental overlay prototype. Included in Apollon's single main.cpp after
// its ImGui, stb_image and GL declarations. Runs only on the overlay GL thread.
#include "cube_mesh.h"
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eclient_preview {
using namespace eclient::schematic;
static bool enabled=false, locked=false, tried=false;
static int position[3]={0,64,0}, turns=0;
static AAssetManager* assets=nullptr;
static jobject assetRef=nullptr;
static std::unique_ptr<TextureCatalog> catalog;
static CubeMesh mesh;
static std::map<std::string,GLuint> textures;
static std::string status="Place the test blocks near the camera, then lock.";

static void setAssets(JNIEnv* env, jobject manager) {
    // Called on the newly created overlay GL context. Old GL names are invalid;
    // do not delete them in a different context.
    textures.clear(); tried=false; catalog.reset();
    if(assetRef) env->DeleteGlobalRef(assetRef);
    assetRef=manager ? env->NewGlobalRef(manager) : nullptr;
    assets=assetRef ? AAssetManager_fromJava(env,assetRef) : nullptr;
}
static std::vector<unsigned char> readAsset(const std::string& path,std::size_t max) {
    if(!assets) return {};
    AAsset* asset=AAssetManager_open(assets,path.c_str(),AASSET_MODE_STREAMING);
    if(!asset) return {};
    const auto length=AAsset_getLength64(asset);
    if(length<=0 || static_cast<std::uint64_t>(length)>max) { AAsset_close(asset); return {}; }
    std::vector<unsigned char> data(static_cast<std::size_t>(length));
    std::size_t pos=0;
    while(pos<data.size()) {
        int n=AAsset_read(asset,data.data()+pos,data.size()-pos);
        if(n<=0) {data.clear();break;}
        pos+=static_cast<std::size_t>(n);
    }
    AAsset_close(asset);return data;
}
static bool loadCatalog() {
    if(tried) return bool(catalog);
    tried=true;
    auto blocks=readAsset("assets/resource_packs/vanilla/blocks.json",8*1024*1024);
    auto terrain=readAsset("assets/resource_packs/vanilla/textures/terrain_texture.json",8*1024*1024);
    if(blocks.empty() || terrain.empty()) {status="Vanilla texture maps unavailable in APK.";return false;}
    try {
        auto c=std::make_unique<TextureCatalog>();
        c->addPack(std::string(blocks.begin(),blocks.end()),std::string(terrain.begin(),terrain.end()));
        catalog=std::move(c);return true;
    } catch(const std::exception&) {status="Could not parse vanilla texture maps.";return false;}
}
static bool rebuild() {
    if(!loadCatalog()) return false;
    try {
        Structure s;s.width=2;s.height=1;s.length=1;
        s.palette={{"minecraft:stone",{},0},{"minecraft:grass",{},0}};
        s.primary={0,1};s.secondary={-1,-1};
        Anchor anchor;
        if(!anchor.set({position[0],position[1],position[2]}) || !anchor.rotate(static_cast<unsigned>(turns))) {status="Coordinates out of range.";return false;}
        anchor.lock(true);mesh=buildCubeMesh(s,anchor,*catalog,64);
        if(mesh.faces.size()!=10 || mesh.missingTextures) {status="Some test block textures were not resolved.";return false;}
        status="Locked: two test blocks. This is not schematic import.";return true;
    } catch(const std::exception&) {status="Could not build test geometry.";return false;}
}
static GLuint texture(const std::string& logical) {
    auto found=textures.find(logical);if(found!=textures.end()) return found->second;
    auto name=logical;
    if(name.size()<4 || name.substr(name.size()-4)!=".png") name+=".png";
    auto bytes=readAsset("assets/resource_packs/vanilla/"+name,8*1024*1024);
    int w=0,h=0,channels=0;
    GLuint id=0;
    if(!bytes.empty() && stbi_info_from_memory(bytes.data(),int(bytes.size()),&w,&h,&channels) && w>0 && h>0 && w<=4096 && h<=4096 && std::uint64_t(w)*h<=4194304) {
        unsigned char* rgba=stbi_load_from_memory(bytes.data(),int(bytes.size()),&w,&h,&channels,STBI_rgb_alpha);
        if(rgba) {
            GLint old=0,alignment=4;glGetIntegerv(GL_TEXTURE_BINDING_2D,&old);glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
            glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT,1);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
            glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);glBindTexture(GL_TEXTURE_2D,GLuint(old));stbi_image_free(rgba);
        }
    }
    textures.emplace(logical,id);return id;
}
static void settings() {
    ImGui::Checkbox("Show test preview",&enabled);
    ImGui::TextWrapped("Experimental two-block preview; no schematic import yet. Separate overlay, no world depth.");
    if(!locked) {
        if(ImGui::Button("Place near camera")) {
            if(inHudScreen && std::isfinite(CameraPos.x) && std::isfinite(CameraPos.y) && std::isfinite(CameraPos.z) &&
               std::abs(CameraPos.x)<29999990 && std::abs(CameraPos.y)<29999990 && std::abs(CameraPos.z)<29999990) {
                position[0]=int(std::floor(CameraPos.x)); position[1]=int(std::floor(CameraPos.y)); position[2]=int(std::floor(CameraPos.z))-4;
                status="Position set north of camera; lock to display.";
            } else status="Camera position unavailable.";
        }
        ImGui::InputInt3("World position",position);
        ImGui::SliderInt("Quarter turns",&turns,0,3);
        if(ImGui::Button("Lock preview")) locked=rebuild();
    } else {
        ImGui::Text("Locked at %d, %d, %d",position[0],position[1],position[2]);
        if(ImGui::Button("Unlock preview")) locked=false;
    }
    ImGui::TextWrapped("%s",status.c_str());
}
static void draw(ImDrawList* list,vec3_t camera,glm::vec2 fov,glm::mat4 matrix) {
    if(!enabled || !locked || !list || screenWidth<=0 || screenHeight<=0) return;
    // Reload textures/maps after Android destroys and recreates the GL context.
    if(!loadCatalog()) return;
    struct Projected {const MeshFace* face; ImVec2 pts[4]; double distance;};
    std::vector<Projected> visible; visible.reserve(mesh.faces.size());
    glm::vec3 origin(camera.x,camera.y,camera.z);
    if(!std::isfinite(camera.x)||!std::isfinite(camera.y)||!std::isfinite(camera.z)||!std::isfinite(fov.x)||!std::isfinite(fov.y)) return;
    for(const auto& face:mesh.faces) {
        const auto& a=face.vertices[0];const auto& b=face.vertices[1];const auto& c=face.vertices[2];
        double ax=b.x-a.x,ay=b.y-a.y,az=b.z-a.z,bx=c.x-a.x,by=c.y-a.y,bz=c.z-a.z;
        double nx=ay*bz-az*by,ny=az*bx-ax*bz,nz=ax*by-ay*bx;
        if(nx*(camera.x-a.x)+ny*(camera.y-a.y)+nz*(camera.z-a.z)<=0) continue;
        Projected out{&face,{},0};bool valid=true;
        for(int i=0;i<4;i++) {
            const auto& v=face.vertices[i];glm::vec3 p(float(v.x-camera.x),float(v.y-camera.y),float(v.z-camera.z));
            const float z=glm::dot(glm::vec4(matrix[2].x,matrix[2].y,matrix[2].z,matrix[2].w),glm::vec4(p,1.0f));
            glm::vec2 point;
            if(!std::isfinite(z) || z>=-0.05f || !OWorldToScreen(origin,glm::vec3(float(v.x),float(v.y),float(v.z)),point,fov,glm::vec2(screenWidth,screenHeight),matrix) || !std::isfinite(point.x)||!std::isfinite(point.y)) {valid=false;break;}
            out.pts[i]=ImVec2(point.x,point.y);out.distance+=double(p.x)*p.x+double(p.y)*p.y+double(p.z)*p.z;
        }
        if(valid) visible.push_back(out);
    }
    std::sort(visible.begin(),visible.end(),[](const Projected& a,const Projected& b){return a.distance>b.distance;});
    for(const auto& face:visible) {
        GLuint id=texture(face.face->texture);if(!id) {status="A resolved texture image could not be loaded.";continue;}
        const auto& v=face.face->vertices;
        list->AddImageQuad((ImTextureID)(intptr_t)id,face.pts[0],face.pts[1],face.pts[2],face.pts[3],
                           ImVec2(v[0].u,v[0].v),ImVec2(v[1].u,v[1].v),ImVec2(v[2].u,v[2].v),ImVec2(v[3].u,v[3].v),IM_COL32(255,255,255,160));
    }
}
}
