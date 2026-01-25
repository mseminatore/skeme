cmake_minimum_required(VERSION 3.10)

# set the project name
project(skeme VERSION 1.0 LANGUAGES C)

# Print the source directory
message(STATUS "Project source directory: ${PROJECT_SOURCE_DIR}")

# testing
enable_testing()
add_test(NAME skeme_test COMMAND skeme ${PROJECT_SOURCE_DIR}/test.scm WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# add the includes
include_directories(${PROJECT_SOURCE_DIR})

# set the C standard
# set(CMAKE_C_STANDARD 99)
# set(CMAKE_C_STANDARD_REQUIRED True)

# build hash table library
#add_subdirectory(./hash ./lib)

# add the executable
add_executable(skeme main.c parser.c lexer.c vm.c builtins.c io.c)
add_subdirectory(${PROJECT_SOURCE_DIR}/hash)

target_compile_definitions(skeme PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(skeme PRIVATE ht)
