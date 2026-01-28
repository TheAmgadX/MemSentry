cmake_minimum_required(VERSION 3.13)
project(MemSentry VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================
#  Output Configuration
# ==========================================
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ==========================================
#  Define the Remote Control (Must be near top)
# ==========================================
# This creates the switch. Default is ON.
option(MEM_SENTRY_ENABLE "Enable memory tracking features" ON)

# ==========================================
#  Build the Library (MemSentry)
# ==========================================
add_library(
    MemSentry STATIC
    src/mem_sentry.cc
    src/heap.cc
    src/console_reporter.cc
)

target_include_directories(MemSentry PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
)

# APPLY THE SWITCH:
if(MEM_SENTRY_ENABLE)
    message(STATUS "MemSentry Tracking: ON")
    target_compile_definitions(MemSentry PUBLIC MEM_SENTRY_ENABLE=1)
else()
    message(STATUS "MemSentry Tracking: OFF")
    target_compile_definitions(MemSentry PUBLIC MEM_SENTRY_ENABLE=0)
endif()

# ==========================================
#  Build the Example App (Client)
# ==========================================
add_executable(application
    examples/main.cc
)

target_link_libraries(
    application
    PRIVATE MemSentry 
)

# Add tests
add_subdirectory(tests)
