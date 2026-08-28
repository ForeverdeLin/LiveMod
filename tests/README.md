# tests/

This directory hosts the LiveMod unit / integration test suite.

## Layout

```
tests/
├── CMakeLists.txt          # CTest + GoogleTest integration
├── README.md               # this file
├── common/                 # tests for common/include/ (no dependencies)
│   ├── CMakeLists.txt
│   ├── test_protocol.cpp   # CC_MsgHeader, CC_NetConnectInfo, CC_AVStream
│   └── test_yuv.cpp        # YUVData_Frame layout / packing
└── README.md
```

## Running

```bash
# Configure (from project root)
cmake -B build -DLIVEMOD_BUILD_TESTS=ON
cmake --build build -j$(nproc)

# Run everything
ctest --test-dir build --output-on-failure

# Run only common/ tests
ctest --test-dir build -R common --output-on-failure
```

## Adding a new test

1. Add a new `test_<name>.cpp` under the appropriate module subdirectory.
2. Register it in the module's `CMakeLists.txt` with `livemod_add_test(...)`.
3. Use GoogleTest macros (`TEST`, `EXPECT_EQ`, `EXPECT_TRUE`, ...).

## Notes

- GoogleTest is fetched via `FetchContent` (declared in `tests/CMakeLists.txt`).
  No manual install required.
- Tests run without any GPU / network / audio device; pure logic only.
- Sanitizer builds (`-DLIVEMOD_ENABLE_SANITIZER=ON`) reuse these same binaries.
