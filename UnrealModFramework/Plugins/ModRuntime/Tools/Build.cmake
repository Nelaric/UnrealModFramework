# Copyright (c) 2026 Nelaric Contributors

cmake_minimum_required(VERSION 3.20)

if(HELP)
    message([=[
Build ModRuntime tools using command-line toolchains.

Windows:       Build.bat [toolchain-directory] [--target name] [--jobs count]
Linux/macOS:   sh Build.sh [toolchain-directory] [--target name] [--jobs count]
Any platform:  cmake [options] -P /path/to/Tools/Build.cmake
Wrappers also accept --build-dir directory and --generator name.

Options (place all -D options before -P):
  TOOLCHAIN_DIR  Optional compiler root or bin directory; takes precedence
                over environment variables. An invalid directory is an error.
  TARGET      all (default), ModRuntimeUhtTools, ModBuildTool, ModHeaderTool,
              ModSDKTool, or ModSourceTool. ModRuntimeUhtTools builds
              ModBuildTool, ModHeaderTool, and ModSourceTool together.
  JOBS        Positive parallel job count; defaults to the logical CPU count
              or CMAKE_BUILD_PARALLEL_LEVEL when set. NMake builds serially.
  GENERATOR   Ninja, Ninja Multi-Config, NMake Makefiles (Windows), or
              Unix Makefiles (Unix). Defaults to Ninja when available.
  BUILD_DIR   Defaults to build/.cmake with executables in build/<tool>.
              An override puts executables in <directory>/bin/Release.
              Relative overrides are based on the Tools directory.
  CXX         Optional compiler executable when TOOLCHAIN_DIR is not supplied
  HELP        ON to display this help

Builds Release only. Requires CMake >= 3.20 and MSVC + Windows SDK on Windows,
or GCC on Unix. Without a directory, search CXX, TOOLCHAIN_DIR and the platform's
common compiler environment variables, then PATH. No install path is assumed.
Windows: MSVC_ROOT, VCToolsInstallDir, VCINSTALLDIR, VSINSTALLDIR.
Unix: GCC_ROOT, GCC_HOME. On macOS CXX must select an installed GNU compiler.
Omit the directory argument to use environment discovery.
]=])
    return()
endif()

if(DEFINED CONFIG AND NOT CONFIG STREQUAL "Release")
    message(FATAL_ERROR "This script builds Release only.")
endif()
set(CONFIG Release)
if(NOT DEFINED TARGET)
    set(TARGET all)
endif()
if(NOT "${TARGET}" MATCHES "^(all|ModRuntimeUhtTools|ModBuildTool|ModHeaderTool|ModSDKTool|ModSourceTool)$")
    message(FATAL_ERROR "Invalid TARGET '${TARGET}'. See -DHELP=ON for supported targets.")
endif()
if(NOT DEFINED JOBS)
    if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL} AND NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
        set(JOBS "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
    else()
        cmake_host_system_information(RESULT JOBS QUERY NUMBER_OF_LOGICAL_CORES)
    endif()
endif()
if(NOT JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "JOBS must be a positive integer.")
endif()

if(CMAKE_HOST_WIN32)
    set(_compiler_id MSVC)
    set(_compiler_name cl.exe)
    set(_fallback_generator "NMake Makefiles")
else()
    set(_compiler_id GNU)
    set(_compiler_name g++)
    set(_fallback_generator "Unix Makefiles")
endif()

function(_find_compiler_in_directory directory output)
    set(${output} "" PARENT_SCOPE)
    if(NOT IS_DIRECTORY "${directory}")
        return()
    endif()
    set(_paths "${directory}" "${directory}/bin")
    if(CMAKE_HOST_WIN32)
        set(_target_arch "$ENV{VSCMD_ARG_TGT_ARCH}")
        if(_target_arch STREQUAL "")
            set(_target_arch x64)
            if("$ENV{PROCESSOR_ARCHITECTURE}" STREQUAL "ARM64")
                set(_target_arch arm64)
            elseif("$ENV{PROCESSOR_ARCHITECTURE}" STREQUAL "x86")
                set(_target_arch x86)
            endif()
        endif()
        set(_host_arch "$ENV{VSCMD_ARG_HOST_ARCH}")
        if(_host_arch STREQUAL "")
            set(_host_arch "${_target_arch}")
        endif()
        list(APPEND _paths
            "${directory}/bin/Host${_host_arch}/${_target_arch}"
        )
        file(GLOB _native_msvc_bins LIST_DIRECTORIES true
            "${directory}/Tools/MSVC/*/bin/Host${_host_arch}/${_target_arch}"
            "${directory}/VC/Tools/MSVC/*/bin/Host${_host_arch}/${_target_arch}"
        )
        list(SORT _native_msvc_bins COMPARE NATURAL ORDER DESCENDING)
        list(APPEND _paths ${_native_msvc_bins})
        file(GLOB _msvc_bins LIST_DIRECTORIES true
            "${directory}/bin/Host*/${_target_arch}"
            "${directory}/Tools/MSVC/*/bin/Host*/${_target_arch}"
            "${directory}/VC/Tools/MSVC/*/bin/Host*/${_target_arch}"
        )
        list(SORT _msvc_bins COMPARE NATURAL ORDER DESCENDING)
        list(APPEND _paths ${_msvc_bins})
    endif()
    unset(_directory_compiler CACHE)
    find_program(_directory_compiler NAMES "${_compiler_name}" PATHS ${_paths} NO_DEFAULT_PATH)
    if(NOT _directory_compiler AND NOT CMAKE_HOST_WIN32)
        file(GLOB _versioned_gxx LIST_DIRECTORIES false "${directory}/g++-[0-9]*" "${directory}/bin/g++-[0-9]*")
        list(SORT _versioned_gxx COMPARE NATURAL ORDER DESCENDING)
        if(_versioned_gxx)
            unset(_directory_compiler CACHE)
            find_program(_directory_compiler NAMES ${_versioned_gxx} NO_DEFAULT_PATH)
        endif()
    endif()
    if(_directory_compiler)
        set(${output} "${_directory_compiler}" PARENT_SCOPE)
    endif()
    unset(_directory_compiler CACHE)
endfunction()

if(DEFINED TOOLCHAIN_DIR AND NOT TOOLCHAIN_DIR STREQUAL "")
    get_filename_component(TOOLCHAIN_DIR "${TOOLCHAIN_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    _find_compiler_in_directory("${TOOLCHAIN_DIR}" _compiler)
    if(NOT _compiler)
        message(FATAL_ERROR "No ${_compiler_id} compiler was found in TOOLCHAIN_DIR: ${TOOLCHAIN_DIR}")
    endif()
else()
    if(DEFINED CXX AND NOT CXX STREQUAL "")
        find_program(_compiler NAMES "${CXX}")
        if(NOT _compiler)
            message(FATAL_ERROR "The requested CXX compiler was not found: ${CXX}")
        endif()
    elseif(DEFINED ENV{CXX} AND NOT "$ENV{CXX}" STREQUAL "")
        find_program(_compiler NAMES "$ENV{CXX}")
    endif()
    set(_root_variables TOOLCHAIN_DIR)
    if(CMAKE_HOST_WIN32)
        list(APPEND _root_variables MSVC_ROOT VCToolsInstallDir VCINSTALLDIR VSINSTALLDIR)
    else()
        list(APPEND _root_variables GCC_ROOT GCC_HOME)
    endif()
    foreach(_variable IN LISTS _root_variables)
        if(NOT _compiler AND DEFINED ENV{${_variable}} AND NOT "$ENV{${_variable}}" STREQUAL "")
            _find_compiler_in_directory("$ENV{${_variable}}" _compiler)
        endif()
    endforeach()
    if(NOT _compiler)
        unset(_compiler CACHE)
        find_program(_compiler NAMES "${_compiler_name}")
    endif()
    if(NOT _compiler)
        message(FATAL_ERROR
            "No ${_compiler_id} compiler was found. Set CXX, TOOLCHAIN_DIR, a compiler root "
            "environment variable, or PATH; alternatively pass a toolchain directory.")
    endif()
endif()

get_filename_component(_compiler_bin "${_compiler}" DIRECTORY)
if(CMAKE_HOST_WIN32)
    set(ENV{PATH} "${_compiler_bin};$ENV{PATH}")
else()
    set(ENV{PATH} "${_compiler_bin}:$ENV{PATH}")
endif()
set(_release_tool_output_root "")
if(NOT DEFINED BUILD_DIR)
    set(BUILD_DIR "build/.cmake")
    set(_release_tool_output_root "${CMAKE_CURRENT_LIST_DIR}/build")
endif()
get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
if(BUILD_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
    message(FATAL_ERROR "BUILD_DIR must be separate from the Tools source directory.")
endif()
file(LOCK "${BUILD_DIR}/.build.lock" GUARD PROCESS)
if(EXISTS "${BUILD_DIR}/CMakeCache.txt")
    file(STRINGS "${BUILD_DIR}/CMakeCache.txt" _cached_compiler REGEX "^CMAKE_CXX_COMPILER:[^=]*=")
    string(REGEX REPLACE "^CMAKE_CXX_COMPILER:[^=]*=" "" _cached_compiler "${_cached_compiler}")
    get_filename_component(_requested_compiler "${_compiler}" REALPATH)
    get_filename_component(_cached_compiler "${_cached_compiler}" REALPATH)
    if(CMAKE_HOST_WIN32)
        string(TOLOWER "${_requested_compiler}" _requested_compiler)
        string(TOLOWER "${_cached_compiler}" _cached_compiler)
    endif()
    if(NOT _requested_compiler STREQUAL _cached_compiler)
        message(FATAL_ERROR "BUILD_DIR belongs to another compiler. Select an unused BUILD_DIR for this toolchain.")
    endif()
endif()

if(NOT DEFINED GENERATOR)
    if(DEFINED ENV{CMAKE_GENERATOR} AND NOT "$ENV{CMAKE_GENERATOR}" STREQUAL "")
        set(GENERATOR "$ENV{CMAKE_GENERATOR}")
    elseif(EXISTS "${BUILD_DIR}/CMakeCache.txt")
        file(STRINGS "${BUILD_DIR}/CMakeCache.txt" _cached_generator REGEX "^CMAKE_GENERATOR:INTERNAL=")
        string(REPLACE "CMAKE_GENERATOR:INTERNAL=" "" GENERATOR "${_cached_generator}")
    else()
        find_program(_ninja NAMES ninja ninja-build)
        if(_ninja)
            set(GENERATOR Ninja)
        else()
            set(GENERATOR "${_fallback_generator}")
        endif()
    endif()
endif()

if(GENERATOR STREQUAL "Ninja" OR GENERATOR STREQUAL "Ninja Multi-Config")
    find_program(_build_tool NAMES ninja ninja-build)
elseif(CMAKE_HOST_WIN32 AND GENERATOR STREQUAL "NMake Makefiles")
    find_program(_build_tool NAMES nmake)
elseif(NOT CMAKE_HOST_WIN32 AND GENERATOR STREQUAL "Unix Makefiles")
    find_program(_build_tool NAMES gmake make)
else()
    message(FATAL_ERROR "Unsupported GENERATOR '${GENERATOR}'. Select a command-line generator; see -DHELP=ON.")
endif()
if(NOT _build_tool)
    message(FATAL_ERROR "The build tool for '${GENERATOR}' was not found on PATH.")
endif()

set(_configure_command "${CMAKE_COMMAND}"
    -S "${CMAKE_CURRENT_LIST_DIR}"
    -B "${BUILD_DIR}"
    -G "${GENERATOR}"
    "-DCMAKE_BUILD_TYPE:STRING=${CONFIG}"
    "-DCMAKE_MAKE_PROGRAM:FILEPATH=${_build_tool}"
    "-DUMF_EXPECTED_COMPILER_ID:STRING=${_compiler_id}"
    "-DUMF_RELEASE_TOOL_OUTPUT_ROOT:PATH=${_release_tool_output_root}"
)
list(APPEND _configure_command "-DCMAKE_CXX_COMPILER:FILEPATH=${_compiler}")
if(GENERATOR STREQUAL "Ninja Multi-Config")
    list(APPEND _configure_command "-DCMAKE_CONFIGURATION_TYPES:STRING=Release")
elseif(GENERATOR STREQUAL "NMake Makefiles")
    # CMake detects NMake's UTF-8 makefile support from its English version banner.
    # Without the BOM, NMake can misread the localized /showIncludes filter prefix.
    execute_process(COMMAND "${_build_tool}" -? OUTPUT_QUIET ERROR_VARIABLE _nmake_banner TIMEOUT 5)
    if(NOT _nmake_banner MATCHES "Program Maintenance Utility Version")
        list(APPEND _configure_command "-DCMAKE_DEPENDS_USE_COMPILER:BOOL=OFF")
    endif()
endif()

message(STATUS "Toolchain: ${_compiler_id}; generator: ${GENERATOR}; configuration: ${CONFIG}")
message(STATUS "Compiler: ${_compiler}")
message(STATUS "Build directory: ${BUILD_DIR}")
execute_process(COMMAND ${_configure_command} RESULT_VARIABLE _configure_result)
if(NOT _configure_result STREQUAL "0")
    message(FATAL_ERROR "Configuration failed (${_configure_result}). If changing toolchains, use a new BUILD_DIR.")
endif()
if(NOT GENERATOR STREQUAL "Ninja Multi-Config")
    file(STRINGS "${BUILD_DIR}/CMakeCache.txt" _build_type REGEX "^CMAKE_BUILD_TYPE:[^=]*=")
    string(REGEX REPLACE "^CMAKE_BUILD_TYPE:[^=]*=" "" _build_type "${_build_type}")
    if(NOT _build_type STREQUAL "Release")
        message(FATAL_ERROR "The configured build type is not Release; refusing to build.")
    endif()
endif()

set(_build_command "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --config "${CONFIG}")
if(NOT "${TARGET}" STREQUAL "all")
    list(APPEND _build_command --target "${TARGET}")
endif()
if(GENERATOR STREQUAL "NMake Makefiles")
    message(STATUS "NMake builds serially; install Ninja for parallel builds.")
else()
    list(APPEND _build_command --parallel "${JOBS}")
endif()
if(_release_tool_output_root)
    # Setup publishes to the same executable directories.
    file(LOCK "${_release_tool_output_root}/.setup.lock" GUARD PROCESS)
endif()
execute_process(COMMAND ${_build_command} RESULT_VARIABLE _build_result)
if(NOT _build_result STREQUAL "0")
    message(FATAL_ERROR "Build failed (${_build_result}).")
endif()
if(NOT _release_tool_output_root)
    set(_executables "${BUILD_DIR}/bin/${CONFIG}")
elseif("${TARGET}" STREQUAL "all" OR "${TARGET}" STREQUAL "ModRuntimeUhtTools")
    set(_executables "${_release_tool_output_root}/<tool>")
else()
    set(_executables "${_release_tool_output_root}/${TARGET}")
endif()
message(STATUS "Build succeeded. Executables: ${_executables}")
