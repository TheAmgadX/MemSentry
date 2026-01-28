add_executable(test_buffer
    test_buffer.cc
)

target_link_libraries(test_buffer
    PRIVATE MemSentry
)

target_include_directories(test_buffer PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

add_executable(test_ringpool
    test_ringpool.cc
)

target_link_libraries(test_ringpool
    PRIVATE MemSentry
)

target_include_directories(test_ringpool PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
