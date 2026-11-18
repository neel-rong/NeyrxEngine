# ---------------------------------------------------------------------------
# Logger Tests
# ---------------------------------------------------------------------------

add_executable(
    LoggerBasicTest
    LoggerBasicTest.cpp
)

target_link_libraries(
    LoggerBasicTest
    PRIVATE
        NeyrxCore
)

add_test(
    NAME LoggerBasicTest
    COMMAND LoggerBasicTest
)

target_compile_definitions(LoggerBasicTest PRIVATE
    PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)