// Copyright (c) 2026 Nelaric Contributors
#include "DeclarationAnalyzer.h"

#include "ModHeaderTool/src/Analysis/AnalysisArguments.h"
#include "ModHeaderTool/src/Analysis/ClangLibrary.h"
#include "ModHeaderTool/src/Analysis/CursorSource.h"
#include "ModHeaderTool/src/Analysis/DeclarationBinding.h"
#include "ModHeaderTool/src/Analysis/FullView.h"
#include "ModHeaderTool/src/Analysis/FullViewSelection.h"
#include "ModHeaderTool/src/Analysis/Location.h"
#include "ModHeaderTool/src/Analysis/Source.h"
#include "ModHeaderTool/src/Analysis/Traversal.h"
#include "ModHeaderTool/src/Analysis/TypeResolution.h"
#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/ExportNames.h"
#include "ModHeaderTool/src/Generation/ModuleInput.h"
#include "ModHeaderTool/src/Generation/ResolvedType.h"
#include "ModHeaderTool/src/Generation/TypeIdentity.h"
#include "ModHeaderTool/src/Metadata/MetadataOptions.h"
#include "ModHeaderTool/src/Metadata/MetadataOptionsReader.h"
#include "ModHeaderTool/src/Metadata/MetadataParser.h"
#include "ModHeaderTool/src/Metadata/MetadataValidator.h"
#include "ModHeaderTool/src/Project/Project.h"
#include "ModHeaderTool/src/Project/ProjectSources.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include "ModExportMetadataSchema.h"

#include <algorithm>
#include <cctype>
#include <clang-c/Index.h>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

GenerationModel DeclarationAnalyzer::Analyze(GenerationInput Input, const fs::path& OptionsFile,
                                             const fs::path& ClangPath, const fs::path& AnalysisDatabase)
{
	GenerationModel Model;
	Model.Input = std::move(Input);
	Model.Dependencies = {Model.Input.File, Model.Input.Database, Model.Input.Project.File};
	Model.Dependencies.insert(AnalysisDatabase);
	const auto TargetDirectory = Model.Input.Project.Root / "Source";
	if (fs::is_directory(TargetDirectory))
	{
		for (const auto& Entry : fs::directory_iterator(TargetDirectory))
		{
			if (Entry.is_regular_file() && Path::PathToUtf8(Entry.path().filename()).ends_with(".Target.cs"))
			{
				Model.Dependencies.insert(Entry.path());
			}
		}
	}
	if (!Model.Input.IdentityFile.empty())
	{
		Model.Dependencies.insert(Model.Input.IdentityFile);
	}
	for (const auto& Module : Model.Input.Modules)
	{
		const auto Rules = Module.Root / (Module.Name + ".Build.cs");
		if (fs::exists(Rules))
		{
			Model.Dependencies.insert(Rules);
		}
	}
	if (Model.Input.Project.bHasConfig)
	{
		Model.Dependencies.insert(Model.Input.Project.ConfigFile);
	}
	const auto Options = MetadataOptionsReader::LoadMetadataOptions(
	    Model.Input.Project,
	    OptionsFile.empty() ? Model.Input.Project.Root / Mod::ExportMetadata::Schema::SnapshotPath : OptionsFile);
	Model.Dependencies.insert(Options.File);
	Model.Dependencies.insert(Options.Inputs.begin(), Options.Inputs.end());
	Model.Dependencies.insert(Model.Input.Project.ConfigFile);
	Model.ConfigRoots = Options.ConfigRoots;
	std::map<std::string, fs::path> SourcePaths;
	for (const auto& File : ProjectSources::CollectSourceFiles(Model.Input.Project))
	{
		if (!TypeResolution::OwnerModule(Model.Input, File))
		{
			continue;
		}
		Model.Dependencies.insert(File);
		SourcePaths.emplace(CursorSource::Key(File), File);
	}
	ClangLibrary Clang(ClangPath);
	Model.ClangVersion = Clang.String(Clang.clang_getClangVersion());
	const auto Index = Clang.clang_createIndex(0, 0);
	const auto IndexCleanup = std::shared_ptr<void>(Index, [&](void* Value) { Clang.clang_disposeIndex(Value); });
	const auto Database = nlohmann::json::parse(TextFile::ReadTextFile(AnalysisDatabase));
	if (!Database.is_array())
	{
		throw std::runtime_error("Expected an actual UBT compilation database array.");
	}
	std::multimap<std::string, const nlohmann::json*> Entries;
	for (const auto& Entry : Database)
	{
		const auto Directory = Path::PathFromUtf8(Entry.at("directory").get<std::string>());
		Entries.emplace(CursorSource::Key(Directory / Path::PathFromUtf8(Entry.at("file").get<std::string>())), &Entry);
	}
	std::map<std::pair<std::string, std::size_t>, std::size_t> Bound;
	std::map<std::string, FullView> FullViews;
	for (const auto& Module : Model.Input.Modules)
	{
		for (const auto& Unit : Module.TranslationUnits)
		{
			Model.Dependencies.insert(Unit);
			const auto [First, Last] = Entries.equal_range(CursorSource::Key(Unit));
			if (First == Last || std::next(First) != Last)
			{
				throw std::runtime_error("Require exactly one actual Target compilation command for " +
				                         DiagnosticLocation::QuoteDiagnosticPath(Unit));
			}
			const auto& Command = *First->second;
			const auto Arguments = Command.contains("arguments")
			                           ? Command.at("arguments").get<std::vector<std::string>>()
			                           : AnalysisArguments::SplitArguments(Command.at("command").get<std::string>());
			const auto Directory = Path::PathFromUtf8(Command.at("directory").get<std::string>());
			const auto Adapted = AnalysisArguments::AnalysisCommand(Arguments, Directory, Unit, Model);
			std::vector<const char*> Argv;
			for (const auto& Arg : Adapted)
			{
				Argv.push_back(Arg.c_str());
			}
			CXTranslationUnit Tu = nullptr;
			const auto Error = Clang.clang_parseTranslationUnit2FullArgv(
			    Index, nullptr, Argv.data(), static_cast<int>(Argv.size()), nullptr, 0,
			    CXTranslationUnit_DetailedPreprocessingRecord | CXTranslationUnit_SkipFunctionBodies, &Tu);
			if (Error != CXError_Success || !Tu)
			{
				throw std::runtime_error("libclang could not parse " + DiagnosticLocation::QuoteDiagnosticPath(Unit) +
				                         "; check the locked analysis arguments.");
			}
			const auto TuCleanup = std::shared_ptr<CXTranslationUnitImpl>(
			    Tu, [&](auto* Value) { Clang.clang_disposeTranslationUnit(Value); });
			std::string Errors;
			for (unsigned I = 0; I < Clang.clang_getNumDiagnostics(Tu); ++I)
			{
				const auto Diagnostic = Clang.clang_getDiagnostic(Tu, I);
				if (Clang.clang_getDiagnosticSeverity(Diagnostic) >= CXDiagnostic_Error)
				{
					Errors += Clang.FormatDiagnostic(Diagnostic) + '\n';
				}
				Clang.clang_disposeDiagnostic(Diagnostic);
			}
			if (!Errors.empty())
			{
				throw std::runtime_error(Errors);
			}
			struct InclusionContext
			{
				ClangLibrary* Clang;
				GenerationModel* Model;
				fs::path Directory;
			} Inclusion{&Clang, &Model, Directory};
			Clang.clang_getInclusions(
			    Tu,
			    [](CXFile File, CXSourceLocation*, unsigned, CXClientData Data)
			    {
				    auto& Context = *static_cast<InclusionContext*>(Data);
				    const auto Name = Context.Clang->String(Context.Clang->clang_getFileName(File));
				    if (!Name.empty())
				    {
					    const auto Path = Path::PathFromUtf8(Name);
					    Context.Model->Dependencies.insert(
					        fs::weakly_canonical(Path.is_absolute() ? Path : Context.Directory / Path));
				    }
			    },
			    &Inclusion);
			Traversal Traversal{Clang, Tu, SourcePaths, {}, {}, {}, {}};
			Clang.clang_visitChildren(Clang.clang_getTranslationUnitCursor(Tu), Traversal::Visit, &Traversal);
			if (Traversal.Error)
			{
				std::rethrow_exception(Traversal.Error);
			}
			std::string Configuration =
			    Model.Input.Configuration == "DebugGame" ? "Development" : Model.Input.Configuration;
			std::transform(Configuration.begin(), Configuration.end(), Configuration.begin(),
			               [](unsigned char C) { return std::toupper(C); });
			const auto Variant = Traversal.Macros.find("UMF_MHT_TARGET_CONFIGURATION");
			const bool bVariantMatches = Variant != Traversal.Macros.end()
			                                 ? Variant->second == Model.Input.Configuration
			                                 : Model.Input.Configuration != "DebugGame";
			if (Traversal.Macros["UE_TARGET_NAME"] != Model.Input.Target ||
			    Traversal.Macros["UE_MODULE_NAME"] != "\"" + Module.Name + "\"" ||
			    Traversal.Macros["UBT_COMPILED_PLATFORM"] != Model.Input.Platform ||
			    Traversal.Macros["UE_BUILD_" + Configuration] != "1" || !Traversal.Macros.contains("_M_X64") ||
			    !bVariantMatches)
			{
				throw std::runtime_error(
				    "Compilation database does not match the declared UBT Target/module/configuration/platform: " +
				    DiagnosticLocation::QuoteDiagnosticPath(Unit));
			}
			if (const auto It = Model.ContextMacros.find(Module.Name);
			    It != Model.ContextMacros.end() && It->second != Traversal.Macros)
			{
				throw std::runtime_error("Inconsistent Target macro context in module " + Module.Name);
			}
			Model.ContextMacros[Module.Name] = std::move(Traversal.Macros);

			for (const auto& [SourceKey, Expansions] : Traversal.ExportExpansions)
			{
				CXFile File = nullptr;
				Clang.clang_getExpansionLocation(Clang.clang_getCursorLocation(Expansions.front()), &File, nullptr,
				                                 nullptr, nullptr);
				const Source Source(Clang, Tu, File, SourcePaths.at(SourceKey));
				const auto& Methods = Traversal.Methods[SourceKey];
				for (const auto Expansion : Expansions)
				{
					auto Invocation = Source.ReadMacro(Clang, Expansion);
					if (!Invocation)
					{
						continue;
					}
					auto& Macro = *Invocation;
					if (Macro.Name != "MOD_EXPORT_API")
					{
						CursorSource::Fail(Macro, "code generation currently supports API declarations only.");
					}
					Macro.Fields = MetadataParser::ParseMetadata(Macro, Source.Text);
					const auto Diagnostics = MetadataValidator::ValidateMetadata(Macro, &Options, Source.Text);
					if (!Diagnostics.empty())
					{
						std::string Message;
						for (const auto& Diagnostic : Diagnostics)
						{
							Message += Diagnostic + '\n';
						}
						throw std::runtime_error(Message);
					}
					CXCursor Candidate{};
					std::size_t Nearest = Source.Text.size() + 1;
					for (const auto Method : Methods)
					{
						const auto Start = CursorSource::ReadLocation(
						    Clang, Clang.clang_getRangeStart(Clang.clang_getCursorExtent(Method)));
						if (Start.Offset >= Macro.Offset + Macro.Length && Start.Offset < Nearest &&
						    !Start.File.empty() && CursorSource::Key(Start.File) == SourceKey)
						{
							Candidate = Method;
							Nearest = Start.Offset;
						}
					}
					if (Candidate.kind != CXCursor_CXXMethod)
					{
						CursorSource::Fail(Macro, "active marker has no supported member declaration.");
					}
					API API;
					try
					{
						API = DeclarationBinding::Bind(Model, Clang, Macro, Source, Candidate);
						FullViewSelection::SelectFullViews(Model, Clang, Candidate, API, FullViews);
					}
					catch (const std::exception& Error)
					{
						if (std::string(Error.what()).find("): error:") != std::string::npos)
						{
							throw;
						}
						CursorSource::Fail(Macro, Error.what());
					}
					const auto KeyValue = std::make_pair(SourceKey, Macro.Offset);
					if (const auto It = Bound.find(KeyValue); It != Bound.end())
					{
						const auto& Previous = Model.APIs[It->second];
						if (Previous.DeclarationUsr != API.DeclarationUsr ||
						    Previous.SourceSignature != API.SourceSignature)
						{
							CursorSource::Fail(Macro, "inconsistent member declaration across translation units.");
						}
						if (Previous.Macro.Arguments != Macro.Arguments || Previous.bReflected != API.bReflected)
						{
							CursorSource::Fail(Macro,
							                   "inconsistent export metadata or reflection across translation units.");
						}
					}
					else
					{
						Bound.emplace(KeyValue, Model.APIs.size());
						Model.APIs.push_back(std::move(API));
					}
				}
			}
		}
	}
	std::set<std::pair<std::string, std::string>> DeclaredMembers;
	for (const auto& API : Model.APIs)
	{
		DeclaredMembers.emplace(API.Owner, API.SourceSignature);
	}
	for (auto& [Name, View] : FullViews)
	{
		View.Permissions.insert("FullView." + Model.Types.at(Name).Identity.Id);
		for (auto& API : View.Members)
		{
			if (!DeclaredMembers.contains({Name, API.SourceSignature}))
			{
				API.Permissions.assign(View.Permissions.begin(), View.Permissions.end());
				Model.APIs.push_back(std::move(API));
			}
		}
	}
	ExportNames::AssignExportNames(Model, Options);
	std::sort(Model.APIs.begin(), Model.APIs.end(), [](const auto& A, const auto& B) { return A.Id < B.Id; });
	std::set<std::string> TypeIds;
	for (const auto& [Name, Type] : Model.Types)
	{
		auto Id = Type.Identity.Id;
		std::transform(Id.begin(), Id.end(), Id.begin(), [](unsigned char C) { return std::tolower(C); });
		if (!TypeIds.insert(Id).second)
		{
			throw std::runtime_error("Duplicate generated TypeId for '" + Name + "': " + Type.Identity.Id);
		}
	}
	std::map<std::string, const API*> Ids;
	for (std::size_t I = 0; I < Model.APIs.size(); ++I)
	{
		auto Id = Model.APIs[I].Id;
		std::transform(Id.begin(), Id.end(), Id.begin(), [](unsigned char C) { return std::tolower(C); });
		if (const auto [Other, bInserted] = Ids.emplace(Id, &Model.APIs[I]); !bInserted)
		{
			const auto& API = Model.APIs[I];
			const auto& Previous = *Other->second;
			const bool bPreviousIsAlias = API.ExportName == API.Name && Previous.ExportName != Previous.Name;
			const auto& Source = bPreviousIsAlias ? Previous : API;
			const auto& Conflict = bPreviousIsAlias ? API : Previous;
			throw std::runtime_error(Source.NameLocation + ": error: duplicate generated ExportId '" + Id +
			                         "'. Also exported at " + Conflict.NameLocation + ".");
		}
		Model.Types.at(Model.APIs[I].Owner).APIs.push_back(I);
	}
	return Model;
}
} // namespace Mod::HeaderTool
