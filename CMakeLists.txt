cmake_minimum_required(VERSION 3.16)

project(suave VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SUAVE_BUILD_TESTS "Build the unit tests" ON)

# Platform independent logic: loudness meter, controller, config, parsers.
add_library(suave_core STATIC
    src/core/level_meter.cpp
    src/core/controller.cpp
    src/core/config.cpp
    src/net/http_client.cpp
    src/actuators/pactl_parse.cpp
)
target_include_directories(suave_core PUBLIC include src)

if(WIN32)
    target_link_libraries(suave_core PUBLIC ws2_32)
    target_compile_definitions(suave_core PUBLIC _WIN32_WINNT=0x0601)
endif()

if(MSVC)
    target_compile_options(suave_core PRIVATE /W4)
    target_compile_definitions(suave_core PUBLIC _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(suave_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Command line tool.
add_executable(suave
    src/app/main.cpp
    src/app/runner.cpp
    src/capture/miniaudio_capture.cpp
    src/capture/miniaudio_impl.c
    src/actuators/factory.cpp
    src/actuators/vlc_http_actuator.cpp
    src/actuators/pulse_actuator.cpp
    src/actuators/wasapi_actuator.cpp
)
target_include_directories(suave PRIVATE third_party)
target_compile_definitions(suave PRIVATE SUAVE_VERSION="${PROJECT_VERSION}")
target_link_libraries(suave PRIVATE suave_core)

if(WIN32)
    target_link_libraries(suave PRIVATE ole32)
    if(MINGW)
        target_link_options(suave PRIVATE -static)
    endif()
else()
    find_package(Threads REQUIRED)
    target_link_libraries(suave PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

include(GNUInstallDirs)
install(TARGETS suave RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(SUAVE_BUILD_TESTS)
    enable_testing()
    add_executable(suave_tests tests/test_main.cpp)
    target_link_libraries(suave_tests PRIVATE suave_core)
    add_test(NAME suave_tests COMMAND suave_tests)
endif()
