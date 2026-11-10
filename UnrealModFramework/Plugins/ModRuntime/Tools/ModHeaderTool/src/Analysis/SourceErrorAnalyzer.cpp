// Copyright (c) 2026 Nelaric Contributors
#include "SourceErrorAnalyzer.h"

#include "Common/Errors/ErrorCodes.h"
#include "Common/Process/Process.h"
#include "Common/Process/ProcessEnvironment.h"
#include "ModHeaderTool/src/Analysis/SourceReport.h"
#include "ModHeaderTool/src/Analysis/SourceReportBinder.h"
#include "ModHeaderTool/src/Analysis/SourceToolInvocation.h"
#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"
#include "ModHeaderTool/src/Hash/Hash.h"
#include "ModHeaderTool/src/Project/Project.h"

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void SourceErrorAnalyzer::AnalyzeSourceErrors(GenerationModel& Model, const fs::path& AnalysisDatabase,
                                              const fs::path& ClangLibrary, const fs::path& SourceTool)
{
	Model.SourceErrorCatalog = fs::weakly_canonical(Model.Input.Project.Root / "Config" / "ModSourceErrorCodes.json");
	Model.Dependencies.insert(Model.SourceErrorCatalog);
	if (!fs::exists(Model.SourceErrorCatalog))
	{
		return;
	}
	const auto Bytes = TextFile::ReadTextFile(Model.SourceErrorCatalog);
	Model.SourceErrorCatalogHash = Hash::HashBytes(Bytes);
	const auto Catalog = Json::parse(Bytes);
	const auto& Members = Catalog.at("members");
	const auto ClassifiedReasons = Catalog.value("classifiedReasons", Json::object());
	const auto& Version = Catalog.at("schemaVersion");
	if (Catalog.value("format", "") != "UMF.SourceErrorCatalog" ||
	    (Version != 1 && Version != Tools::ErrorCodes::StructuredErrorCodeVersion) || !Members.is_object())
	{
		throw std::runtime_error("Unsupported source error catalog: " +
		                         DiagnosticLocation::QuoteDiagnosticPath(Model.SourceErrorCatalog));
	}
	Model.ErrorCodeEncodingVersion = Version.get<int>();
	if (std::none_of(Model.APIs.begin(), Model.APIs.end(), [&](const auto& API) { return Members.contains(API.Id); }))
	{
		return;
	}
	const auto Tool = SourceToolInvocation::ResolveSourceTool(SourceTool);
	const auto Common = SourceToolInvocation::SourceArguments(Model, AnalysisDatabase, ClangLibrary);
	const auto Environment = Mod::BuildTool::ProcessEnvironment::CaptureEnvironment();
	Model.Dependencies.insert(Tool);
	Model.Dependencies.insert(fs::canonical(ClangLibrary));
	for (auto& API : Model.APIs)
	{
		const auto Member = Members.find(API.Id);
		if (Member == Members.end())
		{
			continue;
		}
		const SourceReport Report;
		auto Arguments = Common;
		Arguments.insert(Arguments.end(), {"--member", API.Id, "--output", Path::PathToUtf8(Report.File)});
		const int Exit = Mod::BuildTool::Process::RunProcess(Tool, Arguments, Model.Input.Project.Root, Environment);
		if (Exit != 0)
		{
			throw std::runtime_error("ModSourceTool failed for " + API.Id + " (exit code " + std::to_string(Exit) +
			                         ").");
		}
		SourceReportBinder::BindSourceErrors(API, Model, *Member, ClassifiedReasons,
		                                     Json::parse(TextFile::ReadTextFile(Report.File)), AnalysisDatabase);
	}
}
} // namespace Mod::HeaderTool
