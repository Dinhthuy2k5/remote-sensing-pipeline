cmake_minimum_required(VERSION 3.20)
project(rs_pipeline VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GDAL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED libpqxx)

file(GLOB_RECURSE MAIN_SOURCES "src/*.cpp")

add_executable(rs_pipeline ${MAIN_SOURCES})

target_include_directories(rs_pipeline PRIVATE
    src/
    ${GDAL_INCLUDE_DIRS}
    ${PQXX_INCLUDE_DIRS}
)

target_compile_definitions(rs_pipeline PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT=0   # ← disable SSL, tránh link lỗi
)

target_link_libraries(rs_pipeline PRIVATE
    ${GDAL_LIBRARIES}
    ${PQXX_LIBRARIES}
    Threads::Threads
    stdc++fs
    ssl                  # ← thêm
    crypto               # ← thêm
)

target_compile_options(rs_pipeline PRIVATE -Wall -Wextra)

# Test
add_executable(test_state_machine
    tests/test_state_machine.cpp
    src/pipeline/state_machine.cpp
)
target_include_directories(test_state_machine PRIVATE src/)
target_link_libraries(test_state_machine PRIVATE Threads::Threads)