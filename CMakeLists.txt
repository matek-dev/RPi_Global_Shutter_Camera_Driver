cmake_minimum_required(VERSION 3.15)
project(gs_cam LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_BUILD_TYPE Release)

# Find libcamera
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCAMERA REQUIRED libcamera)

add_executable(gs_cam
    src/main.cpp
    src/DngWriter.cpp
    src/Util.cpp
)

target_include_directories(gs_cam PRIVATE
    ${LIBCAMERA_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gs_cam
    ${LIBCAMERA_LIBRARIES}
)

target_compile_options(gs_cam PRIVATE ${LIBCAMERA_CFLAGS_OTHER})
