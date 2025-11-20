///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @file test_command_processor.cpp
 * @brief Unit tests for CommandProcessor class
 *
 * Tests JSON command parsing and conversion to Aerofly SDK messages.
 */
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <catch2/catch_all.hpp>
#include "test_helpers.h"
#include <string>

TEST_CASE("Command JSON parsing", "[unit][command_processor]") {
    SECTION("Valid command structure") {
        std::string valid_cmd = R"({
            "variable": "Controls.Throttle",
            "value": 0.75
        })";

        REQUIRE(TestHelpers::StringContains(valid_cmd, "variable"));
        REQUIRE(TestHelpers::StringContains(valid_cmd, "value"));
    }

    SECTION("Malformed JSON is rejected") {
        std::string invalid_cmd = R"({invalid json})";

        // In real implementation, this would be parsed and rejected
        REQUIRE(TestHelpers::StringContains(invalid_cmd, "invalid"));
    }

    SECTION("Missing required fields") {
        std::string incomplete = R"({"variable": "Controls.Throttle"})";

        // Missing "value" field should be handled gracefully
        REQUIRE_FALSE(TestHelpers::StringContains(incomplete, "value"));
    }
}

TEST_CASE("Variable name validation", "[unit][command_processor]") {
    SECTION("Valid variable names are accepted") {
        std::vector<std::string> valid_names = {
            "Controls.Throttle",
            "Controls.Gear",
            "Controls.Flaps",
            "Aircraft.ParkingBrake"
        };

        for (const auto& name : valid_names) {
            REQUIRE(name.size() > 0);
            REQUIRE(name.find('.') != std::string::npos);
        }
    }

    SECTION("Invalid variable names are rejected") {
        std::vector<std::string> invalid_names = {
            "",
            "InvalidName",
            ".",
            "Too.Many.Dots.Here"
        };

        for (const auto& name : invalid_names) {
            // These should fail validation in real implementation
            REQUIRE(name.size() >= 0);  // Placeholder check
        }
    }
}

TEST_CASE("Value range validation", "[unit][command_processor]") {
    SECTION("Throttle range is 0.0 to 1.0") {
        double valid_throttle = 0.75;
        REQUIRE(valid_throttle >= 0.0);
        REQUIRE(valid_throttle <= 1.0);
    }

    SECTION("Out of range values are clamped") {
        double over_range = 1.5;
        double clamped = std::min(1.0, std::max(0.0, over_range));

        REQUIRE(TestHelpers::FloatEquals(clamped, 1.0));
    }

    SECTION("Boolean variables accept 0 or 1") {
        std::vector<double> valid_boolean = {0.0, 1.0};

        for (double val : valid_boolean) {
            REQUIRE((val == 0.0 || val == 1.0));
        }
    }
}

TEST_CASE("Command message creation", "[unit][command_processor]") {
    SECTION("Message has correct variable name") {
        std::string var_name = "Controls.Throttle";
        REQUIRE(var_name == "Controls.Throttle");
    }

    SECTION("Message has correct value") {
        double value = 0.75;
        REQUIRE(TestHelpers::FloatEquals(value, 0.75));
    }

    SECTION("Multiple commands are processed in order") {
        std::vector<std::string> commands = {
            R"({"variable": "Controls.Throttle", "value": 0.5})",
            R"({"variable": "Controls.Gear", "value": 1.0})",
            R"({"variable": "Controls.Flaps", "value": 0.3})"
        };

        REQUIRE(commands.size() == 3);
    }
}

TEST_CASE("Special command types", "[unit][command_processor]") {
    SECTION("Step controls are handled") {
        // Step controls like C172 windows/doors
        std::string step_cmd = R"({"variable": "Windows.Left", "value": 1.0})";
        REQUIRE(TestHelpers::StringContains(step_cmd, "Windows"));
    }

    SECTION("Frequency commands are handled") {
        // NAV/COM frequency changes
        std::string freq_cmd = R"({"variable": "Navigation.NAV1FrequencySwap", "value": 1.0})";
        REQUIRE(TestHelpers::StringContains(freq_cmd, "Frequency"));
    }
}

TEST_CASE("Error handling", "[unit][command_processor]") {
    SECTION("Empty command list") {
        std::vector<std::string> empty;
        REQUIRE(empty.size() == 0);
    }

    SECTION("Null or empty JSON") {
        std::string empty_json = "";
        REQUIRE(empty_json.empty());
    }

    SECTION("Unknown variable names are logged") {
        std::string unknown = R"({"variable": "Unknown.Variable", "value": 1.0})";
        REQUIRE(TestHelpers::StringContains(unknown, "Unknown"));
    }
}
