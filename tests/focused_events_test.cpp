#include "../apollon-adapter/combat/focused_events.h"
#include <cassert>
int main() {
    eclient::combat::CrystalCountEvents events;
    assert(events.update(0));
    assert(!events.update(0));
    assert(events.update(1));
    assert(!events.update(1));
    assert(events.update(0));
    events.reset();
    assert(events.update(0));
}
