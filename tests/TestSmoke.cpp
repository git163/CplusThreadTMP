// tests/TestSmoke.cpp — 验证 GTest 已正确接线的 sanity check。
// 有了第一个真实测试后即可删除此文件。

#include <gtest/gtest.h>

TEST(SmokeTest, TrivialAssertion) {
    EXPECT_EQ(1 + 1, 2);
}