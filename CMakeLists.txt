cmake_minimum_required(VERSION 3.21)

project(STOWArchive LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_executable(stow_app WIN32 src/windows/main.cpp)
    target_link_libraries(stow_app PRIVATE user32 gdi32)
elseif(APPLE)
    enable_language(OBJCXX)
    add_executable(stow_app src/macos/main.mm)
    target_link_libraries(stow_app PRIVATE "-framework Cocoa")
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

    add_executable(stow_app src/linux/main.cpp)
    target_link_libraries(stow_app PRIVATE PkgConfig::GTK4)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()
