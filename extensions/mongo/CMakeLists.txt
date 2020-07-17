cmake_minimum_required(VERSION 3.14)
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
include(${CMAKE_BINARY_DIR}/conan_paths.cmake)

message("--- locating mongo dependencies ---")
find_package(mongo-cxx-driver REQUIRED)

message("mongocxx  ver: ${LIBMONGOCXX_PACKAGE_VERSION}")
find_library(MONGOC_LIB mongoc-1.0 ${mongo-cxx-driver_LIBRARIES})
find_library(BSONC_LIB bson-1.0 ${mongo-cxx-driver_LIBRARIES})

if(MSVC)
	set(MONGO_ADDITIONAL_LIBS bcrypt crypt32 secur32 ws2_32)
else()
	set(MONGO_ADDITIONAL_LIBS)
endif()

message("mongocxx libs: ${mongo-cxx-driver_LIBRARIES_TARGETS}")
message("mongocxx  inc: ${mongo-cxx-driver_INCLUDE_DIRS}")
message("mongoc    lib: ${MONGOC_LIB}")
message("bsonc     lib: ${BSONC_LIB}")

### find and set mongo includes and libraries
function(catapult_add_mongo_dependencies TARGET_NAME)
	include_directories(SYSTEM ${mongo-cxx-driver_INCLUDE_DIR})
	target_link_libraries(${TARGET_NAME} ${mongo-cxx-driver_LIBS} ${MONGO_ADDITIONAL_LIBS})
endfunction()

# used to define a catapult mongo int test executable by combining catapult_test_executable and catapult_target
function(catapult_int_test_executable_target TARGET_NAME)
	catapult_test_executable(${TARGET_NAME} ${ARGN})
	catapult_target(${TARGET_NAME})

	set_property(TEST ${TARGET_NAME} PROPERTY LABELS Database)

	# customize and export compiler options for gtest
	catapult_set_test_compiler_options()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
endfunction()

add_subdirectory(plugins)

catapult_define_extension_no_group(mongo)
catapult_add_mongo_dependencies(extension.mongo)

# add extension to mongo group
add_dependencies(mongo extension.mongo)
