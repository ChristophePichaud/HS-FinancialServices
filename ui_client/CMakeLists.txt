# ui_client/CMakeLists.txt
# Builds the HS Financial Services MFC test client (Windows only).
#
# The application connects to the hs_server via TCP and lets you interactively
# test every exposed banking service through a graphical dialog.
#
# Build prerequisites (Windows):
#   - Visual Studio 2019 or 2022 with the "MFC and ATL support" workload
#   - CMake ≥ 3.16
#
# Example (from the repository root, Developer Command Prompt):
#   cmake -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release --target hs_ui_client

cmake_minimum_required(VERSION 3.16)

# Guard: MFC is Windows-only.
if(NOT WIN32)
    message(STATUS "[ui_client] MFC is Windows-only – skipping hs_ui_client.")
    return()
endif()

# ── MFC linkage ──────────────────────────────────────────────────────────────
# 1 = Static MFC (no MFC DLL required on target machine)
# 2 = Dynamic MFC (requires mfc140u.dll / mfc140.dll on target machine)
set(CMAKE_MFC_FLAG 2)

# ── Sources ──────────────────────────────────────────────────────────────────
set(HS_UI_SOURCES
    src/stdafx.cpp
    src/HSClientApp.cpp
    src/MainDlg.cpp
    res/HSClientApp.rc
)

set(HS_UI_HEADERS
    src/stdafx.h
    src/HSClientApp.h
    src/MainDlg.h
    res/resource.h
)

# ── Target ───────────────────────────────────────────────────────────────────
add_executable(hs_ui_client WIN32
    ${HS_UI_SOURCES}
    ${HS_UI_HEADERS}
)

# ── Include paths ────────────────────────────────────────────────────────────
target_include_directories(hs_ui_client PRIVATE
    src
    res
    # The shared protocol header lives in the server component.
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/include
)

# ── Compile definitions ──────────────────────────────────────────────────────
target_compile_definitions(hs_ui_client PRIVATE
    _AFXDLL          # Use MFC as a shared DLL (matches CMAKE_MFC_FLAG=2)
    _UNICODE
    UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(hs_ui_client PRIVATE ws2_32)

# ── Precompiled header ────────────────────────────────────────────────────────
if(MSVC)
    set_source_files_properties(src/stdafx.cpp PROPERTIES
        COMPILE_FLAGS "/Ycsrc/stdafx.h"
    )
    target_compile_options(hs_ui_client PRIVATE
        /Yusrc/stdafx.h   # Use precompiled header for all other TUs
    )
    # The resource file must not use the precompiled header.
    set_source_files_properties(res/HSClientApp.rc PROPERTIES
        COMPILE_FLAGS ""
        SKIP_PRECOMPILE_HEADERS ON
    )
endif()

# ── MSVC-specific flags ───────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(hs_ui_client PRIVATE
        /W3       # Warning level 3
        /EHsc     # C++ exception handling
        /permissive-
    )
endif()
