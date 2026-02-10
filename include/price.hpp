#pragma once
#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>


using Price = int64_t;
constexpr int64_t PRICE_SCALE = 10000;

inline Price to_price(double value)
{
    return static_cast<Price>(std::round(value * PRICE_SCALE));
}

inline double to_double(Price value)
{
    return static_cast<double>(value) / PRICE_SCALE;
}

inline std::string price_to_string(Price value)
{
    int64_t dollars = value / PRICE_SCALE;
    int64_t cents = std::abs(value % PRICE_SCALE);

    std::stringstream ss;
    ss << dollars << "." << std::setfill('0') << std::setw(4) << cents;
    return ss.str();
}

