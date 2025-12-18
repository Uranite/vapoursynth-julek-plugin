cmake_minimum_required(VERSION 3.23)

project(libjulek LANGUAGES CXX)

# libjxl build options
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_DEVTOOLS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_BENCHMARK OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_JNI OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_VIEWERS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_PLUGINS OFF CACHE BOOL "" FORCE)
set(JPEGXL_STATIC ON CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_SKCMS ON CACHE BOOL "" FORCE)

add_subdirectory(thirdparty/libjxl EXCLUDE_FROM_ALL)

add_library(julek SHARED
	src/AGM.cpp
	src/AutoGain.cpp
	src/Butteraugli.cpp
	src/ColorMap.cpp
	src/RFS.cpp
	src/shared.cpp
	src/ssimulacra.cpp
	src/VisualizeDiffs.cpp
	src/torgbs.cpp
	thirdparty/libjxl/tools/ssimulacra.cc
	thirdparty/libjxl/tools/no_memory_manager.cc
	thirdparty/libjxl/tools/ssimulacra2.cc
)

target_include_directories(julek PRIVATE
	thirdparty/libjxl
	thirdparty/libjxl/third_party/skcms
)

set_target_properties(julek PROPERTIES
	CXX_EXTENSIONS OFF
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
)
target_compile_definitions(julek PRIVATE JPEGXL_ENABLE_SKCMS=1)

target_link_libraries(julek PRIVATE
	jxl
	jxl_threads
	hwy
	jxl_gauss_blur
)

if((CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64") OR(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64"))
	message(STATUS "julek: ${CMAKE_SYSTEM_PROCESSOR} processor detected, using vectorclass")
	target_compile_definitions(julek PRIVATE PLUGIN_X86)
	target_include_directories(julek PRIVATE thirdparty/vectorclass)
	target_sources(julek PRIVATE
		thirdparty/vectorclass/instrset_detect.cpp
		src/AVX2/AGM_AVX2.cpp
		src/AVX2/AutoGain_AVX2.cpp
	)
	
	if(MSVC)
		set_source_files_properties(src/AVX2/AGM_AVX2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
		set_source_files_properties(src/AVX2/AutoGain_AVX2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
	else()
		set_source_files_properties(src/AVX2/AGM_AVX2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
		set_source_files_properties(src/AVX2/AutoGain_AVX2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
	endif()

else()
	message(STATUS "julek: ${CMAKE_SYSTEM_PROCESSOR} processor detected, unable to use vectorclass")
endif()

target_include_directories(julek PRIVATE
	thirdparty/vapoursynth/include
)

install(TARGETS julek LIBRARY RUNTIME)

find_package(Git QUIET)

if(GIT_FOUND)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} describe --tags --long --always
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		OUTPUT_VARIABLE VCS_TAG
	)

	if(VCS_TAG)
		string(STRIP ${VCS_TAG} VCS_TAG)
	endif()
endif()

if(VCS_TAG)
	message(STATUS "vapoursynth-julek-plugin ${VCS_TAG}")
else()
	message(WARNING "unknown plugin version")
	set(VCS_TAG "unknown")
endif()

configure_file(src/config.h.in config.h)

include_directories(${CMAKE_CURRENT_BINARY_DIR})