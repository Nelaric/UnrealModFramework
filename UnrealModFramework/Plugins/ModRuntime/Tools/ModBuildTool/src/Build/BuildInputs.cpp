// Copyright (c) 2026 Nelaric Contributors
#include "BuildInputs.h"

#include "Common/Files/Executable.h"
#include "Common/Files/Path.h"
#include "Common/Files/WatchedFiles.h"
#include "Common/Hash/Hash.h"
#include "Common/Process/ProcessEnvironment.h"
#include "ModBuildTool/src/Build/ContextDocument.h"
#include "ModBuildTool/src/Build/RecipeValidation.h"
#include "ModBuildTool/src/Cli/Version.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void BuildInputs::AddInput(Request& Request, const fs::path& Path)
{
	Request.Inputs[Path] = Hash::HashFile(Path);
}

fs::path BuildInputs::ChangedWatchedDirectory(const Json& Directories)
{
	for (const Json& Watch : Directories)
	{
		ContextDocument::RequireChoice(Watch, "kind", {"Config", "Source"});
		const fs::path Root = Path::PathFromUtf8(ContextDocument::Text(Watch, "root"));
		if (!Root.is_absolute())
		{
			throw std::runtime_error("Watched directory roots must be absolute.");
		}
		const bool bSource = Watch.at("kind") == "Source";
		if (bSource && !fs::is_directory(Root))
		{
			return Root;
		}
		const auto FilesNow =
		    bSource ? Tools::WatchedFiles::CollectSourceFiles({Root}) : Tools::WatchedFiles::CollectConfigFiles(Root);
		std::set<std::string> Current;
		for (const auto& Path : FilesNow)
		{
			Current.insert(Path::PathToUtf8(Path));
		}
		if (Watch.at("files") != Json(Current))
		{
			return Root;
		}
	}
	return {};
}

void BuildInputs::ValidateGenerationInputs(Request& Request)
{
	const Json& Files = Request.Manifest.at("inputFiles");
	const Json& Directories = Request.Manifest.at("watchedDirectories");
	if (!Files.is_array() || !Directories.is_array())
	{
		throw std::runtime_error("Generation inputFiles and watchedDirectories must be arrays.");
	}
	for (const Json& File : Files)
	{
		ContextDocument::RequireChoice(File, "kind", {"Semantic", "AnalysisContext"});
		const fs::path Declared = Path::PathFromUtf8(ContextDocument::Text(File, "path"));
		const std::string Expected = File.at("contentHash").get<std::string>();
		if (!Declared.is_absolute())
		{
			throw std::runtime_error("Generation input paths must be absolute.");
		}
		if (!Expected.empty())
		{
			Hash::RequireDigest(Expected, "inputFiles.contentHash");
		}
		const fs::path Path = fs::weakly_canonical(Declared);
		const std::string Current = fs::exists(Path) ? Hash::HashFile(Path) : std::string();
		if (Current != Expected)
		{
			throw std::runtime_error("GeneratedInputsStale: rerun MHT after input changed: " + Path::PathToUtf8(Path));
		}
		Request.Inputs.emplace(Path, Current);
	}
	const fs::path Changed = ChangedWatchedDirectory(Directories);
	if (!Changed.empty())
	{
		throw std::runtime_error("GeneratedInputsStale: rerun MHT after directory contents changed: " +
		                         Path::PathToUtf8(Changed));
	}
}

Request BuildInputs::ReadRequest(const fs::path& ContextPath, const fs::path& ManifestPath)
{
	Request Request;
	Request.ContextPath = fs::canonical(ContextPath);
	Request.ManifestPath = fs::canonical(ManifestPath);
	Request.Root = Request.ManifestPath.parent_path();
	Request.Context = ContextDocument::ReadJson(Request.ContextPath);
	Request.Manifest = ContextDocument::ReadJson(Request.ManifestPath);
	ContextDocument::RequireFormat(Request.Context, "UMF.ModHostContext", {2});
	ContextDocument::RequireFormat(Request.Manifest, "UMF.GeneratedArtifacts", {1, 2, 3});
	const int ManifestVersion = Request.Manifest.at("schemaVersion").get<int>();
	const bool bHasBatchMetadata = ManifestVersion >= 2;
	if (Request.Manifest.value("status", "") != "Complete" || !Request.Manifest.contains("unresolvedTypeCount") ||
	    Request.Manifest["unresolvedTypeCount"] != 0)
	{
		throw std::runtime_error("A host build requires a Complete batch with zero unresolved types.");
	}
	Request.Owner = ContextDocument::Text(Request.Context, "ownerModule");
	Request.ContextHash = Hash::HashBytes(Request.Context.dump());
	const std::string ExpectedContext =
	    bHasBatchMetadata || Request.Manifest.contains("hostBuildContexts")
	        ? ContextDocument::Text(Request.Manifest.at("hostBuildContexts"), Request.Owner)
	        : ContextDocument::Text(Request.Manifest, "buildContextHash");
	if (ExpectedContext != Request.ContextHash)
	{
		throw std::runtime_error("Generated batch does not match the exact host compilation context.");
	}
	Request.GenerationId = ContextDocument::Text(Request.Manifest, "generationId");
	Hash::RequireDigest(Request.GenerationId, "generationId");
	for (std::string_view Key : {"inputHash", "contractHash", "projectionHash"})
	{
		Hash::RequireDigest(ContextDocument::Text(Request.Manifest, Key), Key);
	}
	// Generated artifact schemas v1 and v2 use the old acronym casing.
	Request.NativeABIHash = ContextDocument::Text(Request.Manifest, ManifestVersion >= 3 ? "nativeABIHash"
	                                                                                     : "nativeA"
	                                                                                       "biHash");
	Hash::RequireDigest(Request.NativeABIHash, "nativeABIHash");
	Request.Anchor = ContextDocument::Text(Request.Context, "registrationAnchor");
	ContextDocument::RequireSymbol(Request.Anchor);
	std::set<std::string> Symbols{Request.Anchor};
	std::set<std::pair<std::string, std::string>> SymbolRecords;
	std::set<std::pair<std::string, std::string>> ExportIds;
	if (bHasBatchMetadata || Request.Manifest.contains("requiredSymbols"))
	{
		const Json& Required = Request.Manifest.at("requiredSymbols");
		if (!Required.is_array())
		{
			throw std::runtime_error("requiredSymbols must be an array of owning-module symbol records.");
		}
		for (const Json& Record : Required)
		{
			const std::string Owner = ContextDocument::Text(Record, "ownerModule");
			const std::string Symbol = ContextDocument::Text(Record, "symbol");
			ContextDocument::RequireSymbol(Symbol);
			if (!SymbolRecords.emplace(Owner, Symbol).second)
			{
				throw std::runtime_error("Duplicate required symbol: " + Owner + ":" + Symbol);
			}
			Json Binding = {{"ownerModule", Owner}, {"symbol", Symbol}};
			if (Record.contains("exportId"))
			{
				const std::string Id = ContextDocument::Text(Record, "exportId");
				if (!ExportIds.emplace(Owner, Id).second)
				{
					throw std::runtime_error("Duplicate required exportId in " + Owner + ": " + Id);
				}
				Binding["exportId"] = Id;
			}
			for (std::string_view Key : {"signatureHash", "projectionHash"})
			{
				if (Record.contains(Key))
				{
					const std::string Digest = ContextDocument::Text(Record, Key);
					Hash::RequireDigest(Digest, Key);
					Binding[std::string(Key)] = Digest;
				}
			}
			if (Owner == Request.Owner)
			{
				Symbols.insert(Symbol);
				Request.RequiredBindings.push_back(std::move(Binding));
			}
		}
	}
	Request.RequiredSymbols.assign(Symbols.begin(), Symbols.end());
	if (Request.Context.value("moduleAPIMode", "") != "Producer" || Request.Context.value("lto", true) ||
	    Request.Context.value("pch", true) || Request.Context.value("unity", true))
	{
		throw std::runtime_error("Host context requires Producer mode and explicit lto/pch/unity=false.");
	}
	const Json& Target = Request.Context.at("target");
	ContextDocument::Text(Target, "name");
	ContextDocument::RequireChoice(Target, "type", {"Game", "Editor", "Client", "Server", "Program"});
	ContextDocument::RequireChoice(Target, "configuration", {"Debug", "DebugGame", "Development", "Shipping", "Test"});
	ContextDocument::RequireChoice(Target, "linkType", {"Modular", "Monolithic"});
	Request.Platform = ContextDocument::Text(Target, "platform");
	Request.Architecture = ContextDocument::Text(Target, "architecture");
	if (bHasBatchMetadata &&
	    (ContextDocument::Text(Request.Manifest, "target") != ContextDocument::Text(Target, "name") ||
	     ContextDocument::Text(Request.Manifest, "platform") != Request.Platform ||
	     ContextDocument::Text(Request.Manifest, "configuration") != ContextDocument::Text(Target, "configuration") ||
	     ContextDocument::Text(Request.Manifest, "architecture") != Request.Architecture))
	{
		throw std::runtime_error("Generated target variant does not match the host context.");
	}
	if ((Request.Platform != "Win64" && Request.Platform != "Linux" && Request.Platform != "Mac") ||
	    (Request.Architecture != "x64" && Request.Architecture != "arm64"))
	{
		throw std::runtime_error("Supported archive targets are Win64/Linux/Mac and x64/arm64.");
	}
	Request.WorkingDirectory = Path::ResolvePath(Request.ContextPath.parent_path(),
	                                             ContextDocument::Text(Request.Context, "workingDirectory"));
	if (!fs::is_directory(Request.WorkingDirectory))
	{
		throw std::runtime_error("workingDirectory does not exist.");
	}
	Request.Library =
	    Path::ResolvePath(Request.ContextPath.parent_path(), ContextDocument::Text(Request.Context, "outputLibrary"));
	const fs::path RelativeOutput = Request.Library.lexically_relative(Request.Root);
	if (!RelativeOutput.empty() && *RelativeOutput.begin() != "..")
	{
		throw std::runtime_error("The host archive must be outside the managed generation root.");
	}
	if (Request.Library.extension() != (Request.Platform == "Win64" ? ".lib" : ".a"))
	{
		throw std::runtime_error("outputLibrary has the wrong static-library extension.");
	}
	Request.Receipt = Request.Library;
	Request.Receipt += ".mbt.json";
	if (Request.Context.contains("owningBinary"))
	{
		const Json& Binary = Request.Context.at("owningBinary");
		ContextDocument::RequireChoice(Binary, "kind", {"Executable", "DynamicLibrary"});
		Request.BinaryPath =
		    Path::ResolvePath(Request.ContextPath.parent_path(), ContextDocument::Text(Binary, "path"));
		if (Request.BinaryPath == Request.Library || Request.BinaryPath == Request.Receipt)
		{
			throw std::runtime_error("The owning binary cannot overwrite the host library or receipt.");
		}
		Request.OwningBinary = {{"id", ContextDocument::Text(Binary, "id")},
		                        {"path", Path::PathToUtf8(Request.BinaryPath)},
		                        {"kind", ContextDocument::Text(Binary, "kind")}};
	}
	if (Request.Context.contains("linkVerification"))
	{
		ContextDocument::RequireChoice(Request.Context, "linkVerification", {"PeExportV1"});
		if (Request.OwningBinary.is_null() || Request.Platform != "Win64" ||
		    Request.Context.at("compiler").value("responseFormat", "") != "msvc")
		{
			throw std::runtime_error("PeExportV1 requires an owning Win64 image and an MSVC compiler recipe.");
		}
		Request.bPeLinkWitness = true;
	}
	AddInput(Request, Request.ContextPath);
	AddInput(Request, Request.ManifestPath);
	AddInput(Request, Executable::ExecutablePath());
	if (bHasBatchMetadata)
	{
		ValidateGenerationInputs(Request);
	}
	std::map<fs::path, std::string> Owners;
	std::set<fs::path> PublicPaths;
	const Json& Files = Request.Manifest.at("generatedFiles");
	if (!Files.is_array())
	{
		throw std::runtime_error("generatedFiles must be an array.");
	}
	for (const Json& File : Files)
	{
		if (!File.contains("ownerModule"))
		{
			throw std::runtime_error("Every generated file must declare ownerModule, including null for shared files.");
		}
		const fs::path Path = Path::ResolveGeneratedPath(Request.Root, ContextDocument::Text(File, "path"));
		const std::string Expected = ContextDocument::Text(File, "contentHash");
		Hash::RequireDigest(Expected, "contentHash");
		AddInput(Request, Path);
		if (Request.Inputs.at(Path) != Expected || Owners.contains(Path))
		{
			throw std::runtime_error("Generated file hash mismatch or duplicate path: " + Path::PathToUtf8(Path));
		}
		Owners.emplace(Path,
		               File.value("ownerModule", Json()).is_null() ? "" : ContextDocument::Text(File, "ownerModule"));
		const std::string Role = File.value("role", "");
		if (bHasBatchMetadata)
		{
			ContextDocument::RequireChoice(File, "role",
			                               {"HostBinding", "HostRegistration", "CompileVerification", "HostHeader",
			                                "SdkHeader", "Contract", "SdkResource", "Diagnostic"});
		}
		if (Role == "SdkHeader" || Role == "Contract" || Role == "SdkResource")
		{
			if ((Role == "SdkHeader" && Path.extension() != ".h" && Path.extension() != ".hpp") ||
			    (Role == "Contract" && Path.extension() != ".json"))
			{
				throw std::runtime_error("Public artifact extension does not match its declared role.");
			}
			PublicPaths.insert(Path);
			if (Owners.at(Path).empty() || Owners.at(Path) == Request.Owner)
			{
				Request.PublicArtifacts.push_back(
				    {{"sourcePath", Path::PathToUtf8(Path)},
				     {"relativePath", Path::PathToUtf8(Path.lexically_relative(Request.Root))},
				     {"contentHash", Expected},
				     {"role", Role},
				     {"ownerModule", File.at("ownerModule")}});
			}
		}
	}
	std::set<fs::path> SeenUnits;
	std::set<std::string> UnitOwners;
	const Json& Units = Request.Manifest.at("compilationUnits");
	if (!Units.is_array())
	{
		throw std::runtime_error("compilationUnits must be an array.");
	}
	for (const Json& Unit : Units)
	{
		const std::string Owner = ContextDocument::Text(Unit, "ownerModule");
		UnitOwners.insert(Owner);
		const fs::path Source = Path::ResolveGeneratedPath(Request.Root, ContextDocument::Text(Unit, "source"));
		if (!Owners.contains(Source) || Owners.at(Source) != Owner || !SeenUnits.insert(Source).second ||
		    PublicPaths.contains(Source) ||
		    (Source.extension() != ".cpp" && Source.extension() != ".cc" && Source.extension() != ".cxx"))
		{
			throw std::runtime_error("Invalid or duplicate host compilation unit: " + Path::PathToUtf8(Source));
		}
		if (Owner == Request.Owner)
		{
			Request.Sources.push_back(Source);
		}
	}
	if (bHasBatchMetadata)
	{
		const Json& Contexts = Request.Manifest.at("hostBuildContexts");
		if (!Contexts.is_object() || Contexts.size() != UnitOwners.size())
		{
			throw std::runtime_error("hostBuildContexts must cover exactly the generated compilation owners.");
		}
		for (const auto& Owner : UnitOwners)
		{
			Hash::RequireDigest(ContextDocument::Text(Contexts, Owner), "hostBuildContexts." + Owner);
		}
	}
	if (Request.Sources.empty())
	{
		throw std::runtime_error("The owner needs a compilation unit, including for an empty registration batch.");
	}
	const Json& Dependencies = Request.Context.at("dependencies");
	if (!Dependencies.is_array())
	{
		throw std::runtime_error("Context dependencies must be an array.");
	}
	for (const Json& Dependency : Dependencies)
	{
		AddInput(Request, Path::ResolvePath(Request.ContextPath.parent_path(), Dependency.get<std::string>()));
	}
	RecipeValidation::ValidateRecipe(Request, "compiler");
	RecipeValidation::ValidateRecipe(Request, "archiver");
	const Json& Environment = Request.Context.at("environment");
	if (!Environment.is_object())
	{
		throw std::runtime_error("environment must be a string-to-string object.");
	}
	for (const auto& [Name, Value] : Environment.items())
	{
		if (Name.empty() || Name.find_first_of("=\r\n") != std::string::npos || Name.find('\0') != std::string::npos ||
		    Value.get<std::string>().find('\0') != std::string::npos)
		{
			throw std::runtime_error("Invalid environment name or value.");
		}
	}
	const std::string Mode = Request.Context.value("environmentMode", "Inherit");
	if (Mode == "Inherit")
	{
		Request.Environment = ProcessEnvironment::CaptureEnvironment();
	}
	else if (Mode != "Explicit")
	{
		throw std::runtime_error("environmentMode must be Explicit or Inherit.");
	}
	for (const auto& [Name, Value] : Environment.items())
	{
		std::string Key = Name;
#ifdef _WIN32
		std::transform(Key.begin(), Key.end(), Key.begin(),
		               [](unsigned char Character) { return static_cast<char>(std::toupper(Character)); });
#endif
		Request.Environment[Key] = Value.get<std::string>();
	}
	Request.EnvironmentHash = Hash::HashBytes(Json(Request.Environment).dump());
	Request.Inputs[fs::path("<effective-environment>")] = Request.EnvironmentHash;
	if (Request.Inputs.contains(Request.Library) || Request.Inputs.contains(Request.Receipt) ||
	    (!Request.BinaryPath.empty() && Request.Inputs.contains(Request.BinaryPath)))
	{
		throw std::runtime_error("A build output would replace a build input.");
	}
	return Request;
}

Json BuildInputs::InputList(const std::map<fs::path, std::string>& Inputs)
{
	Json Result = Json::array();
	for (const auto& [Path, Digest] : Inputs)
	{
		Result.push_back({{"path", Path::PathToUtf8(Path)}, {"contentHash", Digest}});
	}
	return Result;
}

std::string BuildInputs::Fingerprint(const std::map<fs::path, std::string>& Inputs)
{
	return Hash::HashBytes(Json({{"toolVersion", Version::ToolVersion}, {"inputs", InputList(Inputs)}}).dump());
}
} // namespace Mod::BuildTool
