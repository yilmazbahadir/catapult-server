#! /bin/bash

if [ "$#" -ne 1 ]; then
	echo "dependencies directory is required"
	exit 1
fi

if [ ! -d "$1" ]; then
	echo "dependencies directory must exist"
	exit 1
fi

deps_dir=$1
boost_output_dir=${deps_dir}/boost
gtest_output_dir=${deps_dir}/gtest
mongo_output_dir=${deps_dir}/mongo
zmq_output_dir=${deps_dir}/zmq

echo "boost_output_dir: ${boost_output_dir}"
echo "gtest_output_dir: ${gtest_output_dir}"
echo "mongo_output_dir: ${mongo_output_dir}"
echo "  zmq_output_dir: ${zmq_output_dir}"
echo

# region boost

function install_boost {
	local boost_ver=1_71_0
	local boost_ver_dotted=1.71.0

	curl -o boost_${boost_ver}.tar.gz -SL https://dl.bintray.com/boostorg/release/${boost_ver_dotted}/source/boost_${boost_ver}.tar.gz
	tar -xzf boost_${boost_ver}.tar.gz
	cd boost_${boost_ver}

	mkdir ${boost_output_dir}
	./bootstrap.sh with-toolset=clang --prefix=${boost_output_dir}

	b2_options=()
	b2_options+=(toolset=clang)
	b2_options+=(cxxflags='-std=c++1y -stdlib=libc++')
	b2_options+=(linkflags='-stdlib=libc++')
	b2_options+=(--prefix=${boost_output_dir})
	./b2 ${b2_options[@]} -j 8 stage release
	./b2 install ${b2_options[@]}
}

# endregion

# region google test + benchmark

function install_git_dependency {
	git clone git://github.com/${1}/${2}.git
	cd ${2}
	git checkout ${3}

	mkdir _build
	cd _build

	cmake_options+=(-DCMAKE_CXX_FLAGS='-std=c++1y -stdlib=libc++')
	cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="${deps_dir}/${1}" ${cmake_options[@]} ..
	make -j 8 && make install
}

function install_google_test {
	cmake_options=()
	cmake_options+=(-DCMAKE_POSITION_INDEPENDENT_CODE=ON)
	install_git_dependency google googletest release-1.8.1
}

function install_google_benchmark {
	cmake_options=()
	cmake_options+=(-DBENCHMARK_ENABLE_GTEST_TESTS=OFF)
	install_git_dependency google benchmark v1.5.0
}

# endregion

# region mongo

function install_mongo_c_driver {
	cmake_options=(-DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF)
	install_git_dependency mongodb mongo-c-driver 1.15.1
}

function install_mongo_cxx_driver {
	cmake_options=()
	cmake_options+=(-DBOOST_ROOT=${boost_output_dir})
	cmake_options+=(-DLIBBSON_DIR=${mongo_output_dir})
	cmake_options+=(-DLIBMONGOC_DIR=${mongo_output_dir})
	cmake_options+=(-DCMAKE_CXX_STANDARD=17)
	cmake_options+=(-DBSONCXX_POLY_USE_BOOST=1) # usage is incompatible with std::optional
	install_git_dependency mongodb mongo-cxx-driver r3.4.0
}

# endregion

# region zmq

function install_zmq_lib {
	cmake_options=()
	install_git_dependency zeromq libzmq v4.3.2
}

function install_zmq_cpp {
	cmake_options=()
	install_git_dependency zeromq cppzmq v4.4.1
}

# endregion

# region rocks

function install_rocks {
	cmake_options=(PORTABLE=1)
	cmake_options+=(USE_SSE=1)
	cmake_options+=(-DWITH_TESTS=OFF)
	install_git_dependency nemtech rocksdb v6.2.4-nem
}

# endregion

cd ${deps_dir}

if [ -d source ]; then
	echo "previous dependencies installation attempt detected"
	exit 0
fi

mkdir source

declare -a installers=(
	install_boost
	install_google_test
	install_google_benchmark
	install_mongo_c_driver
	install_mongo_cxx_driver
	install_zmq_lib
	install_zmq_cpp
	install_rocks
)
for install in "${installers[@]}"
do
	pushd source > /dev/null
	${install}
	popd > /dev/null
done
