# ============================================================================
# LiveMod test suite
# ============================================================================

include(FetchContent)
include(CTest)

# ---------------------------------------------------------------------------
# Helper to register a GoogleTest target with CTest
# ---------------------------------------------------------------------------
function(livemod_add_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/common/include
    )
    if(LIVEMOD_ENABLE_SANITIZER)
        target_compile_options(${name} PRIVATE -fsanitize=address,undefined)
        target_link_options(${name} PRIVATE      -fsanitize=address,undefined)
    endif()
    target_link_libraries(${name} PRIVATE GTest::gtest_main)
    gtest_discover_tests(${name})
endfunction()

# ---------------------------------------------------------------------------
# GoogleTest
# ---------------------------------------------------------------------------
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()

add_subdirectory(common)
