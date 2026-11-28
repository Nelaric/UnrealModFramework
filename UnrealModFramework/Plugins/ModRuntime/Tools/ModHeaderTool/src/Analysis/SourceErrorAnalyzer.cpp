// Copyright (c) 2026 Nelaric Contributors
#include "SourceErrorAnalyzer.h"

#include "Common/Files/File.h"
#include "Common/Process/Process.h"
#include "Common/Process/ProcessEnvironment.h"
#include "ModHeaderTool/src/Analysis/SourceReport.h"
#include "ModHeaderTool/src/Analysis/SourceReportBinder.h"
#include "ModHeaderTool/src/Analysis/SourceToolInvocation.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Metadata/MetadataField.h"

#include <algorithm>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
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
	Json Members = Json::array();
	std::map<std::string, API*> Selected;
	for (auto& API : Model.APIs)
	{
		if (API.Blueprint || API.bFullViewMember)
		{
			continue;
		}
		const auto Field = std::ranges::find(API.Macro.Fields, "FailureReturn", &MetadataField::Name);
		const auto Sentinel = Field == API.Macro.Fields.end() ? std::string{} : Field->Value.Text;
		if (API.TranslationUnit.empty())
		{
			throw std::runtime_error("No target compile command for exported API: " + API.Id);
		}
		fs::path File = API.DefinitionFile;
		fs::path Unit = API.TranslationUnit;
		if (File.empty())
		{
			const auto It = Model.NativeMethodUnits.find(API.DeclarationUsr);
			if (It != Model.NativeMethodUnits.end() && It->second.size() == 1)
			{
				File = Unit = *It->second.begin();
			}
			else
			{
				File = API.Macro.File;
			}
		}
		Json Member{{"id", API.Id},
		            {"module", API.Module},
		            {"source", Path::PathToUtf8(File)},
		            {"translationUnit", Path::PathToUtf8(Unit)},
		            {"function", API.DeclaringType + "::" + API.Name},
		            {"signature", API.SourceSignature.substr(API.SourceSignature.find(" : ") + 3)},
		            {"usr", API.DeclarationUsr}};
		if (!Sentinel.empty())
		{
			auto Value = Json::parse(Sentinel, nullptr, false);
			if (!Value.is_boolean() && !Value.is_number_integer() && !Value.is_null())
			{
				throw std::runtime_error("FailureReturn must declare a boolean, integer, or null: " + API.Id);
			}
			Member["failureReturn"] = std::move(Value);
		}
		Members.push_back(std::move(Member));
		Selected.emplace(API.Id, &API);
	}
	if (Members.empty())
	{
		return;
	}

	const auto Tool = SourceToolInvocation::ResolveSourceTool(SourceTool);
	auto Request = SourceToolInvocation::SourceRequest(Model, AnalysisDatabase, ClangLibrary);
	Request["members"] = std::move(Members);
	Model.Dependencies.insert(Tool);
	Model.Dependencies.insert(fs::canonical(ClangLibrary));
	const auto Started = std::chrono::steady_clock::now();
	const SourceReport Report(Model.Input.File.parent_path());
	Mod::BuildTool::File::WriteFileAtomically(Report.Request, Request.dump(2) + "\n");
	const int Exit = Mod::BuildTool::Process::RunProcess(
	    Tool, {"analyze-errors", Path::PathToUtf8(Report.Request)}, Model.Input.Project.Root,
	    Mod::BuildTool::ProcessEnvironment::CaptureEnvironment(), Report.File);
	if (Exit != 0)
	{
		throw std::runtime_error("ModSourceTool failed source error analysis (exit code " + std::to_string(Exit) +
		                         ").");
	}
	const auto Output = Json::parse(TextFile::ReadTextFile(Report.File));
	if (Output.value("format", "") != "UMF.SourceAnalysisReport" || Output.value("schemaVersion", 0) != 2 ||
	    !Output.at("members").is_array())
	{
		throw std::runtime_error("ModSourceTool returned an invalid source analysis report.");
	}
	for (const auto& Member : Output.at("members"))
	{
		const auto Id = Member.at("memberId").get<std::string>();
		const auto It = Selected.find(Id);
		if (It == Selected.end())
		{
			throw std::runtime_error("Unexpected or duplicate source analysis result: " + Id);
		}
		SourceReportBinder::BindSourceErrors(*It->second, Model, Member, AnalysisDatabase);
		Selected.erase(It);
	}
	if (!Selected.empty())
	{
		throw std::runtime_error("Missing source analysis result: " + Selected.begin()->first);
	}
	const auto Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Started).count();
	std::cerr << "UMF timing MST failure analysis " << Seconds << " s\n";
}
} // namespace Mod::HeaderTool
