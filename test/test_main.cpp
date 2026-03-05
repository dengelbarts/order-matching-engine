#include <gtest/gtest.h>

// Simple sanity test to verify Google Test is working
TEST(SanityTest, BuildSystemWorks) {
    EXPECT_EQ(1 + 1, 2);
    EXPECT_TRUE(true);
}

TEST(SanityTest, BasicArithmetic) {
    EXPECT_EQ(10 - 5, 5);
    EXPECT_NE(10, 5);
    EXPECT_GT(10, 5);
    EXPECT_LT(5, 10);
}

TEST(SanityTest, StringOperations) {
    std::string hello = "Hello";
    std::string world = "World";

    EXPECT_EQ(hello.length(), 5);
    EXPECT_NE(hello, world);

    std::string combined = hello + " " + world;
    EXPECT_EQ(combined, "Hello World");
}

// Test that demonstrates C++17 features compile
TEST(Cpp17Test, StructuredBindings) {
    std::pair<int, std::string> p = {42, "answer"};
    auto [num, str] = p;

    EXPECT_EQ(num, 42);
    EXPECT_EQ(str, "answer");
}

TEST(Cpp17Test, InitStatementInIf) {
    if (auto value = 42; value > 0) {
        EXPECT_GT(value, 0);
    } else {
        FAIL() << "This should not execute";
    }
}