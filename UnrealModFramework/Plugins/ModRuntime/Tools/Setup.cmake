# Copyright (c) 2026 Nelaric Contributors

cmake_minimum_required(VERSION 3.20)

file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}" _tools_dir)
set(_output_dir "${_tools_dir}/build")
if(IS_DIRECTORY "${_output_dir}")
    file(REAL_PATH "${_output_dir}" _output_dir)
endif()

function(_publish_tools release_dir stage_dir)
    file(LOCK "${_output_dir}/.setup.lock" GUARD PROCESS TIMEOUT 30)
    set(_suffix "")
    if(CMAKE_HOST_WIN32)
        set(_suffix .exe)
    endif()
    foreach(_tool IN ITEMS ModBuildTool ModSourceTool ModHeaderTool ModSDKTool)
        file(COPY "${release_dir}/${_tool}${_suffix}" DESTINATION "${stage_dir}/new/${_tool}")
    endforeach()
    file(COPY "${release_dir}/Licenses" DESTINATION "${stage_dir}/new/ModSDKTool")

    # Prepare the replacements and backups on the destination filesystem before updating any file.
    file(GLOB_RECURSE _files LIST_DIRECTORIES false RELATIVE "${stage_dir}/new" "${stage_dir}/new/*")
    foreach(_file IN LISTS _files)
        get_filename_component(_directory "${_file}" DIRECTORY)
        file(MAKE_DIRECTORY "${_output_dir}/${_directory}")
        if(EXISTS "${_output_dir}/${_file}")
            file(COPY "${_output_dir}/${_file}" DESTINATION "${stage_dir}/previous/${_directory}")
        endif()
    endforeach()

    set(_published)
    foreach(_file IN LISTS _files)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E rename "${stage_dir}/new/${_file}" "${_output_dir}/${_file}"
            RESULT_VARIABLE _result)
        if(NOT _result STREQUAL "0")
            list(REVERSE _published)
            foreach(_previous IN LISTS _published)
                if(EXISTS "${stage_dir}/previous/${_previous}")
                    execute_process(COMMAND "${CMAKE_COMMAND}" -E rename
                        "${stage_dir}/previous/${_previous}" "${_output_dir}/${_previous}"
                        RESULT_VARIABLE _restore_result)
                else()
                    execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -f "${_output_dir}/${_previous}"
                        RESULT_VARIABLE _restore_result)
                endif()
                if(NOT _restore_result STREQUAL "0")
                    message(WARNING "Could not restore ${_output_dir}/${_previous}")
                endif()
            endforeach()
            message(FATAL_ERROR "Could not publish ${_output_dir}/${_file}")
        endif()
        list(APPEND _published "${_file}")
    endforeach()
endfunction()

# Use a separate process for publication so the caller can clean up after any CMake error.
if(DEFINED SETUP_RELEASE_DIR)
    _publish_tools("${SETUP_RELEASE_DIR}" "${SETUP_STAGE_DIR}")
    return()
endif()

if(HELP)
    message([=[
Build and publish all four standalone ModRuntime tools in Release mode.

Windows:      Setup.bat [toolchain-directory] [--jobs count] [--generator name]
Linux/macOS:  sh Setup.sh [toolchain-directory] [--jobs count] [--generator name]

Requires CMake 3.20 or newer, MSVC and the Windows SDK on Windows, or GNU g++ on
Linux and macOS. On macOS, set CXX to your installed GNU compiler.
]=])
    return()
endif()
if((DEFINED TARGET AND NOT "${TARGET}" STREQUAL "all") OR DEFINED BUILD_DIR)
    message(FATAL_ERROR "Setup builds all tools in a temporary directory. See --help for options.")
endif()

if(CMAKE_HOST_WIN32)
    set(_temp_root "$ENV{TEMP}")
else()
    set(_temp_root "$ENV{TMPDIR}")
    if(_temp_root STREQUAL "")
        set(_temp_root /tmp)
    endif()
endif()
if(NOT _temp_root STREQUAL "")
    file(REAL_PATH "${_temp_root}" _temp_root)
    cmake_path(GET _temp_root ROOT_PATH _drive_root)
endif()
if(_temp_root STREQUAL "" OR _temp_root STREQUAL _drive_root)
    set(_temp_root "${CMAKE_CURRENT_LIST_DIR}/../../../../.tools/tmp")
    file(MAKE_DIRECTORY "${_temp_root}")
    file(REAL_PATH "${_temp_root}" _temp_root)
endif()

while(TRUE)
    string(RANDOM LENGTH 20 ALPHABET 0123456789abcdef _run_id)
    set(_work_dir "${_temp_root}/umf-setup-${_run_id}")
    set(_stage_dir "${_output_dir}/.setup-${_run_id}")
    if(NOT EXISTS "${_work_dir}" AND NOT EXISTS "${_stage_dir}")
        break()
    endif()
endwhile()
file(MAKE_DIRECTORY "${_work_dir}")

set(_build_options "-DBUILD_DIR=${_work_dir}/build" -DTARGET=all)
foreach(_option IN ITEMS TOOLCHAIN_DIR JOBS GENERATOR)
    if(DEFINED ${_option})
        list(APPEND _build_options "-D${_option}=${${_option}}")
    endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" ${_build_options} -P "${CMAKE_CURRENT_LIST_DIR}/Build.cmake"
    RESULT_VARIABLE _result)
if(_result STREQUAL "0")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DSETUP_RELEASE_DIR=${_work_dir}/build/bin/Release"
        "-DSETUP_STAGE_DIR=${_stage_dir}" -P "${CMAKE_CURRENT_LIST_FILE}" RESULT_VARIABLE _result)
endif()

set(_cleanup_failed OFF)
foreach(_directory IN ITEMS "${_stage_dir}" "${_work_dir}")
    if(EXISTS "${_directory}" OR IS_SYMLINK "${_directory}")
        file(REAL_PATH "${_directory}" _resolved_directory)
        if(_resolved_directory STREQUAL _directory)
            execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_directory}" RESULT_VARIABLE _cleanup_result)
            if(_cleanup_result STREQUAL "0")
                continue()
            endif()
        endif()
        message(WARNING "Could not clean up ${_directory}")
        set(_cleanup_failed ON)
    endif()
endforeach()

if(NOT _result STREQUAL "0" OR _cleanup_failed)
    message(FATAL_ERROR "Setup failed. See the output above.")
endif()
message(STATUS "ModRuntime tools are ready in ${_output_dir}/<tool>")
