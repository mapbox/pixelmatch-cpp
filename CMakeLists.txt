cmake_minimum_required(VERSION 3.15)

project(pixelmatch
    VERSION 1.0.0
    DESCRIPTION "C++ port of pixelmatch, a pixel-level image comparison library"
    LANGUAGES CXX
)

add_library(pixelmatch INTERFACE)
add_library(mapbox::pixelmatch ALIAS pixelmatch)

target_include_directories(pixelmatch INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(pixelmatch INTERFACE cxx_std_17)

if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
    find_package(PNG REQUIRED)

    add_executable(test_pixelmatch test/test.cpp)
    target_link_libraries(test_pixelmatch PRIVATE pixelmatch PNG::PNG)
    target_compile_options(test_pixelmatch PRIVATE -Wall -Wextra -Wshadow)
    add_test(NAME pixelmatch
        COMMAND test_pixelmatch
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(bench_pixelmatch test/bench.cpp)
    target_link_libraries(bench_pixelmatch PRIVATE pixelmatch PNG::PNG)
    target_compile_options(bench_pixelmatch PRIVATE -Wall -Wextra)
endif()

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
