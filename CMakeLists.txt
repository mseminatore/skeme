cmake_minimum_required(VERSION 3.10)

# set the project name
project(skeme VERSION 1.0 LANGUAGES C)

# add the includes
include_directories(${PROJECT_SOURCE_DIR})

# add the executable
add_executable(skeme main.c parser.c)
target_compile_definitions(skeme PRIVATE _CRT_SECURE_NO_WARNINGS)
