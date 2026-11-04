# ---------------------------------------------------------------------------
# Neyrx Core
# ---------------------------------------------------------------------------

add_library(
    NeyrxCore
    STATIC

    "Core/Private/Logger/Logger.cpp"
    "Core/Private/Logger/LogDataStructures.cpp"
    "Core/Private/Time/NX_Time.cpp"
)

target_include_directories(
    NeyrxCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Public
)

target_include_directories(
    NeyrxCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Time/Public
)

target_compile_definitions(
    NeyrxCore
    PUBLIC
        $<$<CONFIG:Debug>:NEYRX_ENABLE_LOGGING>
        $<$<CONFIG:Debug>:NEYRX_ENABLE_DEVELOPMENT>

        $<$<CONFIG:Development>:NEYRX_ENABLE_LOGGING>
        $<$<CONFIG:Development>:NEYRX_ENABLE_DEVELOPMENT>
)