cmake_minimum_required(VERSION 3.16)
project(HS-FinancialServices CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler hardening flags ────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -O2
    )
endif()

# ── Dependencies ─────────────────────────────────────────────────────────────
find_package(Boost 1.74 REQUIRED
    COMPONENTS system thread program_options)
find_package(Threads REQUIRED)

# ── Sub-projects ─────────────────────────────────────────────────────────────
add_subdirectory(server)
add_subdirectory(load_generator)
