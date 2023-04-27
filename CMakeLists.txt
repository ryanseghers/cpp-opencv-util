cmake_minimum_required (VERSION 3.16)
project(CppOpenCVUtil LANGUAGES CXX VERSION 0.0.0)

if (NOT TARGET CppOpenCVUtilLib)
    add_subdirectory("src" CppOpenCVUtilLib)
endif()

if (NOT TARGET CppOpenCVUtilTests)
    add_subdirectory("tests" CppOpenCVUtilTests)
endif()

#
# CTest on top of googletest, with test discovery
# (took trial and error between here and the tests CMakeLists.txt to get VS to discover the tests)
#
include(GoogleTest)
enable_testing()
