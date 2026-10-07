#pragma once

// Internal helpers shared by the core implementation files (not a public header).

#include <cmath>
#include <string>
#include <string_view>

#include "opticalnet/core/Error.hpp"

namespace opticalnet::detail {

inline Error invalid(std::string message) {
    return Error{ErrorCode::InvalidArgument, std::move(message)};
}

inline bool isPositiveFinite(double v) noexcept { return std::isfinite(v) && v > 0.0; }
inline bool isNonNegativeFinite(double v) noexcept { return std::isfinite(v) && v >= 0.0; }

inline bool isBlank(std::string_view s) noexcept {
    return s.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

}  // namespace opticalnet::detail
