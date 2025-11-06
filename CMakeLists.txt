cmake_minimum_required(VERSION 3.16)

project(hydra_cpp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBYAML REQUIRED yaml-0.1)

add_library(hydra-cpp-lib
    src/config_node.cpp
    src/interpolation.cpp
    src/time_utils.cpp
    src/config_utils.cpp
    src/yaml_loader.cpp
    src/yaml_emitter.cpp
    src/overrides.cpp
    src/c_api.cpp
    src/c_api_utils.cpp
)

target_include_directories(hydra-cpp-lib
    PUBLIC
        include
    PRIVATE
        ${LIBYAML_INCLUDE_DIRS}
)

target_link_libraries(hydra-cpp-lib
    PUBLIC
        ${LIBYAML_LIBRARIES}
)

target_compile_options(hydra-cpp-lib
    PUBLIC
        ${LIBYAML_CFLAGS_OTHER}
)

add_executable(hydra-cpp
    src/main.cpp
)

target_link_libraries(hydra-cpp
    PRIVATE
        hydra-cpp-lib
)

add_executable(hydra-c-example
    examples/simple_c/main.c
)

target_link_libraries(hydra-c-example
    PRIVATE
        hydra-cpp-lib
)

target_include_directories(hydra-c-example
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

add_executable(hydra-cpp-example
    examples/simple_cpp/main.cpp
)

target_link_libraries(hydra-cpp-example
    PRIVATE
        hydra-cpp-lib
)

target_include_directories(hydra-cpp-example
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

enable_testing()
add_subdirectory(tests)
