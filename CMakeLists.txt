cmake_minimum_required(VERSION 3.16)
project(serialbench-c C)

set(CMAKE_C_STANDARD 99)

find_package(LibXml2 REQUIRED)
find_library(LEPTRIS_LIB libleptris
  HINTS ENV LEPTRIS_DIR "${CMAKE_SOURCE_DIR}/vendor")

add_executable(serialbench-c src/bench.c)
target_link_libraries(serialbench-c PRIVATE LibXml2::LibXml2)
if(LEPTRIS_LIB)
  target_compile_definitions(serialbench-c PRIVATE HAVE_LEPTRIS=1)
  target_link_libraries(serialbench-c PRIVATE ${LEPTRIS_LIB})
  message(STATUS "leptris: ${LEPTRIS_LIB}")
else()
  message(STATUS "leptris: not found (LEPTRIS_DIR) - benchmarking libxml2 only")
endif()
