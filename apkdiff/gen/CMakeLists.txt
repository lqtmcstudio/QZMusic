cmake_minimum_required(VERSION 3.15)
project(apkdiff_gen C)

set(CMAKE_C_STANDARD 11)

set(BUNDLED_ZLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/zlib")

message(STATUS "Using bundled zlib from ${BUNDLED_ZLIB_DIR}")
add_subdirectory("${BUNDLED_ZLIB_DIR}" "${CMAKE_BINARY_DIR}/zlib" EXCLUDE_FROM_ALL)
set(ZLIB_LIB zlibstatic)
set(ZLIB_INC "${BUNDLED_ZLIB_DIR}")

set(MINIZIP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/zlib/contrib/minizip")

add_library(minizip STATIC
    "${MINIZIP_DIR}/ioapi.c"
    "${MINIZIP_DIR}/unzip.c"
    "${MINIZIP_DIR}/zip.c"
)
target_include_directories(minizip PUBLIC "${MINIZIP_DIR}" "${ZLIB_INC}")
target_compile_definitions(minizip PRIVATE
    NOCRYPT NOUNCRYPT
    $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS;USE_FILE32API>
)

add_executable(gen
    main.c
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/zlib_stream.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../bsdiff.c"
)

target_include_directories(gen PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${MINIZIP_DIR}"
    "${ZLIB_INC}"
)

target_link_libraries(gen PRIVATE minizip ${ZLIB_LIB})

target_compile_definitions(gen PRIVATE
    $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)
