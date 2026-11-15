#include "../apollon-adapter/schematic/mcstructure.h"
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <string>
using Bytes=std::vector<std::uint8_t>;
static void u32(Bytes& b,std::uint32_t v) { for(int i=0;i<4;i++) b.push_back((v>>(i*8))&255); }
static void str(Bytes& b,const std::string& s){ b.push_back(s.size()&255);b.push_back((s.size()>>8)&255);b.insert(b.end(),s.begin(),s.end()); }
static void tag(Bytes& b,int t,const char* name){b.push_back(t);str(b,name);}
static Bytes sample() {
    Bytes b{10,0,0};
    tag(b,9,"size");b.push_back(3);u32(b,3);u32(b,2);u32(b,2);u32(b,3);
    tag(b,10,"structure");
    tag(b,9,"block_indices");b.push_back(9);u32(b,2);
    b.push_back(3);u32(b,12);for(unsigned i=0;i<12;i++)u32(b,i==7?1:0);
    b.push_back(3);u32(b,12);for(unsigned i=0;i<12;i++)u32(b,0xffffffffu);
    tag(b,10,"palette");tag(b,10,"default");tag(b,9,"block_palette");b.push_back(10);u32(b,2);
    tag(b,8,"name");str(b,"minecraft:air");tag(b,10,"states");b.push_back(0);tag(b,3,"version");u32(b,7);b.push_back(0);
    tag(b,8,"name");str(b,"minecraft:stone");tag(b,10,"states");tag(b,1,"flag");b.push_back(1);tag(b,3,"axis");u32(b,2);tag(b,8,"orientation");str(b,"north");b.push_back(0);tag(b,3,"version");u32(b,7);b.push_back(0);
    b.push_back(0);b.push_back(0);b.push_back(0);b.push_back(0);return b;
}
static bool fails(const Bytes& b) {try { (void)eclient::schematic::readMcStructure(b);return false;}catch(const std::runtime_error&){return true;}}
int main() {
    auto b=sample();auto s=eclient::schematic::readMcStructure(b);
    assert(s.width==2 && s.height==2 && s.length==3 && s.palette.size()==2);
    assert(s.at(1,0,1)==1 && s.at(1,1,1)==0 && s.at(1,0,1,true)==-1);
    assert(s.palette[1].states.at("flag").number==1 && s.palette[1].states.at("axis").number==2);
    assert(s.palette[1].states.at("orientation").text=="north");
    try { (void)s.at(2,0,0);assert(false); } catch(const std::out_of_range&) {}
    b.pop_back();assert(fails(b)); b=sample();b.push_back(0);assert(fails(b));
    b=sample(); b[0]=9;assert(fails(b));
    b=sample();b[17]=0xff; b[18]=0xff; b[19]=0xff; b[20]=0x7f;assert(fails(b));
    Bytes huge(32u*1024u*1024u+1); assert(fails(huge));
}
