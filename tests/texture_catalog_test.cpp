#include "../apollon-adapter/schematic/texture_catalog.h"
#include <cassert>
#include <stdexcept>
using eclient::schematic::TextureCatalog;
int main() {
    TextureCatalog c;
    c.addPack(R"({ // comment
      "stone":{"textures":"stone"},
      "grass":{"textures":{"up":"grass_top","side":"grass_side","down":"dirt"}},
      "variant":{"textures":"variants"}, "bad":{"textures":"escape"}
    })", R"({"texture_data":{
      "stone":{"textures":"textures/blocks/stone"},
      "grass_top":{"textures":"textures/blocks/grass_top"},
      "grass_side":{"textures":"textures/blocks/grass_side"},
      "dirt":{"textures":{"path":"textures/blocks/dirt"}},
      "variants":{"textures":["textures/blocks/first","textures/blocks/second"]},
      "escape":{"textures":"textures/../../secret"}
    }})");
    assert(c.lookup("minecraft:stone","up").logicalPath=="textures/blocks/stone");
    assert(c.lookup("grass","north").logicalPath=="textures/blocks/grass_side");
    assert(c.lookup("grass","up").logicalPath=="textures/blocks/grass_top");
    assert(c.lookup("grass","down").logicalPath=="textures/blocks/dirt");
    assert(c.lookup("variant","up").variantFallback);
    assert(c.lookup("bad","up").logicalPath.empty());
    assert(c.lookup("other:stone","up").logicalPath.empty());
    c.addPack(R"({"stone":{"textures":"new"}})", R"({"texture_data":{"new":{"textures":"textures/blocks/new"}}})");
    assert(c.lookup("stone","up").logicalPath=="textures/blocks/new");
    try {c.addPack("{}","not json");assert(false);}catch(const std::runtime_error&){}
    assert(c.lookup("stone","up").logicalPath=="textures/blocks/new");
    std::string deep(65,'[');deep+=std::string(65,']');
    try {c.addPack(deep,"");assert(false);}catch(const std::runtime_error&){}
}
