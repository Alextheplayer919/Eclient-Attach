#include "cube_mesh.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace eclient::schematic {
namespace {
bool air(const PaletteEntry& p) { return p.name=="minecraft:air" || p.name=="air"; }
bool cube(const PaletteEntry& p) {
    return p.name=="minecraft:stone" || p.name=="stone" ||
           p.name=="minecraft:dirt" || p.name=="dirt" ||
           p.name=="minecraft:grass" || p.name=="grass";
}
// Corners of each local face, wound outwards. UVs are provisional.
struct FaceDef { int dx,dy,dz; const char* name; int c[4][3]; };
constexpr FaceDef defs[] = {
 {0,1,0,"up",{{0,1,0},{0,1,1},{1,1,1},{1,1,0}}},
 {0,-1,0,"down",{{0,0,1},{0,0,0},{1,0,0},{1,0,1}}},
 {0,0,-1,"north",{{1,0,0},{0,0,0},{0,1,0},{1,1,0}}},
 {0,0,1,"south",{{0,0,1},{1,0,1},{1,1,1},{0,1,1}}},
 {1,0,0,"east",{{1,0,1},{1,0,0},{1,1,0},{1,1,1}}},
 {-1,0,0,"west",{{0,0,0},{0,0,1},{0,1,1},{0,1,0}}}
};
}
CubeMesh buildCubeMesh(const Structure& s,const Anchor& anchor,const TextureCatalog& catalog,std::size_t limit) {
    if(s.width<=0 || s.height<=0 || s.length<=0) throw std::runtime_error("invalid mesh dimensions");
    std::uint64_t wh=std::uint64_t(s.width)*std::uint64_t(s.height);
    if(wh>2000000 || wh*std::uint64_t(s.length)>2000000) throw std::runtime_error("mesh exceeds cell limit");
    const auto cells=static_cast<std::size_t>(wh*std::uint64_t(s.length));
    if(s.primary.size()!=cells || s.secondary.size()!=cells) throw std::runtime_error("invalid mesh layers");
    for(const auto* layer : {&s.primary,&s.secondary})
        for(auto i:*layer) if(i < -1 || (i>=0 && std::size_t(i)>=s.palette.size())) throw std::runtime_error("invalid mesh palette index");
    CubeMesh result;
    const auto index=[&](int x,int y,int z){return (std::size_t(x)*std::size_t(s.height)+std::size_t(y))*std::size_t(s.length)+std::size_t(z);};
    for(int x=0;x<s.width;x++) for(int y=0;y<s.height;y++) for(int z=0;z<s.length;z++) {
        auto idx=index(x,y,z); auto n=s.primary[idx], n2=s.secondary[idx];
        if(n2>=0 && !air(s.palette[n2])) ++result.secondaryBlocks;
        if(n<0 || air(s.palette[n])) continue;
        const auto& p=s.palette[n];
        if(!cube(p)){++result.skippedBlocks;continue;}
        if(!p.states.empty()) ++result.statefulBlocks;
        // anchor.world rotates BLOCK positions; rotating corners around that
        // block's minimum would shift its cell for turns 1-3. Rotate corners
        // in local face space around the cube centre instead.
        auto b=anchor.world(x,y,z);
        for(const auto& f:defs) {
            int nx=x+f.dx,ny=y+f.dy,nz=z+f.dz;
            if(nx>=0 && ny>=0 && nz>=0 && nx<s.width && ny<s.height && nz<s.length) {
                auto other=s.primary[index(nx,ny,nz)];
                if(other>=0 && cube(s.palette[other])) continue;
            }
            auto tex=catalog.lookup(p.name,f.name);
            if(tex.logicalPath.empty()){++result.missingTextures;continue;}
            if(result.faces.size()>=limit) throw std::runtime_error("mesh face limit exceeded");
            MeshFace face{}; face.texture=std::move(tex.logicalPath);face.variantFallback=tex.variantFallback;
            constexpr float uv[4][2]={{0,1},{1,1},{1,0},{0,0}};
            for(int i=0;i<4;i++) {
                int xx=f.c[i][0], zz=f.c[i][2], rx=xx,rz=zz;
                switch(anchor.rotation()) {
                case 1: rx=1-zz;rz=xx;break;
                case 2: rx=1-xx;rz=1-zz;break;
                case 3: rx=zz;rz=1-xx;break;
                }
                face.vertices[i]={double(b.x)+rx,double(b.y)+f.c[i][1],double(b.z)+rz,uv[i][0],uv[i][1]};
            }
            result.faces.push_back(std::move(face));
        }
    }
    return result;
}
}
