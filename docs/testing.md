# Testing Guide - Aerofly FS4 Bridge

## Overview

The Aerofly FS4 Bridge uses **Catch2 v3** for automated testing. Tests are organized into unit tests and integration tests.

## Test Organization

```
tests/
├── CMakeLists.txt               # Test build configuration
├── main.cpp                     # Test entry point
├── test_helpers.h               # Common test utilities
├── unit/                        # Unit tests (fast, isolated)
│   ├── test_variable_mapper.cpp
│   ├── test_json_builder.cpp
│   └── test_command_processor.cpp
└── integration/                 # Integration tests (slower, system-level)
    ├── test_shared_memory.cpp
    ├── test_tcp_server.cpp
    └── test_websocket_server.cpp
```

## Running Tests

### Build and Run All Tests

```powershell
# Using PowerShell script
.\scripts\build.ps1 -Test

# Using CMake directly
cmake --build build --config Debug --target AeroflyBridge_Tests
cd build\Debug
.\AeroflyBridge_Tests.exe
```

### Run Specific Tests

```bash
# Run only unit tests
.\AeroflyBridge_Tests.exe "[unit]"

# Run only integration tests
.\AeroflyBridge_Tests.exe "[integration]"

# Run specific test file
.\AeroflyBridge_Tests.exe "[json]"

# Run with verbose output
.\AeroflyBridge_Tests.exe --reporter console --success
```

### Using CTest

```bash
cd build
ctest --output-on-failure
ctest -R "unit"        # Run only unit tests
ctest -R "integration" # Run only integration tests
```

## Writing New Tests

### Unit Test Example

```cpp
#include <catch2/catch_all.hpp>
#include "test_helpers.h"

TEST_CASE("My feature works correctly", "[unit][myfeature]") {
    SECTION("Basic functionality") {
        int result = my_function(5);
        REQUIRE(result == 10);
    }

    SECTION("Edge cases") {
        REQUIRE_THROWS(my_function(-1));
    }
}
```

### Integration Test Example

```cpp
TEST_CASE("TCP server accepts connections", "[integration][tcp]") {
    TCPServerInterface server;
    REQUIRE(server.Start(12345, 12346));

    // Test connection logic
    TestHelpers::SleepMs(100);

    REQUIRE(server.GetClientCount() >= 0);
    server.Stop();
}
```

## Test Helpers

The `test_helpers.h` file provides common utilities:

```cpp
// Sleep for testing async operations
TestHelpers::SleepMs(100);

// String checking
REQUIRE(TestHelpers::StringContains(json, "schema"));

// Float comparison with epsilon
REQUIRE(TestHelpers::FloatEquals(3.14, 3.14001, 0.001));

// Random test port
int port = TestHelpers::GetRandomTestPort();
```

## Test Categories (Tags)

Tests are tagged for easy filtering:

- `[unit]` - Fast, isolated unit tests
- `[integration]` - System-level integration tests
- `[variable_mapper]` - Variable mapping tests
- `[json]` - JSON processing tests
- `[command_processor]` - Command processing tests
- `[shared_memory]` - Shared memory tests
- `[tcp]` - TCP server tests
- `[websocket]` - WebSocket server tests

## Continuous Integration

Tests run automatically on:
- Pull requests to main branch
- Pushes to main branch
- Manual workflow dispatch

See `.github/workflows/test.yml` for CI configuration.

## Test Coverage

To measure test coverage (requires additional tools):

```powershell
# Install OpenCppCoverage (Windows)
# Run tests with coverage
OpenCppCoverage --sources src -- .\build\Debug\AeroflyBridge_Tests.exe
```

## Troubleshooting

### Tests Fail to Build

1. Ensure Catch2 is downloaded:
   ```bash
   cmake -B build
   ```

2. Check that all test files are listed in `tests/CMakeLists.txt`

### Tests Hang or Timeout

- Integration tests may need longer timeouts
- Check for deadlocks in multi-threaded tests
- Verify ports are not already in use (12345, 12346, 8765)

### Port Already in Use

```cpp
// Use random ports for tests
int port = TestHelpers::GetRandomTestPort();
```

## Best Practices

1. **Keep unit tests fast** - Each test should complete in milliseconds
2. **Isolate tests** - Tests should not depend on each other
3. **Use descriptive names** - `TEST_CASE("JSON encoding handles special characters")`
4. **Test edge cases** - Empty strings, null values, boundary conditions
5. **Clean up resources** - Always stop servers, close connections
6. **Use SECTION for related tests** - Group related assertions

## Future Improvements

- [ ] Add performance benchmarks
- [ ] Increase test coverage to >80%
- [ ] Add property-based testing
- [ ] Add fuzz testing for protocol parsers
- [ ] Mock Aerofly SDK for isolated testing

## References

- [Catch2 Documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
- [CTest Documentation](https://cmake.org/cmake/help/latest/manual/ctest.1.html)
