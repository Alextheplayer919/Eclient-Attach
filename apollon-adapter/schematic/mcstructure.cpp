#include "mcstructure.h"
#include <limits>
#include <stdexcept>
#include <utility>

namespace eclient::schematic {
namespace {
constexpr std::size_t MAX_BYTES = 32u*1024u*1024u, MAX_CELLS = 2000000, MAX_NODES = 100000;
struct Node {
    unsigned type=0;
    std::int32_t integer=0;
    std::string text;
    std::vector<std::int32_t> integers;
    std::vector<Node> list;
    std::map<std::string, Node> fields;
    unsigned element=0;
};
class Reader {
    const std::vector<std::uint8_t>& b;
    std::size_t pos=0, nodes=0;
    void require(std::size_t n) { if (n>b.size()-pos) throw std::runtime_error("truncated NBT"); }
    std::uint32_t u32() { require(4); auto v=std::uint32_t(b[pos])|(std::uint32_t(b[pos+1])<<8)|(std::uint32_t(b[pos+2])<<16)|(std::uint32_t(b[pos+3])<<24); pos+=4; return v; }
    std::size_t count() { auto n=u32(); if(n>MAX_CELLS) throw std::runtime_error("NBT list exceeds limit"); return n; }
    std::string string() { require(2); auto n=std::size_t(b[pos])|(std::size_t(b[pos+1])<<8); pos+=2; require(n); std::string r(reinterpret_cast<const char*>(b.data()+pos),n); pos+=n; return r; }
    void skip(std::size_t n) { require(n); pos+=n; }
    Node parse(unsigned type, unsigned depth) {
        if(depth>32 || ++nodes>MAX_NODES) throw std::runtime_error("NBT nesting or node limit exceeded");
        Node v; v.type=type;
        switch(type) {
        case 1: require(1); v.integer=static_cast<std::int8_t>(b[pos++]); break;
        case 2: require(2); v.integer=static_cast<std::int16_t>(unsigned(b[pos])|(unsigned(b[pos+1])<<8)); pos+=2; break;
        case 3: v.integer=static_cast<std::int32_t>(u32()); break;
        case 4: case 6: skip(8); break;
        case 5: skip(4); break;
        case 7: { auto n=count(); skip(n); break; }
        case 8: v.text=string(); break;
        case 9: {
            require(1); v.element=b[pos++]; auto n=count();
            if (v.element==0 && n!=0) throw std::runtime_error("invalid end-tag list");
            if(v.element==3) { require(n*4); v.integers.reserve(n); for(std::size_t i=0;i<n;i++) v.integers.push_back(static_cast<std::int32_t>(u32())); }
            else { if(n>MAX_NODES-nodes) throw std::runtime_error("NBT list node limit exceeded"); v.list.reserve(n); for(std::size_t i=0;i<n;i++) v.list.push_back(parse(v.element,depth+1)); }
            break;
        }
        case 10: {
            for(;;) { require(1); unsigned t=b[pos++]; if(!t) break; auto name=string(); Node value=parse(t,depth+1); if(!v.fields.emplace(std::move(name),std::move(value)).second) throw std::runtime_error("duplicate NBT key"); }
            break;
        }
        case 11: { auto n=count(); skip(n*4); break; }
        case 12: { auto n=count(); skip(n*8); break; }
        default: throw std::runtime_error("unknown NBT type");
        }
        return v;
    }
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes):b(bytes) { if(b.empty()||b.size()>MAX_BYTES) throw std::runtime_error("structure input size invalid"); }
    Node root() { require(1); if(b[pos++]!=10) throw std::runtime_error("NBT root must be compound"); string(); auto r=parse(10,0); if(pos!=b.size()) throw std::runtime_error("trailing NBT bytes"); return r; }
};
const Node& field(const Node& n,const char* key,unsigned type) {
    auto it=n.fields.find(key);
    if(n.type!=10 || it==n.fields.end() || it->second.type!=type) throw std::runtime_error(std::string("missing or invalid ")+key);
    return it->second;
}
}
std::int32_t Structure::at(int x,int y,int z,bool second) const {
    if(x<0 || y<0 || z<0 || x>=width || y>=height || z>=length) throw std::out_of_range("structure coordinate");
    return (second?secondary:primary).at((std::size_t(x)*height+y)*length+z);
}
Structure readMcStructure(const std::vector<std::uint8_t>& bytes) {
    const Node root=Reader(bytes).root();
    auto& size=field(root,"size",9);
    if(size.element!=3 || size.integers.size()!=3) throw std::runtime_error("size must contain three integers");
    Structure out; out.width=size.integers[0]; out.height=size.integers[1]; out.length=size.integers[2];
    if(out.width<=0 || out.height<=0 || out.length<=0) throw std::runtime_error("invalid structure dimensions");
    std::uint64_t cells=std::uint64_t(out.width)*std::uint64_t(out.height);
    if(cells>MAX_CELLS || cells*std::uint64_t(out.length)>MAX_CELLS) throw std::runtime_error("structure exceeds cell limit");
    cells*=out.length;
    auto& st=field(root,"structure",10); auto& layers=field(st,"block_indices",9);
    if(layers.element!=9 || layers.list.size()!=2 || layers.list[0].element!=3 || layers.list[1].element!=3 || layers.list[0].integers.size()!=cells || layers.list[1].integers.size()!=cells) throw std::runtime_error("invalid block layers");
    auto& p=field(field(field(st,"palette",10),"default",10),"block_palette",9);
    if(p.element!=10 || p.list.empty() || p.list.size()>65536) throw std::runtime_error("invalid block palette");
    for(const auto& entry:p.list) {
        PaletteEntry block; block.name=field(entry,"name",8).text;
        if(block.name.empty()) throw std::runtime_error("empty block name");
        auto version=entry.fields.find("version"); if(version!=entry.fields.end()) { if(version->second.type!=3) throw std::runtime_error("invalid palette version"); block.version=version->second.integer; }
        auto& states=field(entry,"states",10);
        for(const auto& [key,value]:states.fields) {
            BlockState state;
            if(value.type==1) {state.type=BlockState::Type::Byte;state.number=value.integer;}
            else if(value.type==3) {state.type=BlockState::Type::Int;state.number=value.integer;}
            else if(value.type==8) {state.type=BlockState::Type::String;state.text=value.text;}
            else throw std::runtime_error("unsupported block state type");
            block.states.emplace(key,std::move(state));
        }
        out.palette.push_back(std::move(block));
    }
    for(auto* layer:{&layers.list[0].integers,&layers.list[1].integers}) for(auto i:*layer) if(i < -1 || (i>=0 && static_cast<std::size_t>(i)>=out.palette.size())) throw std::runtime_error("invalid palette index");
    out.primary=layers.list[0].integers; out.secondary=layers.list[1].integers;
    return out;
}
}
