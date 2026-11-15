#include "../apollon-adapter/fly/fly2_probe.h"
#include <cassert>
int main() {
    using namespace eclient::fly2::probe;
    int value = 17, copy = 0;
    assert(read(reinterpret_cast<std::uintptr_t>(&value), copy) && copy == 17);
    assert(!read(1, copy));
    assert(!read(static_cast<std::uintptr_t>(-4096), copy));
    Raw r;
    assert(!sample(0, r));
    assert(!r.snapshot.layoutVerified);
}
