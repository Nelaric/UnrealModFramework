# Copyright (c) 2026 Nelaric Contributors

set -euo pipefail

engine=/home/ue4/UnrealEngine/Engine
source_root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
tools="$source_root/UnrealModFramework/Plugins/ModRuntime/Tools"
tool_build=/tmp/umf-tools
plugin_root=UnrealModFramework/SourcePlugins
plugin=ModDevelopmentSDKEditor
if [[ ! -f "$plugin_root/$plugin/$plugin.uplugin" ]]; then
    plugin=ModDevelopmentEditor
fi

dotnet_hosts=("$engine"/Binaries/ThirdParty/DotNet/*/linux-x64/dotnet)
if (( ${#dotnet_hosts[@]} != 1 )) || [[ ! -x "${dotnet_hosts[0]}" ]]; then
    echo "Expected one bundled Linux x64 .NET host in $engine/Binaries/ThirdParty/DotNet." >&2
    exit 1
fi
export DOTNET_ROOT="${dotnet_hosts[0]%/*}"
export PATH="$DOTNET_ROOT:$PATH"

cmake -S "$tools" -B "$tool_build" -DCMAKE_BUILD_TYPE=Release -DUMF_EXPECTED_COMPILER_ID=GNU
cmake --build "$tool_build" --target ModSDKTool ModBuildTool --parallel "$(nproc)"

exec "$tool_build/bin/Release/ModSDKTool" build-editor \
    --plugin "$plugin_root/$plugin/$plugin.uplugin" \
    --package "Dist/$plugin" \
    --dotnet "${dotnet_hosts[0]}" \
    --automation-tool "$engine/Binaries/DotNET/AutomationTool/AutomationTool.dll" \
    --editor-modules "$engine/Binaries/Linux/UnrealEditor.modules" \
    --architecture x64 \
    --mbt "$tool_build/bin/Release/ModBuildTool"
