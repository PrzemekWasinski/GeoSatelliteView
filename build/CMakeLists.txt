cmake_minimum_required(VERSION 3.10)
project(TimelapseApp VERSION 1.0)

#C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#source files
set(SOURCES
    ../main.cpp
    ../timelapse.cpp
)

#output folder
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/output)

#executable
add_executable(main ${SOURCES})

#OpenCV using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENCV REQUIRED opencv4)

#directories for OpenCV
target_include_directories(main PRIVATE ${OPENCV_INCLUDE_DIRS})

#link libraries
target_link_libraries(main PRIVATE ${OPENCV_LIBRARIES} curl)
