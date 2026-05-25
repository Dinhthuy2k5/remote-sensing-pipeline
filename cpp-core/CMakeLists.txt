cmake_minimum_required(VERSION 3.20)
project(rs_pipeline VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Dependencies ─────────────────────────────────────────────
find_package(GDAL REQUIRED)
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED libpqxx)

# ─── Sources ──────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(rs_pipeline ${SOURCES})

target_include_directories(rs_pipeline PRIVATE
    src/
    ${GDAL_INCLUDE_DIRS}
    ${PQXX_INCLUDE_DIRS}
)

target_link_libraries(rs_pipeline PRIVATE
    ${GDAL_LIBRARIES}
    ${PQXX_LIBRARIES}
    Threads::Threads
)

target_compile_options(rs_pipeline PRIVATE -Wall -Wextra)