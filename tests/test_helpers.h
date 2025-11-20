///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @file test_helpers.h
 * @brief Common test utilities and helpers for Aerofly FS4 Bridge tests
 */
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <catch2/catch_all.hpp>
#include <string>
#include <chrono>
#include <thread>

namespace TestHelpers {

/**
 * @brief Sleep for specified milliseconds (useful for async tests)
 */
inline void SleepMs(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

/**
 * @brief Check if a string contains a substring
 */
inline bool StringContains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

/**
 * @brief Compare floating point numbers with tolerance
 */
inline bool FloatEquals(double a, double b, double epsilon = 0.0001) {
    return std::abs(a - b) < epsilon;
}

/**
 * @brief Generate a random port number for testing (avoid conflicts)
 */
inline int GetRandomTestPort() {
    return 50000 + (rand() % 10000);  // Range: 50000-59999
}

/**
 * @brief Mock Aerofly SDK data structure for testing
 */
struct MockAeroflyData {
    double altitude = 1000.0;
    double airspeed = 120.0;
    double heading = 270.0;
    double latitude = 47.4979;
    double longitude = -122.2079;

    void SetDefaults() {
        altitude = 1000.0;
        airspeed = 120.0;
        heading = 270.0;
        latitude = 47.4979;
        longitude = -122.2079;
    }
};

} // namespace TestHelpers
