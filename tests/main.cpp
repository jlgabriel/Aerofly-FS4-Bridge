///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @file main.cpp
 * @brief Main entry point for Catch2 test suite
 *
 * This file provides the entry point for all Aerofly FS4 Bridge tests.
 * Catch2 will automatically discover and run all tests defined in the test files.
 */
///////////////////////////////////////////////////////////////////////////////////////////////////

// This tells Catch2 to provide a main() - only do this in one cpp file
#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

// Initialize logging for tests
#include "logging/logger.h"

// Custom test listener for global setup/teardown
class TestSetupListener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(Catch::TestRunInfo const&) override {
        // Initialize logging system for tests
        AeroflyBridge::Logger::Initialize();
    }

    void testRunEnded(Catch::TestRunStats const&) override {
        // Shutdown logging system after all tests
        AeroflyBridge::Logger::Shutdown();
    }
};

CATCH_REGISTER_LISTENER(TestSetupListener)
