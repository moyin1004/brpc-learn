cmake_minimum_required(VERSION 3.11)

project(brpc-learn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${DEFINE_CLOCK_GETTIME} -O2 -W -Wall -Wno-unused-parameter -fPIC -fsanitize=address -fno-omit-frame-pointer")

add_subdirectory(brpc)

find_path(LEVELDB_INCLUDE_PATH NAMES leveldb/db.h)
find_library(LEVELDB_LIB NAMES leveldb)

if((NOT LEVELDB_INCLUDE_PATH) OR(NOT LEVELDB_LIB))
    message(FATAL_ERROR "Fail to find leveldb")
endif()

find_package(PkgConfig REQUIRED)
# find_package(Protobuf REQUIRED)
include(FindProtobuf)

include(brpc/cmake/CompileProto.cmake)
compile_proto(PROTO_HDRS PROTO_SRCS ${CMAKE_SOURCE_DIR}/proto ${CMAKE_SOURCE_DIR}/proto ${CMAKE_SOURCE_DIR}/protosrc http.proto)
add_library(TEST_PROTO_LIB OBJECT ${PROTO_SRCS} ${PROTO_HDRS})

pkg_check_modules(brpc REQUIRED IMPORTED_TARGET openssl gflags)

add_executable(bvar_test 
    bvar_test/main.cc
    $<TARGET_OBJECTS:TEST_PROTO_LIB>
)

add_executable(bthread_learn 
    bthread_learn/main.cc
)

add_executable(mem_learn 
    mempool/main.cc
)

macro(brpc_dependencies exec_name)
    add_dependencies(${exec_name} brpc-static)

    target_link_libraries(${exec_name}
        PRIVATE brpc
        PRIVATE PkgConfig::brpc
        PRIVATE leveldb
        PRIVATE pthread
        PRIVATE protobuf
    )

    target_include_directories(${exec_name}
        PRIVATE ${brpc_BINARY_DIR}/output/include
        PRIVATE ${LEVELDB_INCLUDE_PATH}
        PRIVATE ${CMAKE_SOURCE_DIR}
    )

    target_link_directories(${exec_name}
        PRIVATE ${brpc_BINARY_DIR}/output/lib
    )
endmacro(brpc_dependencies)

brpc_dependencies(bvar_test)
brpc_dependencies(bthread_learn)
brpc_dependencies(mem_learn)