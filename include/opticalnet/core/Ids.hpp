#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ostream>

namespace opticalnet {

// Type-safe identifier. A NodeId cannot be passed where a LinkId is expected.
template <class Tag>
class StrongId {
public:
    using value_type = std::uint32_t;

    explicit constexpr StrongId(value_type value) noexcept : value_(value) {}

    [[nodiscard]] constexpr value_type value() const noexcept { return value_; }

    friend constexpr auto operator<=>(const StrongId&, const StrongId&) = default;

    friend std::ostream& operator<<(std::ostream& os, const StrongId& id) {
        return os << id.value_;
    }

private:
    value_type value_;
};

struct NodeTag {};
struct LinkTag {};
struct TransceiverTag {};
struct SwitchingElementTag {};
struct ConnectionTag {};

using NodeId = StrongId<NodeTag>;
using LinkId = StrongId<LinkTag>;
using TransceiverId = StrongId<TransceiverTag>;
using SwitchingElementId = StrongId<SwitchingElementTag>;
using ConnectionId = StrongId<ConnectionTag>;

}  // namespace opticalnet

template <class Tag>
struct std::hash<opticalnet::StrongId<Tag>> {
    std::size_t operator()(const opticalnet::StrongId<Tag>& id) const noexcept {
        return std::hash<typename opticalnet::StrongId<Tag>::value_type>{}(id.value());
    }
};
