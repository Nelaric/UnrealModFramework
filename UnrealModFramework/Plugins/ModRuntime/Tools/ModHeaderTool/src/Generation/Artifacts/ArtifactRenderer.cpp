// Copyright (c) 2026 Nelaric Contributors
#include "ArtifactRenderer.h"

#include "ModHeaderTool/src/Generation/Abi/NativeABIGenerator.h"
#include "ModHeaderTool/src/Generation/Artifacts/GeneratedSource.h"
#include "ModHeaderTool/src/Generation/Artifacts/HostBridgeWriter.h"
#include "ModHeaderTool/src/Generation/Artifacts/LifecycleExport.h"
#include "ModHeaderTool/src/Generation/Artifacts/Projection.h"
#include "ModHeaderTool/src/Generation/Artifacts/ProjectionWriter.h"
#include "ModHeaderTool/src/Generation/Artifacts/SdkViewWriter.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"

#include <utility>

namespace Mod::HeaderTool
{
GeneratedBatch ArtifactRenderer::RenderArtifacts(GenerationModel& Model)
{
	auto Projection = ProjectionWriter::BuildProjection(Model);
	GeneratedBatch Batch;
	SdkViewWriter::WriteSdk(Model, Batch.Files);
	HostBridgeWriter::WriteHost(Model, Batch.Files);
	Batch.NativeABI =
	    NativeABIGenerator::WriteNativeABI(Model, Batch.Files, Projection.ContractHash, Projection.ProjectionHash,
	                                       Projection.Lifecycle, Projection.ErrorHeader);
	for (auto& [Path, Text] : Batch.Files)
	{
		if (Path.ends_with(".h") || Path.ends_with(".cpp"))
		{
			Text = GeneratedSource::SourceBytes(Text);
		}
	}
	if (!Projection.Lifecycle.is_null())
	{
		Batch.Files[LifecycleExport::LifecyclePath] = std::move(Projection.LifecycleExport);
	}
	Batch.Files["Contract/" + Model.Input.GameId + ".modcontract.json"] = Projection.Contract.dump(2) + '\n';
	Batch.Files["Diagnostics/" + Model.Input.GameId + ".sources.json"] = Projection.Sources.dump(2) + '\n';
	Batch.ErrorHeader = std::move(Projection.ErrorHeader);
	Batch.ContractHash = std::move(Projection.ContractHash);
	Batch.ProjectionHash = std::move(Projection.ProjectionHash);
	return Batch;
}
} // namespace Mod::HeaderTool
