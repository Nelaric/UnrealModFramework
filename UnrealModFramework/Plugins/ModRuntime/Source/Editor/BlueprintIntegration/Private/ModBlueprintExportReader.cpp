// Copyright (c) 2026 Nelaric Contributors
#include "ModBlueprintExportReader.h"

#include "ModBlueprintExports.h"

namespace Mod::BlueprintIntegration
{
TArray<FExportedAPI> ReadAPIs(const UBlueprint& Blueprint)
{
	TArray<FExportedAPI> APIs;
	if (const auto* Exports = UModBlueprintExports::Find(&Blueprint))
	{
		for (const UModBlueprintAPIExport* Entry : Exports->APIs)
		{
			if (Entry)
			{
				APIs.Add({Entry->EntryGuid, Entry->PublicName, Entry->GetExportName(), Entry->ExportVersion,
				          Entry->Scope, Entry->Thread, Entry->FailureRule, Entry->Permissions});
			}
		}
	}
	return APIs;
}
} // namespace Mod::BlueprintIntegration
