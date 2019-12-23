#!/bin/bash

cmake .. \
	-DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_PREFIX_PATH="$1/google;$1/mongodb;$1/nemtech;$1/zeromq" \
	\
	-DCATAPULT_BUILD_DEVELOPMENT=1 \
	-DCATAPULT_BUILD_RELEASE=0 \
	\
	-DLIBMONGOCXX_INCLUDE_DIRS="$1/mongodb/libmongoc/include" \
	-DLIBMONGOCXX_DIR="$1/mongodb/lib/cmake/libmongocxx-3.4.0" \
	-DLIBBSONCXX_DIR="$1/mongodb/lib/cmake/libbsoncxx-3.4.0" \
	\
	-GNinja
