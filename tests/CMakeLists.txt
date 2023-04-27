cmake_minimum_required (VERSION 3.16)
project (CppOpenCVUtilTests)

if (NOT TARGET CppOpenCVUtilLib)
    add_subdirectory("../src" CppOpenCVUtilLib)
endif()

find_package(GTest CONFIG REQUIRED)

set(SOURCE_FILES
	main.cpp
	ImageUtilTests.cpp
	)

# Add source to this project's executable.
add_executable(cppcvutiltest ${SOURCE_FILES} )

#target_include_directories(cppcvutiltest PUBLIC ".")

target_link_libraries(cppcvutiltest PRIVATE CppOpenCVUtilLib GTest::gtest_main)

# CTest on top of googletest, with test discovery
include(GoogleTest)
enable_testing()
gtest_discover_tests(cppcvutiltest)
