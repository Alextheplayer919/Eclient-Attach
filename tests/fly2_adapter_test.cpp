#include <cassert>
namespace ImGui {
bool Checkbox(const char*, bool*) { return false; }
void TextWrapped(const char*, ...) {}
}
bool panic = false, inMenu = false, forceCreativeFly = false, creativeFly = false;
struct LayeredAbilities {
    bool flags[11]{}; int writes = 0;
    void setAbility(int index, bool value) { flags[index] = value; ++writes; }
};
struct Abilities {
    LayeredAbilities layer;
    LayeredAbilities* getLayer(int n) { assert(n == 2); return &layer; }
};
struct LocalPlayer {
    Abilities abilities; bool survival = true;
    Abilities* getAbilities() { return &abilities; }
    bool isAttackableGamemode() { return survival; }
};
#include "../apollon-adapter/fly/fly2_adapter.h"
int main() {
    using namespace eclient_fly2_adapter;
    LocalPlayer p, other;
    draw(); // Exercise UI entry without modifying its desired state.
    tick(&p, true); assert(p.abilities.layer.writes == 0);
    enabled.store(true); tick(&p, true);
    assert(p.abilities.layer.flags[9] && p.abilities.layer.flags[10]);
    int n = p.abilities.layer.writes;
    tick(&p, true); assert(p.abilities.layer.writes == n + 2); // continuous, independent of menu
    enabled.store(false); tick(&p, true);
    assert(!p.abilities.layer.flags[9] && !p.abilities.layer.flags[10]);
    n = p.abilities.layer.writes; tick(&p, true); assert(p.abilities.layer.writes == n);
    enabled.store(true); tick(&p, true); forceCreativeFly = true; creativeFly = true;
    enabled.store(false); tick(&p, true);
    assert(p.abilities.layer.flags[9] && p.abilities.layer.flags[10]);
    forceCreativeFly = creativeFly = false;
    enabled.store(true); tick(&p, true); panic = true; tick(&p, true);
    assert(!p.abilities.layer.flags[9] && !p.abilities.layer.flags[10]);
    panic = false; tick(&p, true); tick(&p, false);
    assert(!p.abilities.layer.flags[9] && !p.abilities.layer.flags[10]);
    tick(&p, true); n = p.abilities.layer.writes; tick(&other, true);
    assert(p.abilities.layer.writes == n); // never dereference old player
    other.survival = false; enabled.store(false); tick(&other, true);
    assert(other.abilities.layer.flags[9] && other.abilities.layer.flags[10]);
}
