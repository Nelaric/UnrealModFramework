// Copyright (c) 2026 Nelaric Contributors
#include "SourceToolInvocation.h"

#include "Common/Files/Executable.h"
#include "Common/Files/File.h"
#include "Common/Process/Process.h"
#include "Common/Process/ProcessEnvironment.h"
#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Analysis/SourceReport.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"
#include "ModHeaderTool/src/Hash/Hash.h"
#include "ModHeaderTool/src/Project/Project.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>
#include <cstddef>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

nlohmann::json SourceToolInvocation::SourceRequest(const GenerationModel& Model, const fs::path& Database,
                                                   const fs::path& ClangLibrary)
{
	if (ClangLibrary.empty())
	{
		throw std::runtime_error("Source error analysis requires libclang in the generation request.");
	}
	const auto& Arguments = Model.Input.AnalysisArguments;
	std::vector<std::string> AnalysisArguments;
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
			AnalysisArguments.push_back(Arguments[I]);
		}
	}
	if (!ResourceDirectory.is_absolute())
	{
		throw std::runtime_error("Source error analysis requires an absolute -resource-dir in analysisArguments.");
	}
	return {{"format", "UMF.SourceAnalysisRequest"},
	        {"schemaVersion", 2},
	        {"compilationDatabase", Path::PathToUtf8(Database)},
	        {"target", Model.Input.Target},
	        {"projectRoot", Path::PathToUtf8(Model.Input.Project.Root)},
	        {"libclang", Path::PathToUtf8(fs::canonical(ClangLibrary))},
	        {"resourceDirectory", Path::PathToUtf8(fs::canonical(ResourceDirectory))},
	        {"analysisArguments", std::move(AnalysisArguments)}};
}

void SourceToolInvocation::RewriteHooks(GenerationModel& Model, const fs::path& Database, const fs::path& ClangLibrary,
                                        const fs::path& SourceTool)
{
	using Json = nlohmann::json;
	Json Modules = Json::array();
	for (const auto& Module : Model.Input.Modules)
	{
		Json Dispatchers = Json::object();
		for (const auto& [Id, Entry] : Model.Extensions)
		{
			if (Entry.Module == Module.Name && !Entry.DefinitionUsr.empty())
			{
				Dispatchers[Entry.DefinitionUsr] =
				    "::Mod::Generated::" + Entry.Module + "::" + Entry.CppHelperName() + "::Dispatch";
			}
		}
		if (Dispatchers.empty())
		{
			continue;
		}
		std::set<fs::path> Candidates;
		for (const auto& [Usr, Dispatcher] : Dispatchers.items())
		{
			const auto Methods = Model.NativeMethodUnits.find(Usr);
			if (Methods == Model.NativeMethodUnits.end())
			{
				// Let MST diagnose a definition that the declaration pass could not locate.
				Candidates.insert(Module.TranslationUnits.begin(), Module.TranslationUnits.end());
				break;
			}
			Candidates.insert(Methods->second.begin(), Methods->second.end());
		}
		Json Sources = Json::array();
		for (const auto& Unit : Module.TranslationUnits)
		{
			if (Candidates.contains(Unit))
			{
				Sources.push_back(Path::PathToUtf8(Unit));
			}
		}
		Modules.push_back({{"owner", Module.Name}, {"sources", Sources}, {"dispatchers", Dispatchers}});
	}
	if (Modules.empty())
	{
		return;
	}
	const auto Tool = ResolveSourceTool(SourceTool);
	Model.Dependencies.insert(Tool);
	auto Request = SourceRequest(Model, Database, ClangLibrary);
	Request["format"] = "UMF.HookSourceRequest";
	Request["schemaVersion"] = 1;
	Request["modules"] = std::move(Modules);
	const SourceReport Report(Model.Input.File.parent_path());
	Mod::BuildTool::File::WriteFileAtomically(Report.Request, Request.dump(2) + "\n");
	const auto Started = std::chrono::steady_clock::now();
	const int Exit = Mod::BuildTool::Process::RunProcess(
	    Tool, {"rewrite-hooks", Path::PathToUtf8(Report.Request)}, Model.Input.Project.Root,
	    Mod::BuildTool::ProcessEnvironment::CaptureEnvironment(), Report.File);
	if (Exit != 0)
	{
		throw std::runtime_error("ModSourceTool failed to rewrite automatic Hooks (exit code " + std::to_string(Exit) +
		                         ").");
	}
	const auto Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Started).count();
	std::cerr << "UMF timing MST Hook rewrite " << Seconds << " s\n";
	Json Plan = Json::array();
	const auto Sources = Json::parse(Mod::BuildTool::File::ReadFile(Report.File));
	for (const auto& Record : Sources)
	{
		const auto& Owner = Record.at("owner").get_ref<const std::string&>();
		const auto Module = std::ranges::find(Model.Input.Modules, Owner, &ModuleInput::Name);
		const auto Source = fs::canonical(Path::PathFromUtf8(Record.at("source").get<std::string>()));
		const auto Original = Hash::HashBytes(Record.at("original").get_ref<const std::string&>());
		if (Module == Model.Input.Modules.end() ||
		    std::ranges::find(Module->TranslationUnits, Source) == Module->TranslationUnits.end() ||
		    Hash::HashFile(Source) != Original)
		{
			throw std::runtime_error("The reported automatic Hook source is stale or outside its module inputs.");
		}
		const auto Relative = Source.lexically_relative(Module->Root);
		if (Relative.empty() || *Relative.begin() == "..")
		{
			throw std::runtime_error("Automatic Hook source is outside its owning module: " + Path::PathToUtf8(Source));
		}
		const auto File = "Diagnostics/HookSources/" + Owner + "/" + Path::PathToUtf8(Relative);
		auto Code = Record.at("code").get<std::string>();
		const auto Witness = "UMF_ModHook_" + Hash::HashBytes(File + '\n' + Code);
		Code += "\nextern \"C\" void " + Witness + "() {}\n";
		Plan.push_back({{"owner", Owner},
		                {"source", Path::PathToUtf8(Source)},
		                {"sourceHash", Original},
		                {"generated", File},
		                {"generatedHash", Hash::HashBytes(Code)},
		                {"witness", Witness}});
		if (!Model.HookSources.emplace(File, std::move(Code)).second)
		{
			throw std::runtime_error("Duplicate automatic Hook translation unit: " + File);
		}
	}
	Model.HookSources.emplace("Diagnostics/HookSources.json",
	                          Json{{"format", "UMF.HookSourceMap"}, {"schemaVersion", 1}, {"units", Plan}}.dump(2) +
	                              '\n');
}
} // namespace Mod::HeaderTool
