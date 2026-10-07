#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include <initializer_list>

#include "opticalnet/core/Network.hpp"
#include "opticalnet/topology/Topology.hpp"

namespace opticalnet::testing {

// Unwrap a Result that the test expects to succeed.
template <class T>
T must(Result<T> result) {
    EXPECT_TRUE(result.ok()) << (result.ok() ? "" : result.error().describe());
    return std::move(result).value();
}

inline Node makeNode(std::uint32_t id, std::string name = "n") { return must(Node::create(NodeId{id}, name + std::to_string(id))); }

inline FiberLink makeLink(std::uint32_t id, std::uint32_t from, std::uint32_t to, double km = 100.0,
                          std::uint32_t channels = 40) {
    return must(FiberLink::create(LinkId{id}, "l" + std::to_string(id), NodeId{from}, NodeId{to}, km, 0.2, channels));
}

inline Transceiver makeTransceiver(std::uint32_t id, double rate = 100.0, double reach = 1000.0) {
    return must(Transceiver::create(TransceiverId{id}, "t" + std::to_string(id), rate, reach));
}

inline SwitchingElement makeSwitch(std::uint32_t id, std::uint32_t ports = 8) {
    return must(SwitchingElement::create(SwitchingElementId{id}, "s" + std::to_string(id), SwitchingType::Roadm, ports));
}

inline FiberLink makeDirectedLink(std::uint32_t id, std::uint32_t from, std::uint32_t to) {
    return must(FiberLink::create(LinkId{id}, "d" + std::to_string(id), NodeId{from}, NodeId{to}, 100.0, 0.2, 40,
                                  LinkDirection::Directed));
}

// Topology with the given node ids (named "n<id>") and links.
inline Topology makeTopology(std::initializer_list<std::uint32_t> nodes, std::initializer_list<FiberLink> links) {
    Topology topology;
    for (const auto id : nodes) EXPECT_TRUE(topology.addNode(makeNode(id)).ok());
    for (const auto& link : links) EXPECT_TRUE(topology.addLink(link).ok());
    return topology;
}

template <class R>
::testing::AssertionResult succeeded(const R& result) {
    if (result.ok()) return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << "expected success, got " << result.error().describe();
}

template <class R>
::testing::AssertionResult failedWith(const R& result, ErrorCode expected) {
    if (result.ok()) return ::testing::AssertionFailure() << "expected " << toString(expected) << ", got success";
    if (result.error().code != expected)
        return ::testing::AssertionFailure() << "expected " << toString(expected) << ", got " << result.error().describe();
    return ::testing::AssertionSuccess();
}

// Streamable like any gtest assertion: ASSERT_ERROR(x, code) << "context";
#define ASSERT_OK(expr) ASSERT_TRUE(::opticalnet::testing::succeeded(expr))
#define ASSERT_ERROR(expr, expectedCode) ASSERT_TRUE(::opticalnet::testing::failedWith((expr), (expectedCode)))

}  // namespace opticalnet::testing
