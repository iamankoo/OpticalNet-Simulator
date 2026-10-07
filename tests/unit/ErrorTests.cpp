#include <gtest/gtest.h>

#include <string>

#include "opticalnet/core/Error.hpp"

using namespace opticalnet;

TEST(Result, SuccessHoldsValue) {
    Result<int> r{42};
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.value(), 42);
    EXPECT_THROW((void)r.error(), BadResultAccess);
}

TEST(Result, ErrorHoldsCodeAndMessage) {
    Result<int> r{Error{ErrorCode::NotFound, "missing"}};
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::NotFound);
    EXPECT_EQ(r.error().message, "missing");
    EXPECT_EQ(r.error().describe(), "NotFound: missing");
}

TEST(Result, AccessingValueOfErrorThrows) {
    Result<std::string> r{Error{ErrorCode::InvalidArgument, "bad"}};
    EXPECT_THROW((void)r.value(), BadResultAccess);
}

TEST(Result, MoveOutOfRvalueValue) {
    std::string s = Result<std::string>{std::string("hello")}.value();
    EXPECT_EQ(s, "hello");
}

TEST(Result, VoidSuccessAndError) {
    Result<void> ok;
    EXPECT_TRUE(ok.ok());
    EXPECT_THROW((void)ok.error(), BadResultAccess);

    Result<void> bad{Error{ErrorCode::DuplicateId, "dup"}};
    EXPECT_FALSE(bad.ok());
    EXPECT_EQ(bad.error().code, ErrorCode::DuplicateId);
}

TEST(ErrorCode, HasReadableNames) {
    EXPECT_EQ(toString(ErrorCode::InvalidArgument), "InvalidArgument");
    EXPECT_EQ(toString(ErrorCode::DuplicateId), "DuplicateId");
    EXPECT_EQ(toString(ErrorCode::NotFound), "NotFound");
    EXPECT_EQ(toString(ErrorCode::ConstraintViolation), "ConstraintViolation");
}
