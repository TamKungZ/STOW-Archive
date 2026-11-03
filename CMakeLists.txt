cmake_minimum_required(VERSION 3.21)

project(STOWArchive VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
    set(STOW_WINDOWS_ICON_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/stow_icon.rc")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/windows/app.rc.in"
        "${STOW_WINDOWS_ICON_RESOURCE}"
        @ONLY
    )

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(STOW_INSTALLER_ARCH x64)
        set(_stow_redist_arch x64)
    else()
        set(STOW_INSTALLER_ARCH x86)
        set(_stow_redist_arch x86)
    endif()

    if(MSVC)
        get_filename_component(_msvc_target_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_msvc_version_dir
            "${_msvc_target_bin}/../../.." ABSOLUTE
        )
        get_filename_component(_vc_root "${_msvc_version_dir}/../../.." ABSOLUTE)
        file(GLOB _stow_vcruntime_candidates
            "${_vc_root}/Redist/MSVC/*/${_stow_redist_arch}/Microsoft.VC*CRT/vcruntime140.dll"
        )
        if(_stow_vcruntime_candidates)
            list(SORT _stow_vcruntime_candidates)
            list(GET _stow_vcruntime_candidates -1 STOW_VCRUNTIME_DLL)
            message(STATUS "Using app-local VC runtime: ${STOW_VCRUNTIME_DLL}")
        else()
            message(WARNING "Could not locate the MSVC vcruntime140.dll redistributable.")
        endif()
    endif()

    add_executable(stow_app WIN32
        src/windows/main.cpp
        "${STOW_WINDOWS_ICON_RESOURCE}"
    )
    set_target_properties(stow_app PROPERTIES OUTPUT_NAME stow)
    target_link_libraries(stow_app PRIVATE stow_archive user32 gdi32 comdlg32 shell32)
    add_custom_command(TARGET stow_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${STOW_CABI_RUNTIME}"
            "$<TARGET_FILE_DIR:stow_app>/stow.dll"
        VERBATIM
    )
    if(STOW_VCRUNTIME_DLL)
        add_custom_command(TARGET stow_app POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${STOW_VCRUNTIME_DLL}"
                "$<TARGET_FILE_DIR:stow_app>/vcruntime140.dll"
            VERBATIM
        )
    endif()

    add_executable(stow_cli
        src/windows/cli.cpp
        "${STOW_WINDOWS_ICON_RESOURCE}"
    )
    set_target_properties(stow_cli PROPERTIES OUTPUT_NAME stow-cli)
    target_link_libraries(stow_cli PRIVATE stow_archive advapi32 shell32)
    add_custom_command(TARGET stow_cli POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${STOW_CABI_RUNTIME}"
            "$<TARGET_FILE_DIR:stow_cli>/stow.dll"
        VERBATIM
    )
    if(STOW_VCRUNTIME_DLL)
        add_custom_command(TARGET stow_cli POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${STOW_VCRUNTIME_DLL}"
                "$<TARGET_FILE_DIR:stow_cli>/vcruntime140.dll"
            VERBATIM
        )
    endif()

    if(STOW_VCRUNTIME_DLL)
        find_program(NSIS_MAKENSIS_EXECUTABLE
            NAMES makensis
            HINTS "C:/Program Files (x86)/NSIS" "$ENV{ProgramFiles}/NSIS"
        )
    endif()
    if(NSIS_MAKENSIS_EXECUTABLE AND STOW_VCRUNTIME_DLL)
        set(STOW_INSTALLER_OUTPUT
            "${CMAKE_BINARY_DIR}/installers/STOW-Archive-Setup-${PROJECT_VERSION}-${STOW_INSTALLER_ARCH}.exe"
        )
        add_custom_target(stow_installer
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/installers"
            COMMAND "${NSIS_MAKENSIS_EXECUTABLE}"
                "/DAPP_DIR=$<TARGET_FILE_DIR:stow_app>"
                "/DAPP_VERSION=${PROJECT_VERSION}"
                "/DAPP_ARCH=${STOW_INSTALLER_ARCH}"
                "/DOUTPUT_FILE=${STOW_INSTALLER_OUTPUT}"
                "${CMAKE_CURRENT_SOURCE_DIR}/installer/stow.nsi"
            DEPENDS stow_app stow_cli
            BYPRODUCTS "${STOW_INSTALLER_OUTPUT}"
            VERBATIM
        )
    elseif(NOT NSIS_MAKENSIS_EXECUTABLE)
        message(STATUS "NSIS not found; the stow_installer target is unavailable.")
    else()
        message(STATUS "MSVC runtime redistributable not found; the stow_installer target is unavailable.")
    endif()
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
