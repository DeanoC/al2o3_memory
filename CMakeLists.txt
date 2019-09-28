cmake_minimum_required(VERSION 3.12.4)

option(unittests "unittests" OFF)

get_directory_property(hasParent PARENT_DIRECTORY)
if(NOT hasParent)
	include(FetchContent)
	FetchContent_Declare( al2o3 GIT_REPOSITORY https://github.com/DeanoC/al2o3 GIT_TAG master )
	FetchContent_GetProperties(al2o3)
	if(NOT al2o3_POPULATED)
		FetchContent_Populate(al2o3)
		add_subdirectory(${al2o3_SOURCE_DIR} ${al2o3_BINARY_DIR})
	endif()
	INIT_AL2O3(${CMAKE_CURRENT_SOURCE_DIR})
endif ()

SET_MIN_VERSIONS()
set(LibName al2o3_memory)
project(${LibName})


set(Interface
		memory.h
		)
set(Src
		memory.c
		)
set(Deps
		al2o3_platform)
ADD_LIB(${LibName} "${Interface}" "${Src}" "${Deps}")

if(unittests)
	include(CTEST)
	set( Tests
		runner.cpp
		test_memory.cpp
		)
	set( TestDeps
		al2o3_catch2 )
	ADD_LIB_TESTS(${LibName} "${Interface}" "${Tests}" "${TestDeps}")
	add_test(NAME test_${LibName} 
		COMMAND test_${LibName}
		WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif()
