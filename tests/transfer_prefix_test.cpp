#include "../apollon-adapter/combat/transfer_prefix.h"
#include <cassert>
int main() {
    eclient::combat::TransferPrefix prefix{};
    prefix.bytes[0] = 0x22;
    prefix.bytes[1] = 0xee; // ignored, not assumed to be initialized
    prefix.bytes[12] = 3;
    auto fields = eclient::combat::transferFields(prefix);
    assert(fields.container == 34 && fields.slot == 3);
}
