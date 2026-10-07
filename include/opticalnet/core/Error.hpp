#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace opticalnet {

enum class ErrorCode {
    InvalidArgument,      // a value violates a domain invariant
    DuplicateId,          // an object with this id is already registered
    NotFound,             // referenced object does not exist
    ConstraintViolation,  // operation would break a relationship between objects
    ParseError,           // configuration text is malformed or violates the schema
    NoRoute,              // destination is unreachable in the topology
    NoFeasibleRoute,      // a route exists, but none satisfies the routing constraints
    InsufficientCapacity, // a route exists, but a link on it lacks the free capacity requested
    InconsistentState,    // resource bookkeeping contradicts the requested operation (e.g. over-release)
};

[[nodiscard]] std::string_view toString(ErrorCode code) noexcept;

struct Error {
    ErrorCode code;
    std::string message;

    [[nodiscard]] std::string describe() const;
};

// Thrown only when a Result is accessed in the wrong state (a programming error).
class BadResultAccess : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

// Expected failures are returned as values; exceptions are for programming errors.
template <class T>
class [[nodiscard]] Result {
public:
    Result(T value) : data_(std::move(value)) {}      // implicit by design
    Result(Error error) : data_(std::move(error)) {}  // implicit by design

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(data_); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() & { return get(); }
    [[nodiscard]] const T& value() const& { return get(); }
    [[nodiscard]] T&& value() && { return std::move(get()); }

    [[nodiscard]] const Error& error() const {
        if (ok()) throw BadResultAccess("Result::error() called on a success value");
        return std::get<Error>(data_);
    }

private:
    T& get() {
        if (!ok()) throw BadResultAccess("Result::value() called on an error: " + std::get<Error>(data_).describe());
        return std::get<T>(data_);
    }
    const T& get() const {
        if (!ok()) throw BadResultAccess("Result::value() called on an error: " + std::get<Error>(data_).describe());
        return std::get<T>(data_);
    }

    std::variant<T, Error> data_;
};

template <>
class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)) {}  // implicit by design

    [[nodiscard]] bool ok() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const Error& error() const {
        if (ok()) throw BadResultAccess("Result::error() called on a success value");
        return *error_;
    }

private:
    std::optional<Error> error_;
};

}  // namespace opticalnet
