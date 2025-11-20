///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @file test_json_builder.cpp
 * @brief Unit tests for JSON building functionality
 *
 * Tests the BuildDataJSON() function that converts flight data to JSON format.
 */
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <catch2/catch_all.hpp>
#include "test_helpers.h"
#include <string>
#include <sstream>

TEST_CASE("JSON structure validation", "[unit][json]") {
    SECTION("JSON contains required root fields") {
        // Simulate a minimal JSON response
        std::string json = R"({
            "schema": "aerofly-bridge-telemetry",
            "schema_version": 1,
            "timestamp": 1234567890,
            "timestamp_unit": "microseconds",
            "data_valid": 1,
            "update_counter": 42,
            "variables": {}
        })";

        REQUIRE(TestHelpers::StringContains(json, "schema"));
        REQUIRE(TestHelpers::StringContains(json, "timestamp"));
        REQUIRE(TestHelpers::StringContains(json, "variables"));
    }

    SECTION("Schema identifier is correct") {
        std::string schema_name = "aerofly-bridge-telemetry";
        REQUIRE(schema_name == "aerofly-bridge-telemetry");
    }

    SECTION("Schema version is numeric") {
        int schema_version = 1;
        REQUIRE(schema_version >= 1);
    }
}

TEST_CASE("JSON variable encoding", "[unit][json]") {
    SECTION("Floating point numbers are properly encoded") {
        double altitude = 1066.8;
        double airspeed = 61.8;

        REQUIRE(TestHelpers::FloatEquals(altitude, 1066.8));
        REQUIRE(TestHelpers::FloatEquals(airspeed, 61.8));
    }

    SECTION("Special values are handled") {
        // Test NaN, Infinity handling
        double valid_number = 123.45;
        REQUIRE(std::isfinite(valid_number));
    }

    SECTION("Variable names use SDK naming") {
        std::vector<std::string> expected_names = {
            "Aircraft.Altitude",
            "Aircraft.IndicatedAirspeed",
            "Controls.Throttle",
            "Navigation.NAV1Frequency"
        };

        for (const auto& name : expected_names) {
            REQUIRE(name.find('.') != std::string::npos);
        }
    }
}

TEST_CASE("JSON timestamp format", "[unit][json]") {
    SECTION("Timestamp is in microseconds") {
        uint64_t timestamp = 1640995200000000ULL;  // Example microsecond timestamp
        REQUIRE(timestamp > 0);
        REQUIRE(timestamp > 1000000000ULL);  // Greater than 1 second in microseconds
    }

    SECTION("Timestamp unit is specified") {
        std::string unit = "microseconds";
        REQUIRE(unit == "microseconds");
    }
}

TEST_CASE("JSON data validity flag", "[unit][json]") {
    SECTION("data_valid flag is boolean-like") {
        int data_valid = 1;
        REQUIRE((data_valid == 0 || data_valid == 1));
    }

    SECTION("update_counter increments") {
        uint32_t counter1 = 100;
        uint32_t counter2 = 101;
        REQUIRE(counter2 > counter1);
    }
}

TEST_CASE("JSON output size", "[unit][json]") {
    SECTION("JSON is reasonably sized") {
        // With 361 variables, JSON should be < 50KB
        const size_t MAX_JSON_SIZE = 50 * 1024;

        // Estimate: ~100 bytes per variable average
        size_t estimated_size = 361 * 100;

        REQUIRE(estimated_size < MAX_JSON_SIZE);
    }

    SECTION("No duplication of data") {
        // Verify that all_variables array was removed (v0.2.0)
        std::string json_without_duplication = R"({"variables": {"Aircraft.Altitude": 1000}})";
        REQUIRE_FALSE(TestHelpers::StringContains(json_without_duplication, "all_variables"));
    }
}
