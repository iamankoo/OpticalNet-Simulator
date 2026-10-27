#include "opticalnet/validation/ValidationResult.hpp"

#include <algorithm>

namespace opticalnet {

bool ValidationResult::failed(const std::string& check) const {
    return std::ranges::any_of(failures_, [&check](const ValidationFailure& f) { return f.check == check; });
}

std::string ValidationResult::describe(std::size_t maxFailures) const {
    if (failures_.empty()) return {};
    std::string text = std::to_string(failures_.size()) + " validation failure(s):";
    for (std::size_t i = 0; i < failures_.size() && i < maxFailures; ++i) {
        text += "\n  [" + failures_[i].check + "] " + failures_[i].message;
    }
    if (failures_.size() > maxFailures) text += "\n  ... and " + std::to_string(failures_.size() - maxFailures) + " more";
    return text;
}

}  // namespace opticalnet
