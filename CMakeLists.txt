cmake_minimum_required(VERSION 3.13)

if (NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()

# strongly encouraged to enable this globally to avoid conflicts between
# -Wpedantic being enabled and -std=c++20 and -std=gnu++20 for example
# when compiling with PCH enabled
set(CMAKE_CXX_EXTENSIONS OFF)

project(xoz
    VERSION 1.0
    HOMEPAGE_URL "https://github.com/eldipa/xoz"
    LANGUAGES CXX
    )

message(CMAKE_CXX_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}")

# Define targets: xoz lib and a few demo programs
add_library(xoz STATIC)
add_executable(allocdemo)
add_executable(tarlike)

add_dependencies(allocdemo xoz)
add_dependencies(tarlike xoz)

# add the source files that make the lib and demos
add_subdirectory(xoz/alloc)
add_subdirectory(xoz/blk)
add_subdirectory(xoz/dsc)
add_subdirectory(xoz/err)
add_subdirectory(xoz/ext)
add_subdirectory(xoz/io)
add_subdirectory(xoz/log)
add_subdirectory(xoz/mem)
add_subdirectory(xoz/repo)
add_subdirectory(xoz/segm)

add_subdirectory(demos)

# set compiler warnings to the targets
include(cmake/CompilerWarnings.cmake)
set_project_warnings(xoz ON FALSE)

set_project_warnings(allocdemo ON FALSE)
set_project_warnings(tarlike ON FALSE)

# Say where the headers will be found required by xoz library target
# Make this PUBLIC so any dependant linking xoz lib will inherit this
# location (so our dependants will not have to explicitly request this)
target_include_directories(xoz PUBLIC .)

# Link the xoz lib target to each demo targets
target_link_libraries(allocdemo xoz)
target_link_libraries(tarlike xoz)


# Testing section
# ---------------

# Let's use google test
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
# (borrowed from https://google.github.io/googletest/quickstart-cmake.html)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# target for testing
add_executable(runtests)
add_dependencies(runtests xoz)

# Say where to find the xoz headers
target_include_directories(runtests PUBLIC .)

# Add the source files to the runtests target
add_subdirectory(test)
add_subdirectory(test/alloc)
add_subdirectory(test/blk)
add_subdirectory(test/dsc)
add_subdirectory(test/ext)
add_subdirectory(test/io)
add_subdirectory(test/mem)
add_subdirectory(test/repo)
add_subdirectory(test/segm)

# Set the compiler warnings to the tests target (but be a little lesser strict)
set_project_warnings(runtests ON TRUE)

# Say where googletest headers can be found
target_include_directories(runtests
    PUBLIC
    ${gtest_SOURCE_DIR}/include
    ${gmock_SOURCE_DIR}/include
    )

target_link_libraries(runtests
    xoz
    GTest::gtest_main
)
