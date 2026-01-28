add_executable(mem_sentry_tests
    test_runner.cc
)

target_link_libraries(mem_sentry_tests
    PRIVATE MemSentry
)

target_include_directories(mem_sentry_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# Add mem_pools unit tests
add_subdirectory(mem_pools)

# set(MEM_SENTRY_ENABLE ON CACHE BOOL "" FORCE)
