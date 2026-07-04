cmake_minimum_required(VERSION 3.15)
project(apkpatch C)

set(CMAKE_C_STANDARD 11)

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(MINIZIP_DIR "${PROJECT_ROOT}/third_party/zlib/contrib/minizip")

add_library(apkpatch SHARED
    apkpatch.c
    "${PROJECT_ROOT}/common/zlib_stream.c"
    "${PROJECT_ROOT}/bspatch.c"
    "${MINIZIP_DIR}/ioapi.c"
    "${MINIZIP_DIR}/unzip.c"
    "${MINIZIP_DIR}/zip.c"
)

target_include_directories(apkpatch PRIVATE
    "${PROJECT_ROOT}"
    "${MINIZIP_DIR}"
)

# NDK provides zlib and log
find_library(log-lib log)
target_link_libraries(apkpatch z ${log-lib})
