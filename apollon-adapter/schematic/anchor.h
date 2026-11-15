#pragma once
// World-space placement state only; this does not render or access Minecraft.
#include <cstdint>
namespace eclient::schematic {
struct Position { std::int64_t x=0, y=0, z=0; };
class Anchor {
    Position position_{};
    unsigned rotation_=0;
    bool locked_=false;
public:
    bool set(Position p) {
        constexpr std::int64_t bound = 30000000;
        if(locked_ || p.x < -bound || p.x > bound || p.y < -bound || p.y > bound || p.z < -bound || p.z > bound) return false;
        position_=p; return true;
    }
    bool rotate(unsigned quarterTurns) { if(locked_) return false; rotation_=quarterTurns%4; return true; }
    void lock(bool on) { locked_=on; }
    bool locked() const { return locked_; }
    Position position() const { return position_; }
    unsigned rotation() const { return rotation_; }
    // Input is local non-negative block coordinates. Rotation is around the
    // origin, not the structure centre. Negative output coordinates are valid.
    // int32 inputs keep all additions representable with bounded world anchors.
    Position world(std::int32_t x,std::int32_t y,std::int32_t z) const {
        auto ix=std::int64_t(x), iz=std::int64_t(z);
        switch(rotation_) {
        case 1: return {position_.x-iz,position_.y+y,position_.z+ix};
        case 2: return {position_.x-ix,position_.y+y,position_.z-iz};
        case 3: return {position_.x+iz,position_.y+y,position_.z-ix};
        default:return {position_.x+ix,position_.y+y,position_.z+iz};
        }
    }
};
}
