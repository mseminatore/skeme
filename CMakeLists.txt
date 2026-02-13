cmake_minimum_required(VERSION 3.10)

# set the project name
project(skeme VERSION 1.0 LANGUAGES C)

# Print the source directory
message(STATUS "Project source directory: ${PROJECT_SOURCE_DIR}")

# testing
enable_testing()
add_test(NAME skeme_test COMMAND skeme -v test.scm WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME unit_tests COMMAND skeme_tests)

# add the includes
include_directories(${PROJECT_SOURCE_DIR})

# set the C standard
# set(CMAKE_C_STANDARD 99)
# set(CMAKE_C_STANDARD_REQUIRED True)

# build hash table library
#add_subdirectory(./hash ./lib)

# add the executable
add_executable(skeme main.c parser.c lexer.c vm.c builtins.c io.c platform.c error.c sk.c)
add_subdirectory(${PROJECT_SOURCE_DIR}/hash)
set_property(TARGET skeme PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(skeme PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(skeme PRIVATE ht)
if(NOT MSVC)
    target_link_libraries(skeme PRIVATE m)
endif()

# Unit tests executable
add_executable(skeme_tests
    tests/test_main.c
    tests/test_vm.c
    tests/test_gc.c
    tests/test_lexer.c
    tests/test_parser.c
    tests/test_builtins.c
    tests/test_helpers.c
    tests/test_error.c
    parser.c
    lexer.c
    vm.c
    builtins.c
    io.c
    platform.c
    error.c
    sk.c
)
target_compile_definitions(skeme_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(skeme_tests PRIVATE ht testy)
if(NOT MSVC)
    target_link_libraries(skeme_tests PRIVATE m)
endif()

# Add coverage support for Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(skeme_tests PRIVATE --coverage)
    target_link_options(skeme_tests PRIVATE --coverage)
endif()
