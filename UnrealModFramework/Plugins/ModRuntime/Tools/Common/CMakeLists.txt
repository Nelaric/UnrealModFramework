# Copyright (c) 2026 Nelaric Contributors

add_library(ModToolSupport STATIC
    Errors/ErrorCodes.cpp
    Errors/ErrorCodes.h
    Errors/PublicStatuses.cpp
    Errors/PublicStatuses.h
    Files/BuildLock.cpp
    Files/BuildLock.h
    Files/Executable.cpp
    Files/Executable.h
    Files/File.cpp
    Files/File.h
    Files/Path.cpp
    Files/Path.h
    Files/PathName.cpp
    Files/PathName.h
    Files/TemporaryName.cpp
    Files/TemporaryName.h
    Files/WatchedFiles.cpp
    Files/WatchedFiles.h
    Hash/Hash.cpp
    Hash/Hash.h
    Hash/Sha256.cpp
    Hash/Sha256.h
    Process/EnvironmentBlock.cpp
    Process/EnvironmentBlock.h
    Process/EnvironmentKeyLess.cpp
    Process/EnvironmentKeyLess.h
    Process/Handle.cpp
    Process/Handle.h
    Process/Process.cpp
    Process/Process.h
    Process/ProcessArguments.cpp
    Process/ProcessArguments.h
    Process/ProcessCancellation.cpp
    Process/ProcessCancellation.h
    Process/ProcessEnvironment.cpp
    Process/ProcessEnvironment.h
    Process/ProcessOutput.cpp
    Process/ProcessOutput.h
    Process/ResponseFile.cpp
    Process/ResponseFile.h
)

target_include_directories(ModToolSupport PUBLIC ..)
target_compile_features(ModToolSupport PUBLIC cxx_std_20)
set_target_properties(ModToolSupport PROPERTIES CXX_EXTENSIONS NO)

if(WIN32)
    target_compile_definitions(ModToolSupport PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MSVC)
    target_compile_options(ModToolSupport PRIVATE /W4 /permissive- /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ModToolSupport PRIVATE -Wall -Wextra -Wpedantic)
endif()
