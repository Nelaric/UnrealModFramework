# Copyright (c) 2026 Nelaric Contributors

include("${RESOURCE_DIRECTORY}/Files.cmake")
list(SORT development_editor_files)

set(source [=[// Copyright (c) 2026 Nelaric Contributors
// Generated from the DevelopmentEditor resources by CMake.
#include "ModSDKTool/src/Editor/DevelopmentEditorResources.h"

namespace Mod::SdkTool
{
namespace
{
]=])
set(resource_table "")
set(index 0)
string(REPEAT "0x[0-9a-f][0-9a-f], " 16 row_pattern)
foreach(relative_path IN LISTS development_editor_files)
    file(READ "${RESOURCE_DIRECTORY}/${relative_path}" hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1, " bytes "${hex}")
    string(REGEX REPLACE "(${row_pattern})" "\\1\n    " bytes "${bytes}")
    set(symbol "Resource${index}")
    # The trailing byte gives even an empty resource a valid address.
    string(APPEND source "constexpr unsigned char ${symbol}[] = {\n    ${bytes}0x00\n};\n")
    string(APPEND resource_table "    {\"${relative_path}\", ${symbol}, sizeof(${symbol}) - 1},\n")
    math(EXPR index "${index} + 1")
endforeach()

string(APPEND source "constexpr EmbeddedResource Resources[] = {\n${resource_table}};\n")
string(APPEND source [=[} // namespace

std::span<const EmbeddedResource> GetDevelopmentEditorResources()
{
    return Resources;
}
} // namespace Mod::SdkTool
]=])
file(WRITE "${OUTPUT_FILE}.tmp" "${source}")
file(RENAME "${OUTPUT_FILE}.tmp" "${OUTPUT_FILE}")
