#include "../apollon-adapter/schematic/texture_catalog.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
static std::string load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if(!f)throw std::runtime_error("missing input "+path);
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        const std::string root=argv[1]; eclient::schematic::TextureCatalog catalog;
        catalog.addPack(load(root+"/blocks.json"),load(root+"/textures/terrain_texture.json"));
        struct Case {const char* block; const char* face;};
        for(const auto& test:{Case{"stone","up"},Case{"grass","up"},Case{"grass","north"},Case{"grass","down"},Case{"dirt","up"}}) {
            auto result=catalog.lookup(test.block,test.face);
            if(result.logicalPath.empty())throw std::runtime_error(std::string("unresolved block: ")+test.block+" "+test.face);
            auto path=root+"/"+result.logicalPath;
            if(path.size()<4 || path.substr(path.size()-4)!=".png")path+=".png";
            std::ifstream image(path,std::ios::binary);
            if(!image)throw std::runtime_error("missing resolved texture: "+path);
            std::cout<<test.block<<" "<<test.face<<"="<<result.logicalPath<<" variant_fallback="<<result.variantFallback<<"\n";
        }
    } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
