#include "../apollon-adapter/schematic/cube_mesh.h"
#include <cassert>
#include <stdexcept>
using namespace eclient::schematic;
int main() {
    TextureCatalog catalog;
    catalog.addPack(R"({"stone":{"textures":"stone"},"grass":{"textures":{"up":"top","side":"side","down":"dirt"}}})",
        R"({"texture_data":{"stone":{"textures":"textures/blocks/stone"},"top":{"textures":"textures/blocks/top"},"side":{"textures":"textures/blocks/side"},"dirt":{"textures":"textures/blocks/dirt"}}})");
    Structure s; s.width=2;s.height=1;s.length=1;
    s.palette={{"minecraft:stone",{},0},{"minecraft:grass",{},0},{"minecraft:stairs",{},0}};
    s.primary={0,1};s.secondary={-1,-1}; Anchor a; assert(a.set({10,20,30}));
    auto mesh=buildCubeMesh(s,a,catalog); assert(mesh.faces.size()==10);
    assert(mesh.faces[0].texture=="textures/blocks/stone");
    assert(mesh.faces[5].texture=="textures/blocks/top");
    assert(mesh.skippedBlocks==0 && mesh.missingTextures==0);
    assert(a.rotate(1)); a.lock(true); assert(!a.set({0,0,0}));
    mesh=buildCubeMesh(s,a,catalog); assert(mesh.faces.size()==10);
    for(const auto& face:mesh.faces) for(const auto& v:face.vertices) {
        assert(v.x>=10 && v.x<=11 && v.y>=20 && v.y<=21 && v.z>=30 && v.z<=32);
    }
    a.lock(false);
    for(unsigned rotation=0; rotation<4; ++rotation) {
        assert(a.rotate(rotation));
        auto rotated=buildCubeMesh(s,a,catalog);
        assert(rotated.faces.size()==10);
        auto cell=a.world(0,0,0);
        for(int f=0;f<5;f++) for(const auto& v:rotated.faces[f].vertices) {
            assert(v.x>=cell.x && v.x<=cell.x+1 && v.z>=cell.z && v.z<=cell.z+1);
        }
    }
    s.primary[1]=2;s.secondary[0]=2;
    mesh=buildCubeMesh(s,a,catalog);assert(mesh.faces.size()==6 && mesh.skippedBlocks==1 && mesh.secondaryBlocks==1);
    s.palette[0].states["example"]={};mesh=buildCubeMesh(s,a,catalog);assert(mesh.statefulBlocks==1);
    try {buildCubeMesh(s,a,catalog,5);assert(false);}catch(const std::runtime_error&){}
    s.primary[0]=99;try {buildCubeMesh(s,a,catalog);assert(false);}catch(const std::runtime_error&){}
}
