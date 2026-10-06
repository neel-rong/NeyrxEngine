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