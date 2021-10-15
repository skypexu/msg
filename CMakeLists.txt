cmake_minimum_required (VERSION 2.6)
project(libmessenger CXX C ASM)
include(CheckFunctionExists)

option(test "Build all tests." OFF)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
      "Choose the type of build, options are: None Debug Release" FORCE)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug")
endif()

get_directory_property(hasParent PARENT_DIRECTORY)

check_function_exists(splice MSGR_HAVE_SPLICE)
check_function_exists(accept4 MSGR_HAVE_ACCEPT4)

set(DEBUG_FLAGS "-g")
option(profiling "Enable profiling." OFF)
if (profiling)
    set(PROFILING_FLAGS -pg)
endif (profiling)
    
set(PROJECT_C_FLAGS "${DEBUG_FLAGS} -Wall -Wmissing-prototypes ${PROFLING_FLAGS} -D_GNU_SOURCE -fPIC " ${PROJECT_C_FLAGS})
set(PROJECT_CXX_FLAGS "-std=c++0x ${DEBUG_FLAGS} -Wall ${PROFLING_FLAGS} -D_GNU_SOURCE -fPIC" ${PROJECT_CXX_FLAGS})
if (HAVE_SPLICE)
    set(PROJECT_C_FLAGS "-DHAVE_SPLICE=1 ${PROJECT_C_FLAGS}")
    set(PROJECT_CXX_FLAGS "-DHAVE_SPLICE=1 ${PROJECT_CXX_FLAGS}")
endif()

set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} ${PROJECT_C_FLAGS})
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} ${PROJECT_CXX_FLAGS})

find_path(LIBEV_INC_DIR ev.h HINTS /usr/include /usr/local/include)
find_library(LIBEV NAMES libev.so /usr/lib /usr/local/lib)
message(STATUS "libev.so is " ${LIBEV})

if (NOT LIBEV)
    message(FATAL_ERROR "libev not found")
endif()

include_directories(${PROJECT_SOURCE_DIR}/include)

if ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64")
    find_program(YASM_EXE NAMES yasm)
    if (NOT YASM_EXE)
        message(STATUS "yasm not found")
    else ()
        execute_process(COMMAND ${YASM_EXE} -f elf64 ${PROJECT_SOURCE_DIR}/src/crc32c_intel_fast_asm.S -o /dev/null
           RESULT_VARIABLE YASM_RESULT)
        if (YASM_RESULT EQUAL 0)
           message(STATUS "have yasm compiler")
           set(MSGR_HAVE_GOOD_YASM_ELF64 1)
        endif()
    endif ()
endif ()

set(SOURCES
    src/arm.c
    src/armor.c
    src/buffer.cc
    src/BufferAllocator.cc
    src/Clock.cc
    src/Connection.cc
    src/crc32c.cc
    src/crc32c_intel_baseline.c
    src/crc32c_intel_fast.c
    src/Crypto.cc
    src/dout.cc
    src/environment.cc
    src/cpp_errno.cc
    src/intel.c
    src/io_priority.cc
    src/Message.cc
    src/Messenger.cc
    src/msg_types.cc
    src/Mutex.cc
    src/PrebufferedStreambuf.cc
    src/page.cc
    src/probe.cc
    src/msgr_assert.cc
    src/msgr_strings.cc
    src/MsgrConfig.cc
    src/MsgrContext.cc
    src/safe_io.cc
    src/sctp_crc32.c
    src/simple_spin.cc
    src/str_list.cc
    src/strtol.cc
    src/Thread.cc
    src/Throttle.cc
    src/RefCountedObj.cc
    src/DispatchQueue.cc
    src/async/Event.cc
    src/async/net_handler.cc
    src/async/AsyncMessenger.cc
    src/async/AsyncConnection.cc
    src/async/LocalConnection.cc
    )

if (MSGR_HAVE_GOOD_YASM_ELF64)
    add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/crc32c_intel_fast_asm.o COMMAND ${YASM_EXE}
                   ARGS -f elf64 -o ${PROJECT_BINARY_DIR}/crc32c_intel_fast_asm.o ${PROJECT_SOURCE_DIR}/src/crc32c_intel_fast_asm.S)
    add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/crc32c_intel_fast_zero_asm.o COMMAND ${YASM_EXE}
                   ARGS -f elf64 -o ${PROJECT_BINARY_DIR}/crc32c_intel_fast_zero_asm.o ${PROJECT_SOURCE_DIR}/src/crc32c_intel_fast_zero_asm.S)
    SET(asm_objs ${PROJECT_BINARY_DIR}/crc32c_intel_fast_asm.o ${PROJECT_BINARY_DIR}/crc32c_intel_fast_zero_asm.o)
endif(MSGR_HAVE_GOOD_YASM_ELF64)

add_library(messenger SHARED ${SOURCES} ${asm_objs})
target_link_libraries(messenger ${LIBEV})
set_target_properties(messenger PROPERTIES VERSION "1.0.0")

if (hasParent)
    set(LIBMESSENGER $<TARGET_FILE:messenger> PARENT_SCOPE)
endif()

configure_file(acconfig.h.in ${PROJECT_BINARY_DIR}/acconfig.h)
include_directories(${PROJECT_BINARY_DIR})
include_directories(${LIBEV_INC_DIR})

set(CPACK_PACKAGE_NAME libmessenger)
set(CPACK_PACKAGE_VENDOR "Panguv2")
set(CPACK_PACKAGE_VERSION_MAJOR 1)
set(CPACK_PACKAGE_VERSION_MINOR 0)
set(CPACK_PACKAGE_VERSION_PATCH 0)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "libmessenger by xuyifeng")
set(CPACK_MONOLITHIC_INSTALL TRUE)
set(CPACK_GENERATOR "RPM;DEB")
set(CPACK_PACKAGE_CONTACT "xuyifeng")
set(PKG_DIR "/usr/local")

install(TARGETS messenger LIBRARY DESTINATION "${PKG_DIR}/lib")

file(GLOB msgr_HEADERS
    "include/msgr/*.h"
)

install(
  FILES
    ${msgr_HEADERS}
  DESTINATION "${PKG_DIR}/include/msgr")

include(CPack)

if (test)                                                                       
    enable_testing()
    add_subdirectory(gtest)
    SET(TEST_SRCS
        tests/client.cc
        tests/server.cc
        tests/test_msgr.cc
       )
    include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})
    foreach(tfile ${TEST_SRCS})
        get_filename_component(FNAME ${tfile} NAME_WE)
        add_executable(${FNAME} ${tfile})
        target_link_libraries(${FNAME} messenger uuid)
        target_link_libraries(${FNAME} ${LIBEV} gtest gtest_main m pthread)
        add_test(NAME ${FNAME} COMMAND ${FNAME})
    endforeach()
endif()


