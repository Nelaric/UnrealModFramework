// Copyright (c) 2026 Nelaric Contributors
#include "GenerationDestination.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"

#include <filesystem>
#include <stdexcept>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

fs::path GenerationDestination::ResolveOutputRoot(const GenerationModel& Model, const fs::path& OutputDirectory)
{
	const fs::path Root = fs::absolute(OutputDirectory).lexically_normal();
	for (const auto& Dependency : Model.Dependencies)
	{
		const auto Relative = Dependency.lexically_relative(Root);
		if (!Relative.empty() && *Relative.begin() != "..")
		{
			throw std::runtime_error("Generation output must not contain an input: " +
			                         DiagnosticLocation::QuoteDiagnosticPath(Dependency));
		}
	}
	return Root;
}
} // namespace Mod::HeaderTool
