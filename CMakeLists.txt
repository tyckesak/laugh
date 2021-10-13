cmake_minimum_required(VERSION 3.21)

project(laugh)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)

option(LAUGH_BUILD_EXAMPLES "Build examples in the Example/ folder" ON)
option(LAUGH_BUILD_DOCS     "Build Doxygen documentation for the project" ON)

add_subdirectory(Extern/concurrentqueue)

add_library(laugh Source/Actor.cpp)

set_target_properties(laugh PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED TRUE
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/Library
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/Library
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/Library)

target_include_directories(laugh PUBLIC ${PROJECT_SOURCE_DIR}/Include)
target_link_libraries(laugh concurrentqueue)

if(LAUGH_BUILD_DOCS)
    find_package(Doxygen)
    set(DOXYGEN_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/Doxygen)
    doxygen_add_docs(docs ${PROJECT_SOURCE_DIR}/Include
                     COMMENT "Build documentation")
endif()

if(LAUGH_BUILD_EXAMPLES)
    add_executable(pingpong Example/Pingpong.cpp)
    add_executable(ask Example/Ask.cpp)
    add_executable(become Example/Become.cpp)

    set_target_properties(pingpong ask become PROPERTIES
                          CXX_STANDARD 20
                          CXX_STANDARD_REQUIRED TRUE
                          RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/Example)

    target_link_libraries(pingpong laugh)
    target_link_libraries(ask laugh)
    target_link_libraries(become laugh)
endif()


