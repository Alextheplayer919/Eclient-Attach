#pragma once
// Counts are observations within a radius, not proof of placement or destruction.
namespace eclient::combat {
struct CrystalCountEvents {
    int previous = -1;
    void reset() { previous = -1; }
    bool update(int count) {
        if (count == previous) return false;
        previous = count;
        return true;
    }
};
}
