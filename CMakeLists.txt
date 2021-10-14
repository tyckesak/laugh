cmake_minimum_required(VERSION 3.21)

project(laugh)

include(ExternalProject)
include(GNUInstallDirs)

option(LAUGH_BUILD_EXAMPLES "Build examples in the Example/ folder" ON)
option(LAUGH_BUILD_DOCS     "Build Doxygen documentation for the project" ON)
option(LAUGH_BUILD_SHARED   "Build as shared library" ON)

option(LAUGH_INSTALL_CONCURRENTQUEUE_HEADERS
    "Run the 'install' target on the concurrentqueue submodule" OFF)


if(LAUGH_INSTALL_CONCURRENTQUEUE_HEADERS)
    set(LAUGH_CCQ_INSTALL cmake --install .)
else()
    set(LAUGH_CCQ_INSTALL "")
endif()


ExternalProject_Add(concurrentqueue
    PREFIX ${PROJECT_SOURCE_DIR}/Extern
    TMP_DIR      Extern/Temporary
    SOURCE_DIR   Extern/concurrentqueue/moodycamel
    BINARY_DIR   Extern/concurrentqueue-build
    STAMP_DIR    Extern/concurrentqueue-stamp
    DOWNLOAD_DIR Extern/concurrentqueue
    INSTALL_COMMAND "${LAUGH_CCQ_INSTALL}"
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue
    GIT_TAG 07534961616f00728a99cbe864c5833c445cfd9b)

set(LAUGH_CCQ_HEADERS
    Extern/concurrentqueue/moodycamel/concurrentqueue.h
    Extern/concurrentqueue/moodycamel/lightweightsemaphore.h
    Extern/concurrentqueue/moodycamel/blockingconcurrentqueue.h
    Extern/concurrentqueue/moodycamel/LICENSE.md)

set(LAUGH_HEADERS Include/Laugh/Actor.hpp Include/Laugh/Actor.inl)

if(LAUGH_BUILD_SHARED)
    add_library(laugh SHARED Source/Actor.cpp)
else()
    add_library(laugh Source/Actor.cpp)
endif()

add_dependencies(laugh concurrentqueue)

set_target_properties(laugh PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED TRUE
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/Library
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/Library
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/Library
    PUBLIC_HEADER "${LAUGH_HEADERS}")

target_include_directories(laugh
    PUBLIC ${PROJECT_SOURCE_DIR}/Include
           ${PROJECT_SOURCE_DIR}/Extern)

install(TARGETS laugh
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Laugh)

if(LAUGH_INSTALL_CONCURRENTQUEUE_HEADERS)
    install(FILES ${LAUGH_CCQ_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/concurrentqueue/moodycamel)
endif()


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




