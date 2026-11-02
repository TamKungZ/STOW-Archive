cmake_minimum_required(VERSION 3.21)

project(STOWArchive LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(STOW_CABI_INCLUDE_DIR "" CACHE PATH "Directory containing the STOW C ABI header")
if(NOT STOW_CABI_INCLUDE_DIR)
    find_path(STOW_CABI_INCLUDE_DIR stow.h
        HINTS "${CMAKE_CURRENT_SOURCE_DIR}/STOW/stow-ffi/include"
    )
endif()
if(NOT EXISTS "${STOW_CABI_INCLUDE_DIR}/stow.h")
    message(FATAL_ERROR
        "STOW C ABI header not found. Set STOW_CABI_INCLUDE_DIR to the stow-dev include directory."
    )
endif()

if(WIN32)
    set(STOW_CABI_LIBRARY
        "${CMAKE_CURRENT_SOURCE_DIR}/STOW/target/release/stow.dll.lib"
        CACHE FILEPATH "STOW C ABI import library (.lib)"
    )
    set(STOW_CABI_RUNTIME
        "${CMAKE_CURRENT_SOURCE_DIR}/STOW/target/release/stow.dll"
        CACHE FILEPATH "STOW C ABI runtime DLL"
    )
    if(NOT EXISTS "${STOW_CABI_LIBRARY}" OR NOT EXISTS "${STOW_CABI_RUNTIME}")
        message(FATAL_ERROR
            "STOW DLL/import library not found. Build STOW separately with "
            "`cargo build --manifest-path STOW/stow-ffi/Cargo.toml --release`, "
            "or set STOW_CABI_LIBRARY and STOW_CABI_RUNTIME."
        )
    endif()
    add_library(STOW::CABI SHARED IMPORTED GLOBAL)
    set_target_properties(STOW::CABI PROPERTIES
        IMPORTED_IMPLIB "${STOW_CABI_LIBRARY}"
        IMPORTED_LOCATION "${STOW_CABI_RUNTIME}"
        INTERFACE_INCLUDE_DIRECTORIES "${STOW_CABI_INCLUDE_DIR}"
    )
else()
    find_library(STOW_CABI_LIBRARY
        NAMES stow
        HINTS "${CMAKE_CURRENT_SOURCE_DIR}/STOW/target/release"
    )
    if(NOT STOW_CABI_LIBRARY)
        message(FATAL_ERROR
            "STOW shared library not found. Install the STOW runtime library "
            "or build it separately with `cargo build --manifest-path "
            "STOW/stow-ffi/Cargo.toml --release`."
        )
    endif()
    add_library(STOW::CABI SHARED IMPORTED GLOBAL)
    set_target_properties(STOW::CABI PROPERTIES
        IMPORTED_LOCATION "${STOW_CABI_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${STOW_CABI_INCLUDE_DIR}"
    )
endif()

add_library(stow_archive STATIC src/stow_archive.cpp)
target_include_directories(stow_archive PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(stow_archive PUBLIC STOW::CABI)

if(WIN32)
    add_executable(stow_app WIN32 src/windows/main.cpp)
    set_target_properties(stow_app PROPERTIES OUTPUT_NAME stow)
    target_link_libraries(stow_app PRIVATE stow_archive user32 gdi32 comdlg32 shell32)
    add_custom_command(TARGET stow_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${STOW_CABI_RUNTIME}"
            "$<TARGET_FILE_DIR:stow_app>/stow.dll"
        VERBATIM
    )

    add_executable(stow_cli src/windows/cli.cpp)
    set_target_properties(stow_cli PROPERTIES OUTPUT_NAME stow-cli)
    target_link_libraries(stow_cli PRIVATE stow_archive advapi32 shell32)
    add_custom_command(TARGET stow_cli POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${STOW_CABI_RUNTIME}"
            "$<TARGET_FILE_DIR:stow_cli>/stow.dll"
        VERBATIM
    )
elseif(APPLE)
    enable_language(OBJCXX)
    add_executable(stow_app src/macos/main.mm)
    set_target_properties(stow_app PROPERTIES OUTPUT_NAME stow)
    target_link_libraries(stow_app PRIVATE stow_archive "-framework Cocoa")
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

    add_executable(stow_app src/linux/main.cpp)
    set_target_properties(stow_app PROPERTIES OUTPUT_NAME stow)
    target_link_libraries(stow_app PRIVATE stow_archive PkgConfig::GTK4)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

if(NOT WIN32)
    add_executable(stow_cli src/cli.cpp)
    set_target_properties(stow_cli PROPERTIES OUTPUT_NAME stow-cli)
    target_link_libraries(stow_cli PRIVATE stow_archive)
endif()
