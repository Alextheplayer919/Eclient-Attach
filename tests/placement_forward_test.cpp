#include "../apollon-adapter/combat/placement_forward.h"
#include <cassert>
using namespace eclient::combat;
static unsigned calls;
static std::uint64_t sample(std::uintptr_t a0,std::uintptr_t a1,std::uintptr_t a2,std::uint32_t a3,std::uintptr_t a4,std::uintptr_t a5,std::uint32_t a6) {
    ++calls;
    assert(a0==0x123456789aULL && a1==0x23456789abULL && a2==0x3456789abcULL);
    assert(a3==42 && a4==0x456789abcdULL && a5==0x56789abcdeULL && a6==1);
    return 0xf123456789abcdefULL;
}
int main() {
    const PlacementArgs args{0x123456789aULL,0x23456789abULL,0x3456789abcULL,42,0x456789abcdULL,0x56789abcdeULL,1};
    assert(forwardPlacement(sample,args)==0xf123456789abcdefULL && calls==1);
}
