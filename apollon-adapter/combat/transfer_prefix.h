#pragma once
#include <cstdint>

namespace eclient::combat {
// Only byte positions are observed; protocol names are hypotheses until
// correlated with a controlled on-device capture for this exact game build.
struct TransferPrefix { std::uint8_t bytes[13]{}; };
struct TransferFields { unsigned container, slot; };
inline TransferFields transferFields(const TransferPrefix& prefix) {
    return {prefix.bytes[0], prefix.bytes[12]};
}
}
