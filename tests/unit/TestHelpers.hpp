#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "opticalnet/core/Network.hpp"

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
