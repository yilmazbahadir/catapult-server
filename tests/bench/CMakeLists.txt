cmake_minimum_required(VERSION 3.14)
include(${CMAKE_BINARY_DIR}/conan_paths.cmake)
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)

# find and set benchmark includes
function(catapult_add_benchmark_dependencies TARGET_NAME)
	find_package(benchmark REQUIRED)
	# this should both link and add propers include path
	target_link_libraries(${TARGET_NAME} benchmark::benchmark)
	include_directories(SYSTEM ${CONAN_INCLUDE_DIRS_BENCHMARK})
endfunction()

# used to define a catapult bench executable
function(catapult_bench_executable_target TARGET_NAME)
	catapult_executable(${TARGET_NAME} ${ARGN})

	catapult_add_benchmark_dependencies(${TARGET_NAME})
	catapult_set_test_compiler_options()

	catapult_target(${TARGET_NAME})

	add_dependencies(tools ${TARGET_NAME})
	install(TARGETS ${TARGET_NAME})
endfunction()

add_subdirectory(crypto)

add_subdirectory(nodeps)
