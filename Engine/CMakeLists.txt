# ---------------------------------------------------------------------------
# Neyrx Core
# ---------------------------------------------------------------------------

add_library(
    NeyrxCore
    STATIC

    # Logger
    Core/Private/Logger/Logger.cpp
    Core/Private/Logger/LogDataStructures.cpp

    # Time
    Core/Private/Time/NX_Time.cpp

    # Public Headers
    Core/Public/Neyrx/Core/Time/NX_HighResTimer.h
)

target_include_directories(
    NeyrxCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Public
)

target_compile_definitions(
    NeyrxCore
    PUBLIC
        $<$<CONFIG:Debug>:NEYRX_ENABLE_LOGGING>
        $<$<CONFIG:Debug>:NEYRX_ENABLE_DEVELOPMENT>

        $<$<CONFIG:Development>:NEYRX_ENABLE_LOGGING>
        $<$<CONFIG:Development>:NEYRX_ENABLE_DEVELOPMENT>
)

if(MSVC)
    target_compile_options(
        NeyrxCore
        PUBLIC
            /Zc:preprocessor
    )
endif()