# ---------------------------------------------------------------------------
# Sandbox
# ---------------------------------------------------------------------------

add_executable(
    NeyrxSandbox
    Source/main.cpp
)

target_link_libraries(
    NeyrxSandbox
    PRIVATE
        NeyrxCore
)