#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace opticalnet {

// The outcome of an independent check on OpticalNet's behaviour: either it passed, or it lists what is wrong,
// each failure naming the check and giving enough context (ids, expected vs. actual values) to reproduce it.
//
// This is project-specific validation, not a replacement for GoogleTest: the checkers in this layer return
// ValidationResults, and the tests turn them into ordinary GoogleTest assertions.
struct ValidationFailure {
    std::string check;    // short name of the violated invariant, e.g. "resources.link_within_capacity"
    std::string message;  // what was found, with ids and values
};

class ValidationResult {
public:
    void fail(std::string check, std::string message) { failures_.push_back({std::move(check), std::move(message)}); }
    void merge(const ValidationResult& other) { failures_.insert(failures_.end(), other.failures_.begin(), other.failures_.end()); }

    [[nodiscard]] bool passed() const noexcept { return failures_.empty(); }
    [[nodiscard]] const std::vector<ValidationFailure>& failures() const noexcept { return failures_; }
    [[nodiscard]] bool failed(const std::string& check) const;  // did this particular check fail?

    // Human-readable report of (at most) the first `maxFailures` failures; empty string when passed.
    [[nodiscard]] std::string describe(std::size_t maxFailures = 8) const;

private:
    std::vector<ValidationFailure> failures_;
};

}  // namespace opticalnet
