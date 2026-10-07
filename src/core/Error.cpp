#include "opticalnet/core/Error.hpp"

namespace opticalnet {

std::string_view toString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidArgument: return "InvalidArgument";
        case ErrorCode::DuplicateId: return "DuplicateId";
        case ErrorCode::NotFound: return "NotFound";
        case ErrorCode::ConstraintViolation: return "ConstraintViolation";
        case ErrorCode::ParseError: return "ParseError";
        case ErrorCode::NoRoute: return "NoRoute";
        case ErrorCode::NoFeasibleRoute: return "NoFeasibleRoute";
        case ErrorCode::InsufficientCapacity: return "InsufficientCapacity";
        case ErrorCode::InconsistentState: return "InconsistentState";
        case ErrorCode::InternalError: return "InternalError";
    }
    return "Unknown";
}

std::string Error::describe() const {
    return std::string(toString(code)) + ": " + message;
}

}  // namespace opticalnet
