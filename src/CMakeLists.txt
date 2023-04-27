cmake_minimum_required (VERSION 3.16)
project(CppOpenCVUtilLib LANGUAGES CXX VERSION 0.0.0)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fmt CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED)

# get CppBaseUtil via relative path
add_subdirectory("../../cpp-base-util/src" CppBaseUtilLib)

set(SOURCE_FILES
	CollageSpec.h
	FloatHist.h
	FloatHist.cpp
	ImageUtil.h
	ImageUtil.cpp
)

add_library(CppOpenCVUtilLib STATIC ${SOURCE_FILES})
target_link_libraries(CppOpenCVUtilLib PUBLIC CppBaseUtilLib ${OpenCV_LIBS})
target_include_directories(CppOpenCVUtilLib PUBLIC ".")
