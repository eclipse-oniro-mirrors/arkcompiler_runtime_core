#!/usr/bin/env bash
# Copyright (c) 2021-2024 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -eo pipefail

SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
ARK_ROOT="$SCRIPT_DIR/../.."

BUILD_TYPE_RELEASE="Release"
BUILD_TYPE_FAST_VERIFY="FastVerify"
BUILD_TYPE_DEBUG="Debug"

OHOS_SDK_NATIVE="$(realpath "$1")"
SDK_BUILD_ROOT="$(realpath "$2")"
PANDA_SDK_BUILD_TYPE="${3:-"$BUILD_TYPE_RELEASE"}"

function usage() {
    echo "$(basename "${BASH_SOURCE[0]}") path/to/ohos/sdk/native path/to/panda/sdk/destination build_type:[$BUILD_TYPE_RELEASE,$BUILD_TYPE_FAST_VERIFY,$BUILD_TYPE_DEBUG]"
    exit 1
}

case "$PANDA_SDK_BUILD_TYPE" in
"$BUILD_TYPE_RELEASE" | "$BUILD_TYPE_FAST_VERIFY" | "$BUILD_TYPE_DEBUG") ;;
*)
    echo "Invalid build_type option!"
    usage
    ;;
esac

if [ ! -d "$OHOS_SDK_NATIVE" ]; then
    echo "Error: No such directory: $OHOS_SDK_NATIVE"
    usage
fi

if [ -z "$SDK_BUILD_ROOT" ]; then
    echo "Error: path to panda sdk destination is not provided"
    usage
fi

PANDA_SDK_PATH="$SDK_BUILD_ROOT/sdk"

# Arguments: build_dir, cmake_arguments, ninja_targets
function build_panda() {
    local build_dir="$1"
    local cmake_arguments="$2"
    local ninja_targets="$3"

    local product_build="OFF"
    if [ "$PANDA_SDK_BUILD_TYPE" = "$BUILD_TYPE_RELEASE" ]; then
        product_build="ON"
    fi

    CONCURRENCY=${NPROC_PER_JOB:=$(nproc)}
    COMMONS_CMAKE_ARGS="\
        -GNinja \
        -S$ARK_ROOT \
        -DCMAKE_BUILD_TYPE=$PANDA_SDK_BUILD_TYPE \
        -DPANDA_PRODUCT_BUILD=$product_build \
        -DPANDA_WITH_ECMASCRIPT=ON \
        -DPANDA_WITH_ETS=ON \
        -DPANDA_WITH_JAVA=OFF \
        -DPANDA_WITH_ACCORD=OFF \
        -DPANDA_WITH_CANGJIE=OFF"

    # shellcheck disable=SC2086
    cmake -B"$build_dir" ${COMMONS_CMAKE_ARGS[@]} $cmake_arguments
    # shellcheck disable=SC2086
    ninja -C"$build_dir" -j"${CONCURRENCY}" $ninja_targets
}

# Arguments: src, dst, file_list, include_pattern
function copy_into_sdk() {
    local src="$1"
    local dst="$2"
    local file_list="$3"
    local include_pattern="$4"
    local exclude_pattern='\(/tests/\|/test/\)'

    # Below construction (cd + find + cp --parents) is used to copy
    # all files listed in file_list with their relative paths
    # Example: cd /path/to/panda && cp --parents runtime/include/cframe.h /dst
    # Result: /dst/runtime/include/cframe.h
    mkdir -p "$dst"
    cd "$src"
    for FILE in $(cat "$file_list"); do
        for F in $(find "$FILE" -type f | grep "$include_pattern" | grep -v "$exclude_pattern"); do
            cp --parents "$F" "$dst"
        done
    done
}

function linux_tools() {
    echo "> Building linux tools..."
    local linux_build_dir="$SDK_BUILD_ROOT/linux_host_tools"
    local linux_cmake_args=" \
        -DPANDA_CROSS_AARCH64_TOOLCHAIN_FILE=cmake/toolchain/cross-ohos-musl-aarch64.cmake \
        -DTOOLCHAIN_SYSROOT=$OHOS_SDK_NATIVE/sysroot \
        -DTOOLCHAIN_CLANG_ROOT=$OHOS_SDK_NATIVE/llvm \
        -DPANDA_WITH_ECMASCRIPT=ON"
    local linux_build_targets="ark ark_aot ark_disasm ark_link es2panda e2p_test_plugin etsnative"
    build_panda "$linux_build_dir" "$linux_cmake_args" "$linux_build_targets"
    copy_into_sdk "$linux_build_dir" "$PANDA_SDK_PATH/linux_host_tools" "$SCRIPT_DIR"/linux_host_tools.txt
}

function windows_tools() {
    echo "> Building windows tools..."
    local windows_build_dir="$SDK_BUILD_ROOT/windows_host_tools"
    local windows_cmake_args="-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/cross-clang-14-x86_64-w64-mingw32-static.cmake"
    local windows_build_targets="es2panda ark_link"
    build_panda "$windows_build_dir" "$windows_cmake_args" "$windows_build_targets"
    copy_into_sdk "$windows_build_dir" "$PANDA_SDK_PATH/windows_host_tools" "$SCRIPT_DIR"/windows_host_tools.txt
}

function ohos() {
    echo "> Building runtime for OHOS ARM64..."
    local ohos_build_dir="$SDK_BUILD_ROOT/ohos_arm64"
    local taget_sdk_dir="$PANDA_SDK_PATH/ohos_arm64"
    local target_cmake_args=" \
        -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/cross-ohos-musl-aarch64.cmake \
        -DTOOLCHAIN_SYSROOT=$OHOS_SDK_NATIVE/sysroot \
        -DTOOLCHAIN_CLANG_ROOT=$OHOS_SDK_NATIVE/llvm \
        -DPANDA_ETS_INTEROP_JS=ON \
        -DPANDA_WITH_ECMASCRIPT=ON"
    local ohos_build_targets="ark ark_aot arkruntime arkassembler ets_interop_js_napi e2p_test_plugin etsnative"
    build_panda "$ohos_build_dir" "$target_cmake_args" "$ohos_build_targets"
    copy_into_sdk "$ohos_build_dir" "$taget_sdk_dir" "$SCRIPT_DIR"/ohos_arm64.txt

    echo "> Copying headers into SDK..."
    local headers_dst="$taget_sdk_dir"/include
    # Source headers
    copy_into_sdk "$ARK_ROOT" "$headers_dst" "$SCRIPT_DIR"/headers.txt '\(\.h$\|\.inl$\|\.inc$\)'
    # Generated headers
    copy_into_sdk "$ohos_build_dir" "$headers_dst" "$SCRIPT_DIR"/gen_headers.txt '\(\.h$\|\.inl$\|\.inc$\)'
    # Copy compiled etsstdlib into Panda SDK
    mkdir -p "$PANDA_SDK_PATH"/ets
    cp -r "$ohos_build_dir"/plugins/ets/etsstdlib.abc "$PANDA_SDK_PATH"/ets
}

function ts_linter() {
    echo "> Building tslinter..."
    local linter_root="$ARK_ROOT/tools/es2panda/linter"
    (cd "$linter_root" && npm install)
    local tgz="$(ls "$linter_root"/bundle/panda-tslinter*tgz)"
    mkdir -p "$PANDA_SDK_PATH"/tslinter
    tar -xf "$tgz" -C "$PANDA_SDK_PATH"/tslinter
    mv "$PANDA_SDK_PATH"/tslinter/package/* "$PANDA_SDK_PATH"/tslinter/
    rm -rf "$PANDA_SDK_PATH"/tslinter/node_modules
    rm -rf "$PANDA_SDK_PATH"/tslinter/package

    # Clean up
    rm "$tgz"
    rm "$linter_root"/package-lock.json
    rm -rf "$linter_root"/build
    rm -rf "$linter_root"/bundle
    rm -rf "$linter_root"/dist
    rm -rf "$linter_root"/node_modules
}

function ets_std_lib() {
    echo "> Copying ets std lib into SDK..."
    mkdir -p "$PANDA_SDK_PATH"/ets
    cp -r "$ARK_ROOT"/plugins/ets/stdlib "$PANDA_SDK_PATH"/ets
}

rm -r -f "$PANDA_SDK_PATH"
ohos
linux_tools
windows_tools
ts_linter
ets_std_lib

echo "> Packing NPM package..."
cp "$SCRIPT_DIR"/package.json "$PANDA_SDK_PATH"
cd "$PANDA_SDK_PATH"
npm pack --pack-destination "$SDK_BUILD_ROOT"
