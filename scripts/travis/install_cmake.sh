#! /bin/bash

set -ex

if [ "$#" -ne 1 ]; then
	echo "cmake directory is required"
	exit 1
fi

if [ ! -d "$1" ]; then
	echo "cmake directory must exist"
	exit 1
fi

cmake_dir=$1
cd ${cmake_dir}

if [ -d bin ]; then
	echo "previous dependencies installation attempt detected"
	exit 0
fi

function install_cmake {
	local cmake_ver=3.15.3
	local cmake_script="cmake-${cmake_ver}-Linux-x86_64.sh"

	curl -o ${cmake_script} -SL "https://github.com/Kitware/CMake/releases/download/v${cmake_ver}/${cmake_script}"
	chmod +x ${cmake_script}
	./${cmake_script} --prefix=${cmake_dir} --exclude-subdir --skip-license
	rm -rf ${cmake_script}

	echo curl -o ${cmake_script} -SL "https://github.com/Kitware/CMake/releases/download/v${cmake_ver}/${cmake_script}"
	echo chmod +x ${cmake_script}
	echo ./${cmake_script} --prefix=${cmake_dir} --exclude-subdir --skip-license
	echo rm -rf ${cmake_script}
}

install_cmake
