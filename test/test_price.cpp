#include "../include/price.hpp"

#include <gtest/gtest.h>

TEST(PriceTest, BasicConversion) {
    EXPECT_EQ(to_price(10.50), 105000);
    EXPECT_EQ(to_price(0.0001), 1);
    EXPECT_EQ(to_price(100.0), 1000000);
}

TEST(PriceTest, RoundTrip) {
    double original = 10.50;
    Price price = to_price(original);
    double result = to_double(price);
    EXPECT_DOUBLE_EQ(original, result);
}

TEST(PriceTest, EdgeCases) {
    EXPECT_EQ(to_price(0.0), 0);
    EXPECT_EQ(to_price(-10.50), -105000);
    EXPECT_EQ(to_price(0.0001), 1);
    EXPECT_EQ(to_price(1000000.0), 10000000000);
}

TEST(PriceTest, NoPrecisionLoss) {
    std::vector<double> test_values = {10.5000, 10.5001, 10.9999, 0.0001, 0.9999};
    for (double val : test_values) {
        Price p = to_price(val);
        double result = to_double(p);
        EXPECT_NEAR(val, result, 0.00005);
    }
}

TEST(PriceTest, StringConversion) {
    EXPECT_EQ(price_to_string(105000), "10.5000");
    EXPECT_EQ(price_to_string(1), "0.0001");
    EXPECT_EQ(price_to_string(0), "0.0000");
    EXPECT_EQ(price_to_string(-105000), "-10.5000");
}

TEST(PriceTest, Rounding) {
    EXPECT_EQ(to_price(10.50005), 105001);
    EXPECT_EQ(to_price(10.50004), 105000);
}

TEST(PriceTest, MaxInt64) {
    Price max_safe = INT64_MAX / 2;
    double max_double = to_double(max_safe);
    Price back = to_price(max_double);

    EXPECT_NEAR(max_safe, back, PRICE_SCALE);
}
