// Copyright (c) 2026 Nelaric Contributors
#include "SourceToolInvocation.h"

#include "Common/Files/Executable.h"
#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

fs::path SourceToolInvocation::ResolveSourceTool(const fs::path& Configured)
{
#ifdef _WIN32
	constexpr const char* Name = "ModSourceTool.exe";
#else
	constexpr const char* Name = "ModSourceTool";
#endif
	auto Path = Configured;
	if (Path.empty())
	{
		const auto Directory = Mod::BuildTool::Executable::ExecutablePath().parent_path();
		Path = Directory / Name;
		if (!fs::is_regular_file(Path) && Directory.filename() == "ModHeaderTool")
		{
			Path = Directory.parent_path() / "ModSourceTool" / Name;
		}
	}
	if (!fs::is_regular_file(Path))
	{
		throw std::runtime_error("Build ModSourceTool for source error analysis: " +
		                         DiagnosticLocation::QuoteDiagnosticPath(Path));
	}
	return fs::canonical(Path);
}

std::vector<std::string> SourceToolInvocation::SourceArguments(const GenerationModel& Model, const fs::path& Database,
                                                               const fs::path& ClangLibrary)
{
	if (ClangLibrary.empty())
	{
		throw std::runtime_error("Source error analysis requires MHT --libclang.");
	}
	std::vector<std::string> Result{"analyze-errors",
	                                "--compdb",
	                                Path::PathToUtf8(Database),
	                                "--target",
	                                Model.Input.Target,
	                                "--catalog",
	                                Path::PathToUtf8(Model.SourceErrorCatalog),
	                                "--libclang",
	                                Path::PathToUtf8(fs::canonical(ClangLibrary))};
	const auto& Arguments = Model.Input.AnalysisArguments;
	constexpr std::string_view ResourceOption = "-resource-dir";
	fs::path ResourceDirectory;
	for (std::size_t I = 0; I < Arguments.size(); ++I)
	{
		const std::string_view Argument = Arguments[I];
		if (Argument == ResourceOption)
		{
			ResourceDirectory = Path::PathFromUtf8(Arguments.at(++I));
		}
		else if (Argument.starts_with("-resource-dir="))
		{
			ResourceDirectory = Path::PathFromUtf8(Argument.substr(ResourceOption.size() + 1));
		}
		else
		{
			Result.insert(Result.end(), {"--analysis-arg", Arguments[I]});
		}
	}
	if (!ResourceDirectory.is_absolute())
	{
		throw std::runtime_error("Source error analysis requires an absolute -resource-dir in analysisArguments.");
	}
	Result.insert(Result.end(), {"--resource-dir", Path::PathToUtf8(fs::canonical(ResourceDirectory))});
	return Result;
}
} // namespace Mod::HeaderTool
