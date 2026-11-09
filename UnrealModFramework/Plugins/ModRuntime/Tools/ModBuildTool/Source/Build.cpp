// Copyright (c) 2026 Nelaric Contributors
#include "Build.h"

#include "Archive.h"
#include "Binary.h"
#include "Hash.h"
#include "Process.h"
#include "Version.h"
#include "WatchedFiles.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace Mod::BuildTool
{
namespace
{
std::string Text(const FJson& Document, std::string_view Key)
{
	const std::string Value = Document.at(std::string(Key)).get<std::string>();
	if (Value.empty() || Value.find('\0') != std::string::npos)
	{
		throw std::runtime_error(std::string(Key) + " must be a nonempty string without NUL characters.");
	}
	return Value;
}

FJson ReadJson(const fs::path& Path)
{
	try
	{
		return FJson::parse(ReadFile(Path));
	}
	catch (const FJson::exception& Error)
	{
		throw std::runtime_error("Invalid JSON in " + PathToUtf8(Path) + ": " + Error.what());
	}
}

void RequireFormat(const FJson& Document, std::string_view Format, std::initializer_list<int> Versions = {1})
{
	if (!Document.is_object() || Document.value("format", "") != Format || !Document.contains("schemaVersion") ||
	    !Document["schemaVersion"].is_number_integer() ||
	    std::none_of(Versions.begin(), Versions.end(),
	                 [&](int Version) { return Document["schemaVersion"] == Version; }))
	{
		throw std::runtime_error("Unsupported format or schemaVersion for " + std::string(Format) + ".");
	}
}

void RequireSymbol(std::string_view Symbol)
{
	if (Symbol.empty() || (!std::isalpha(static_cast<unsigned char>(Symbol.front())) && Symbol.front() != '_') ||
	    Symbol.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
	        std::string_view::npos)
	{
		throw std::runtime_error("Required symbols must be undecorated C identifiers.");
	}
}

void RequireChoice(const FJson& Document, std::string_view Key, std::initializer_list<std::string_view> Choices)
{
	const std::string Value = Text(Document, Key);
	if (std::find(Choices.begin(), Choices.end(), std::string_view(Value)) == Choices.end())
	{
		throw std::runtime_error("Unsupported " + std::string(Key) + ": " + Value);
	}
}

bool ContainsMarker(const FJson& Arguments, std::string_view Marker)
{
	return std::any_of(Arguments.begin(), Arguments.end(), [Marker](const FJson& Value)
	                   { return Value.get<std::string>().find(Marker) != std::string::npos; });
}

struct FRequest
{
	fs::path ContextPath;
	fs::path ManifestPath;
	fs::path Root;
	fs::path WorkingDirectory;
	fs::path Library;
	fs::path Receipt;
	FJson Context;
	FJson Manifest;
	std::string ContextHash;
	std::string GenerationId;
	std::string Owner;
	std::string Anchor;
	std::string Platform;
	std::string Architecture;
	std::vector<fs::path> Sources;
	std::vector<std::string> RequiredSymbols;
	FJson RequiredBindings = FJson::array();
	FJson PublicArtifacts = FJson::array();
	FJson OwningBinary = nullptr;
	fs::path BinaryPath;
	bool bPeLinkWitness = false;
	std::map<fs::path, std::string> Inputs;
	std::map<std::string, std::string> Environment;
	std::string EnvironmentHash;
};

void AddInput(FRequest& Request, const fs::path& Path)
{
	Request.Inputs[Path] = HashFile(Path);
}

fs::path ChangedWatchedDirectory(const FJson& Directories)
{
	for (const FJson& Watch : Directories)
	{
		RequireChoice(Watch, "kind", {"Config", "Source"});
		const fs::path Root = PathFromUtf8(Text(Watch, "root"));
		if (!Root.is_absolute())
		{
			throw std::runtime_error("Watched directory roots must be absolute.");
		}
		const bool bSource = Watch.at("kind") == "Source";
		if (bSource && !fs::is_directory(Root))
		{
			return Root;
		}
		const auto FilesNow = bSource ? Tools::CollectSourceFiles({Root}) : Tools::CollectConfigFiles(Root);
		std::set<std::string> Current;
		for (const auto& Path : FilesNow)
		{
			Current.insert(PathToUtf8(Path));
		}
		if (Watch.at("files") != FJson(Current))
		{
			return Root;
		}
	}
	return {};
}

void ValidateGenerationInputs(FRequest& Request)
{
	const FJson& Files = Request.Manifest.at("inputFiles");
	const FJson& Directories = Request.Manifest.at("watchedDirectories");
	if (!Files.is_array() || !Directories.is_array())
	{
		throw std::runtime_error("Generation inputFiles and watchedDirectories must be arrays.");
	}
	for (const FJson& File : Files)
	{
		RequireChoice(File, "kind", {"Semantic", "AnalysisContext"});
		const fs::path Declared = PathFromUtf8(Text(File, "path"));
		const std::string Expected = File.at("contentHash").get<std::string>();
		if (!Declared.is_absolute())
		{
			throw std::runtime_error("Generation input paths must be absolute.");
		}
		if (!Expected.empty())
		{
			RequireDigest(Expected, "inputFiles.contentHash");
		}
		const fs::path Path = fs::weakly_canonical(Declared);
		const std::string Current = fs::exists(Path) ? HashFile(Path) : std::string();
		if (Current != Expected)
		{
			throw std::runtime_error("GeneratedInputsStale: rerun MHT after input changed: " + PathToUtf8(Path));
		}
		Request.Inputs.emplace(Path, Current);
	}
	const fs::path Changed = ChangedWatchedDirectory(Directories);
	if (!Changed.empty())
	{
		throw std::runtime_error("GeneratedInputsStale: rerun MHT after directory contents changed: " +
		                         PathToUtf8(Changed));
	}
}

void ValidateRecipe(FRequest& Request, std::string_view Name)
{
	FJson& Recipe = Request.Context.at(std::string(Name));
	const fs::path Tool = ResolvePath(Request.ContextPath.parent_path(), Text(Recipe, "path"));
	Recipe["path"] = PathToUtf8(Tool);
	AddInput(Request, Tool);
	const std::string Format = Text(Recipe, "responseFormat");
	if (Format != "msvc" && Format != "gnu")
	{
		throw std::runtime_error("Unsupported tool responseFormat: " + Format);
	}
	const FJson& Arguments = Recipe.at("arguments");
	if (!Arguments.is_array() || Arguments.empty())
	{
		throw std::runtime_error("Tool arguments must be a nonempty argument array.");
	}
	for (const FJson& Argument : Arguments)
	{
		const std::string Value = Argument.get<std::string>();
		if (Value.find_first_of("\r\n") != std::string::npos || Value.find('\0') != std::string::npos)
		{
			throw std::runtime_error("Tool arguments cannot contain NUL or newlines.");
		}
	}
	if (Name == "compiler")
	{
		if (!ContainsMarker(Arguments, "{source}") || !ContainsMarker(Arguments, "{object}") ||
		    !ContainsMarker(Arguments, "{dependencies}"))
		{
			throw std::runtime_error("Compiler arguments must name {source}, {object}, and {dependencies}.");
		}
		const std::string Dependencies = Text(Recipe, "dependencyFormat");
		if (Dependencies != "make" && Dependencies != "msvc-json")
		{
			throw std::runtime_error("Unsupported compiler dependencyFormat.");
		}
	}
	else if (!ContainsMarker(Arguments, "{output}") ||
	         std::find(Arguments.begin(), Arguments.end(), FJson("{objects}")) == Arguments.end())
	{
		throw std::runtime_error("Archiver arguments require {output} and a standalone {objects} argument.");
	}
}

FRequest ReadRequest(const fs::path& ContextPath, const fs::path& ManifestPath)
{
	FRequest Request;
	Request.ContextPath = fs::canonical(ContextPath);
	Request.ManifestPath = fs::canonical(ManifestPath);
	Request.Root = Request.ManifestPath.parent_path();
	Request.Context = ReadJson(Request.ContextPath);
	Request.Manifest = ReadJson(Request.ManifestPath);
	RequireFormat(Request.Context, "UMF.ModHostContext");
	RequireFormat(Request.Manifest, "UMF.GeneratedArtifacts", {1, 2});
	const bool bManifestV2 = Request.Manifest.at("schemaVersion") == 2;
	if (Request.Manifest.value("status", "") != "Complete" || !Request.Manifest.contains("unresolvedTypeCount") ||
	    Request.Manifest["unresolvedTypeCount"] != 0)
	{
		throw std::runtime_error("A host build requires a Complete batch with zero unresolved types.");
	}
	Request.Owner = Text(Request.Context, "ownerModule");
	Request.ContextHash = HashBytes(Request.Context.dump());
	const std::string ExpectedContext = bManifestV2 || Request.Manifest.contains("hostBuildContexts")
	                                        ? Text(Request.Manifest.at("hostBuildContexts"), Request.Owner)
	                                        : Text(Request.Manifest, "buildContextHash");
	if (ExpectedContext != Request.ContextHash)
	{
		throw std::runtime_error("Generated batch does not match the exact host compilation context.");
	}
	Request.GenerationId = Text(Request.Manifest, "generationId");
	RequireDigest(Request.GenerationId, "generationId");
	for (std::string_view Key : {"inputHash", "contractHash", "projectionHash", "nativeAbiHash"})
	{
		RequireDigest(Text(Request.Manifest, Key), Key);
	}
	Request.Anchor = Text(Request.Context, "registrationAnchor");
	RequireSymbol(Request.Anchor);
	std::set<std::string> Symbols{Request.Anchor};
	std::set<std::pair<std::string, std::string>> SymbolRecords;
	std::set<std::pair<std::string, std::string>> ExportIds;
	if (bManifestV2 || Request.Manifest.contains("requiredSymbols"))
	{
		const FJson& Required = Request.Manifest.at("requiredSymbols");
		if (!Required.is_array())
		{
			throw std::runtime_error("requiredSymbols must be an array of owning-module symbol records.");
		}
		for (const FJson& Record : Required)
		{
			const std::string Owner = Text(Record, "ownerModule");
			const std::string Symbol = Text(Record, "symbol");
			RequireSymbol(Symbol);
			if (!SymbolRecords.emplace(Owner, Symbol).second)
			{
				throw std::runtime_error("Duplicate required symbol: " + Owner + ":" + Symbol);
			}
			FJson Binding = {{"ownerModule", Owner}, {"symbol", Symbol}};
			if (Record.contains("exportId"))
			{
				const std::string Id = Text(Record, "exportId");
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
					const std::string Digest = Text(Record, Key);
					RequireDigest(Digest, Key);
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
	if (Request.Context.value("moduleApiMode", "") != "Producer" || Request.Context.value("lto", true) ||
	    Request.Context.value("pch", true) || Request.Context.value("unity", true))
	{
		throw std::runtime_error("Host context requires Producer mode and explicit lto/pch/unity=false.");
	}
	const FJson& Target = Request.Context.at("target");
	Text(Target, "name");
	RequireChoice(Target, "type", {"Game", "Editor", "Client", "Server", "Program"});
	RequireChoice(Target, "configuration", {"Debug", "DebugGame", "Development", "Shipping", "Test"});
	RequireChoice(Target, "linkType", {"Modular", "Monolithic"});
	Request.Platform = Text(Target, "platform");
	Request.Architecture = Text(Target, "architecture");
	if (bManifestV2 && (Text(Request.Manifest, "target") != Text(Target, "name") ||
	                    Text(Request.Manifest, "platform") != Request.Platform ||
	                    Text(Request.Manifest, "configuration") != Text(Target, "configuration") ||
	                    Text(Request.Manifest, "architecture") != Request.Architecture))
	{
		throw std::runtime_error("Generated target variant does not match the host context.");
	}
	if ((Request.Platform != "Win64" && Request.Platform != "Linux" && Request.Platform != "Mac") ||
	    (Request.Architecture != "x64" && Request.Architecture != "arm64"))
	{
		throw std::runtime_error("Supported archive targets are Win64/Linux/Mac and x64/arm64.");
	}
	Request.WorkingDirectory =
	    ResolvePath(Request.ContextPath.parent_path(), Text(Request.Context, "workingDirectory"));
	if (!fs::is_directory(Request.WorkingDirectory))
	{
		throw std::runtime_error("workingDirectory does not exist.");
	}
	Request.Library = ResolvePath(Request.ContextPath.parent_path(), Text(Request.Context, "outputLibrary"));
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
		const FJson& Binary = Request.Context.at("owningBinary");
		RequireChoice(Binary, "kind", {"Executable", "DynamicLibrary"});
		Request.BinaryPath = ResolvePath(Request.ContextPath.parent_path(), Text(Binary, "path"));
		if (Request.BinaryPath == Request.Library || Request.BinaryPath == Request.Receipt)
		{
			throw std::runtime_error("The owning binary cannot overwrite the host library or receipt.");
		}
		Request.OwningBinary = {
		    {"id", Text(Binary, "id")}, {"path", PathToUtf8(Request.BinaryPath)}, {"kind", Text(Binary, "kind")}};
	}
	if (Request.Context.contains("linkVerification"))
	{
		RequireChoice(Request.Context, "linkVerification", {"PeExportV1"});
		if (Request.OwningBinary.is_null() || Request.Platform != "Win64" ||
		    Request.Context.at("compiler").value("responseFormat", "") != "msvc")
		{
			throw std::runtime_error("PeExportV1 requires an owning Win64 image and an MSVC compiler recipe.");
		}
		Request.bPeLinkWitness = true;
	}
	AddInput(Request, Request.ContextPath);
	AddInput(Request, Request.ManifestPath);
	AddInput(Request, ExecutablePath());
	if (bManifestV2)
	{
		ValidateGenerationInputs(Request);
	}
	std::map<fs::path, std::string> Owners;
	std::set<fs::path> PublicPaths;
	const FJson& Files = Request.Manifest.at("generatedFiles");
	if (!Files.is_array())
	{
		throw std::runtime_error("generatedFiles must be an array.");
	}
	for (const FJson& File : Files)
	{
		if (!File.contains("ownerModule"))
		{
			throw std::runtime_error("Every generated file must declare ownerModule, including null for shared files.");
		}
		const fs::path Path = ResolveGeneratedPath(Request.Root, Text(File, "path"));
		const std::string Expected = Text(File, "contentHash");
		RequireDigest(Expected, "contentHash");
		AddInput(Request, Path);
		if (Request.Inputs.at(Path) != Expected || Owners.contains(Path))
		{
			throw std::runtime_error("Generated file hash mismatch or duplicate path: " + PathToUtf8(Path));
		}
		Owners.emplace(Path, File.value("ownerModule", FJson()).is_null() ? "" : Text(File, "ownerModule"));
		const std::string Role = File.value("role", "");
		if (bManifestV2)
		{
			RequireChoice(File, "role",
			              {"HostBinding", "HostRegistration", "CompileVerification", "HostHeader", "SdkHeader",
			               "Contract", "SdkResource", "Diagnostic"});
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
				Request.PublicArtifacts.push_back({{"sourcePath", PathToUtf8(Path)},
				                                   {"relativePath", PathToUtf8(Path.lexically_relative(Request.Root))},
				                                   {"contentHash", Expected},
				                                   {"role", Role},
				                                   {"ownerModule", File.at("ownerModule")}});
			}
		}
	}
	std::set<fs::path> SeenUnits;
	std::set<std::string> UnitOwners;
	const FJson& Units = Request.Manifest.at("compilationUnits");
	if (!Units.is_array())
	{
		throw std::runtime_error("compilationUnits must be an array.");
	}
	for (const FJson& Unit : Units)
	{
		const std::string Owner = Text(Unit, "ownerModule");
		UnitOwners.insert(Owner);
		const fs::path Source = ResolveGeneratedPath(Request.Root, Text(Unit, "source"));
		if (!Owners.contains(Source) || Owners.at(Source) != Owner || !SeenUnits.insert(Source).second ||
		    PublicPaths.contains(Source) ||
		    (Source.extension() != ".cpp" && Source.extension() != ".cc" && Source.extension() != ".cxx"))
		{
			throw std::runtime_error("Invalid or duplicate host compilation unit: " + PathToUtf8(Source));
		}
		if (Owner == Request.Owner)
		{
			Request.Sources.push_back(Source);
		}
	}
	if (bManifestV2)
	{
		const FJson& Contexts = Request.Manifest.at("hostBuildContexts");
		if (!Contexts.is_object() || Contexts.size() != UnitOwners.size())
		{
			throw std::runtime_error("hostBuildContexts must cover exactly the generated compilation owners.");
		}
		for (const auto& Owner : UnitOwners)
		{
			RequireDigest(Text(Contexts, Owner), "hostBuildContexts." + Owner);
		}
	}
	if (Request.Sources.empty())
	{
		throw std::runtime_error("The owner needs a compilation unit, including for an empty registration batch.");
	}
	const FJson& Dependencies = Request.Context.at("dependencies");
	if (!Dependencies.is_array())
	{
		throw std::runtime_error("Context dependencies must be an array.");
	}
	for (const FJson& Dependency : Dependencies)
	{
		AddInput(Request, ResolvePath(Request.ContextPath.parent_path(), Dependency.get<std::string>()));
	}
	ValidateRecipe(Request, "compiler");
	ValidateRecipe(Request, "archiver");
	const FJson& Environment = Request.Context.at("environment");
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
		Request.Environment = CaptureEnvironment();
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
	Request.EnvironmentHash = HashBytes(FJson(Request.Environment).dump());
	Request.Inputs[fs::path("<effective-environment>")] = Request.EnvironmentHash;
	if (Request.Inputs.contains(Request.Library) || Request.Inputs.contains(Request.Receipt) ||
	    (!Request.BinaryPath.empty() && Request.Inputs.contains(Request.BinaryPath)))
	{
		throw std::runtime_error("A build output would replace a build input.");
	}
	return Request;
}

FJson InputList(const std::map<fs::path, std::string>& Inputs)
{
	FJson Result = FJson::array();
	for (const auto& [Path, Digest] : Inputs)
	{
		Result.push_back({{"path", PathToUtf8(Path)}, {"contentHash", Digest}});
	}
	return Result;
}

std::string Fingerprint(const std::map<fs::path, std::string>& Inputs)
{
	return HashBytes(FJson({{"toolVersion", ToolVersion}, {"inputs", InputList(Inputs)}}).dump());
}

constexpr std::string_view LinkWitnessPrefix = "UMF_ModHostLink_";

std::vector<std::string> ArchiveSymbols(const FRequest& Request, const std::string& Witness)
{
	auto Symbols = Request.RequiredSymbols;
	if (Request.bPeLinkWitness)
	{
		if (!Witness.starts_with(LinkWitnessPrefix))
		{
			throw std::runtime_error("Host archive is missing its PE link witness.");
		}
		RequireDigest(std::string_view(Witness).substr(LinkWitnessPrefix.size()), "linkWitness");
		Symbols.push_back(Witness);
	}
	return Symbols;
}

FJson MakePlan(const FRequest& Request)
{
	std::map<fs::path, std::string> Inputs = Request.Inputs;
	std::string Reason = "No valid successful receipt.";
	bool bUpToDate = false;
	if (fs::is_regular_file(Request.Receipt))
	{
		try
		{
			const FJson Receipt = ReadJson(Request.Receipt);
			RequireFormat(Receipt, "UMF.ModHostReceipt");
			if (Receipt.at("contextHash") != Request.ContextHash ||
			    Receipt.at("generationId") != Request.GenerationId ||
			    Receipt.at("toolVersion").get<std::string>() != ToolVersion ||
			    Receipt.at("ownerModule") != Request.Owner || Receipt.at("library") != PathToUtf8(Request.Library) ||
			    Receipt.at("target") != Request.Context.at("target") ||
			    Receipt.at("requiredSymbols") != Request.RequiredSymbols ||
			    Receipt.at("publicArtifacts") != Request.PublicArtifacts ||
			    Receipt.at("owningBinary") != Request.OwningBinary ||
			    Receipt.at("contractHash") != Request.Manifest.at("contractHash") ||
			    Receipt.at("projectionHash") != Request.Manifest.at("projectionHash") ||
			    Receipt.at("nativeAbiHash") != Request.Manifest.at("nativeAbiHash"))
			{
				Reason = "Build identity changed.";
			}
			else if (Receipt.value("environmentHash", "") != Request.EnvironmentHash)
			{
				Reason = "Effective compiler environment changed.";
			}
			else
			{
				for (const FJson& File : Receipt.at("compilerDependencies"))
				{
					const fs::path Path = ResolvePath(Request.WorkingDirectory, Text(File, "path"));
					Inputs[Path] = fs::is_regular_file(Path) ? HashFile(Path) : "missing";
				}
				if (Receipt.at("inputFingerprint") != Fingerprint(Inputs))
				{
					Reason = "Compilation inputs changed or disappeared.";
				}
				else if (!fs::is_regular_file(Request.Library) ||
				         Receipt.at("libraryHash") != HashFile(Request.Library))
				{
					Reason = "Host archive is missing or its hash changed.";
				}
				else
				{
					ValidateHostArchive(Request.Library, Request.Platform, Request.Architecture,
					                    ArchiveSymbols(Request, Receipt.value("linkWitness", "")));
					bUpToDate = true;
					Reason = "All recorded inputs and the host archive match.";
				}
			}
		}
		catch (const std::exception& Error)
		{
			Reason = std::string("Invalid cached output: ") + Error.what();
		}
	}
	return {{"format", "UMF.ModHostPlan"},
	        {"schemaVersion", 1},
	        {"toolVersion", ToolVersion},
	        {"ownerModule", Request.Owner},
	        {"target", Request.Context.at("target")},
	        {"contextHash", Request.ContextHash},
	        {"generationId", Request.GenerationId},
	        {"inputFingerprint", Fingerprint(Inputs)},
	        {"environmentHash", Request.EnvironmentHash},
	        {"library", PathToUtf8(Request.Library)},
	        {"receipt", PathToUtf8(Request.Receipt)},
	        {"registrationAnchor", Request.Anchor},
	        {"manifestSchemaVersion", Request.Manifest.at("schemaVersion")},
	        {"requiredSymbols", Request.RequiredSymbols},
	        {"requiredBindings", Request.RequiredBindings},
	        {"publicArtifacts", Request.PublicArtifacts},
	        {"owningBinary", Request.OwningBinary},
	        {"contractHash", Request.Manifest.at("contractHash")},
	        {"projectionHash", Request.Manifest.at("projectionHash")},
	        {"nativeAbiHash", Request.Manifest.at("nativeAbiHash")},
	        {"compilationUnitCount", Request.Sources.size()},
	        {"upToDate", bUpToDate},
	        {"reason", Reason},
	        {"linkRequirement",
	         {{"kind", "OwnerBinaryOnly"},
	          {"ownerModule", Request.Owner},
	          {"owningBinary", Request.OwningBinary},
	          {"library", PathToUtf8(Request.Library)},
	          {"retainSymbols", FJson::array({Request.Anchor})},
	          {"producesImportLibrary", false}}}};
}

std::vector<std::string> ExpandArguments(const FJson& Recipe, const std::map<std::string, std::string>& Values,
                                         const std::vector<fs::path>& Objects = {})
{
	std::vector<std::string> Result;
	for (const FJson& Item : Recipe.at("arguments"))
	{
		std::string Argument = Item.get<std::string>();
		if (Argument == "{objects}")
		{
			for (const fs::path& Object : Objects)
			{
				Result.push_back(PathToUtf8(Object));
			}
			continue;
		}
		for (const auto& [Marker, Value] : Values)
		{
			std::size_t Position = 0;
			while ((Position = Argument.find(Marker, Position)) != std::string::npos)
			{
				Argument.replace(Position, Marker.size(), Value);
				Position += Value.size();
			}
		}
		Result.push_back(std::move(Argument));
	}
	return Result;
}

void ExecuteRecipe(const FRequest& Request, std::string_view Name, const fs::path& Response,
                   const std::map<std::string, std::string>& Values, const std::vector<fs::path>& Objects = {},
                   const std::string& LinkWitness = {})
{
	const FJson& Recipe = Request.Context.at(std::string(Name));
	auto Arguments = ExpandArguments(Recipe, Values, Objects);
	if (!LinkWitness.empty())
	{
		Arguments.push_back("/DUMF_MOD_HOST_LINK_WITNESS=" + LinkWitness);
	}
	WriteResponseFile(Response, Arguments, Text(Recipe, "responseFormat"));
	const int Code = RunProcess(PathFromUtf8(Text(Recipe, "path")), {"@" + PathToUtf8(Response)},
	                            Request.WorkingDirectory, Request.Environment);
	if (Code != 0)
	{
		throw std::runtime_error(std::string(Name) + " failed with exit code " + std::to_string(Code) + ".");
	}
}

std::vector<fs::path> ReadCompilerDependencies(const FRequest& Request, const fs::path& Path)
{
	std::vector<fs::path> Result;
	if (Text(Request.Context.at("compiler"), "dependencyFormat") == "msvc-json")
	{
		const FJson Data = ReadJson(Path).at("Data");
		if ((!Data.value("ImportedModules", FJson::array()).empty()) ||
		    (!Data.value("ImportedHeaderUnits", FJson::array()).empty()))
		{
			throw std::runtime_error("C++ module/header-unit dependencies need a separate MBT profile.");
		}
		for (const FJson& Include : Data.at("Includes"))
		{
			Result.push_back(ResolvePath(Request.WorkingDirectory, Include.get<std::string>()));
		}
	}
	else
	{
		const std::string Contents = ReadFile(Path);
		std::string Rule;
		for (std::size_t Index = 0; Index < Contents.size(); ++Index)
		{
			if (Contents[Index] == '\\' && Index + 1 < Contents.size() &&
			    (Contents[Index + 1] == '\n' || Contents[Index + 1] == '\r'))
			{
				++Index;
				if (Contents[Index] == '\r' && Index + 1 < Contents.size() && Contents[Index + 1] == '\n')
				{
					++Index;
				}
				Rule.push_back(' ');
			}
			else if (Contents[Index] == '\n' || Contents[Index] == '\r')
			{
				break;
			}
			else
			{
				Rule.push_back(Contents[Index]);
			}
		}
		std::size_t Colon = std::string::npos;
		for (std::size_t Index = 0; Index < Rule.size(); ++Index)
		{
			if (Rule[Index] == '\\')
			{
				++Index;
			}
			else if (Rule[Index] == ':' &&
			         (Index + 1 == Rule.size() || std::isspace(static_cast<unsigned char>(Rule[Index + 1]))))
			{
				Colon = Index;
				break;
			}
		}
		if (Colon == std::string::npos)
		{
			throw std::runtime_error("Invalid Make dependency rule.");
		}
		std::string Token;
		for (std::size_t Index = Colon + 1; Index <= Rule.size(); ++Index)
		{
			if (Index < Rule.size() && Rule[Index] == '\\' && Index + 1 < Rule.size())
			{
				Token.push_back(Rule[++Index]);
			}
			else if (Index < Rule.size() && Rule[Index] == '$' && Index + 1 < Rule.size() && Rule[Index + 1] == '$')
			{
				Token.push_back('$');
				++Index;
			}
			else if (Index == Rule.size() || std::isspace(static_cast<unsigned char>(Rule[Index])))
			{
				if (!Token.empty())
				{
					Result.push_back(ResolvePath(Request.WorkingDirectory, Token));
					Token.clear();
				}
			}
			else
			{
				Token.push_back(Rule[Index]);
			}
		}
	}
	return Result;
}

struct FOwnedStage
{
	fs::path Path;
	fs::path Parent;
	~FOwnedStage()
	{
		std::error_code Ignored;
		const fs::path Resolved = fs::weakly_canonical(Path, Ignored);
		if (!Ignored && Path.is_absolute() && Path.parent_path() == Parent && Resolved.parent_path() == Parent &&
		    Path.filename().string().starts_with(".mbt-stage-"))
		{
			fs::remove_all(Path, Ignored);
		}
	}
};
} // namespace

std::string HashHostContext(const fs::path& Context)
{
	const FJson Document = ReadJson(Context);
	RequireFormat(Document, "UMF.ModHostContext");
	return HashBytes(Document.dump());
}

FJson PlanHostBuild(const fs::path& Context, const fs::path& Manifest)
{
	return MakePlan(ReadRequest(Context, Manifest));
}

FJson BuildHost(const fs::path& Context, const fs::path& Manifest, std::string_view ExpectedInput)
{
	const FBuildLock Generation(fs::canonical(Manifest).parent_path() / ".mht.lock", "");
	FRequest Request = ReadRequest(Context, Manifest);
	FBuildLock Lock(Request.Library);
	const FJson Plan = MakePlan(Request);
	if (!ExpectedInput.empty() && ExpectedInput != Plan.at("inputFingerprint").get<std::string>())
	{
		throw std::runtime_error("InputsChanged: this MBT action no longer matches its planned input fingerprint.");
	}
	if (Plan.at("upToDate").get<bool>())
	{
		FJson Result = Plan;
		Result["status"] = "UpToDate";
		return Result;
	}
	const fs::path Work = Request.Library.parent_path() / (".mbt-stage-" + UniqueSuffix());
	if (!fs::create_directory(Work))
	{
		throw std::runtime_error("Cannot create a fresh owned build stage.");
	}
	FOwnedStage Stage{Work, Request.Library.parent_path()};
	std::vector<fs::path> Objects;
	std::set<fs::path> CompilerDependencies;
	const std::string LinkWitness =
	    Request.bPeLinkWitness
	        ? std::string(LinkWitnessPrefix) + HashBytes(Request.ContextHash + Request.GenerationId + UniqueSuffix())
	        : std::string();
	const auto Started = fs::file_time_type::clock::now();
	for (std::size_t Index = 0; Index < Request.Sources.size(); ++Index)
	{
		const fs::path& Source = Request.Sources[Index];
		const fs::path Object = Work / (std::to_string(Index) + (Request.Platform == "Win64" ? ".obj" : ".o"));
		const fs::path Dependencies = Work / (std::to_string(Index) + ".deps");
		std::cerr << "ModBuildTool: compiling " << PathToUtf8(Source) << '\n';
		ExecuteRecipe(Request, "compiler", Work / (std::to_string(Index) + ".rsp"),
		              {{"{source}", PathToUtf8(Source)},
		               {"{object}", PathToUtf8(Object)},
		               {"{dependencies}", PathToUtf8(Dependencies)}},
		              {}, LinkWitness);
		if (!fs::is_regular_file(Object))
		{
			throw std::runtime_error("Compiler did not produce its declared object file.");
		}
		for (const fs::path& Dependency : ReadCompilerDependencies(Request, Dependencies))
		{
			if (fs::last_write_time(Dependency) > Started)
			{
				throw std::runtime_error("InputsChanged: a compiler dependency changed during the build.");
			}
			CompilerDependencies.insert(Dependency);
		}
		Objects.push_back(Object);
	}
	const fs::path Archive = Work / Request.Library.filename();
	ExecuteRecipe(Request, "archiver", Work / "archive.rsp", {{"{output}", PathToUtf8(Archive)}}, Objects);
	ValidateHostArchive(Archive, Request.Platform, Request.Architecture, ArchiveSymbols(Request, LinkWitness));
	std::map<fs::path, std::string> Dependencies;
	for (const fs::path& Path : CompilerDependencies)
	{
		if (fs::last_write_time(Path) > Started)
		{
			throw std::runtime_error("InputsChanged: a compiler dependency changed before publication.");
		}
		Dependencies[Path] = HashFile(Path);
	}
	const FRequest Current = ReadRequest(Context, Manifest);
	if (Current.Inputs != Request.Inputs)
	{
		throw std::runtime_error("InputsChanged: build inputs changed before publication.");
	}
	for (const auto& [Path, Digest] : Dependencies)
	{
		Request.Inputs[Path] = Digest;
	}
	const FJson Receipt = {{"format", "UMF.ModHostReceipt"},
	                       {"schemaVersion", 1},
	                       {"toolVersion", ToolVersion},
	                       {"ownerModule", Request.Owner},
	                       {"target", Request.Context.at("target")},
	                       {"contextHash", Request.ContextHash},
	                       {"generationId", Request.GenerationId},
	                       {"inputFingerprint", Fingerprint(Request.Inputs)},
	                       {"environmentHash", Request.EnvironmentHash},
	                       {"library", PathToUtf8(Request.Library)},
	                       {"libraryHash", HashFile(Archive)},
	                       {"linkWitness", LinkWitness},
	                       {"registrationAnchor", Request.Anchor},
	                       {"requiredSymbols", Request.RequiredSymbols},
	                       {"publicArtifacts", Request.PublicArtifacts},
	                       {"owningBinary", Request.OwningBinary},
	                       {"contractHash", Request.Manifest.at("contractHash")},
	                       {"projectionHash", Request.Manifest.at("projectionHash")},
	                       {"nativeAbiHash", Request.Manifest.at("nativeAbiHash")},
	                       {"compilerDependencies", InputList(Dependencies)}};
	// The receipt is the commit record. A partial publish never validates.
	ReplaceFileAtomically(Archive, Request.Library);
	WriteFileAtomically(Request.Receipt, Receipt.dump(2) + "\n");
	FJson Result = MakePlan(Request);
	if (!Result.at("upToDate").get<bool>())
	{
		throw std::runtime_error("Published archive failed receipt verification.");
	}
	Result["status"] = "Built";
	return Result;
}

namespace
{
FJson CurrentHostPlan(const FRequest& Request)
{
	if (Request.Manifest.at("schemaVersion") != 2)
	{
		throw std::runtime_error("finalize-host requires a v2 generated-artifact manifest.");
	}
	if (Request.OwningBinary.is_null())
	{
		throw std::runtime_error("finalize-host requires owningBinary in the host context.");
	}
	const FJson Plan = MakePlan(Request);
	if (!Plan.at("upToDate").get<bool>())
	{
		throw std::runtime_error("Cannot finalize a stale host build: " + Text(Plan, "reason"));
	}
	return Plan;
}

FJson VerifyLink(const FRequest& Request, const FJson& Plan, const fs::path& LinkPath, const FJson& Record,
                 const std::string& LinkHash)
{
	RequireFormat(Record, "UMF.ModHostLinkReceipt");
	if (Record.value("status", "") != "Succeeded" || Record.at("target") != Request.Context.at("target"))
	{
		throw std::runtime_error("The link record is unsuccessful or belongs to another Target.");
	}
	const FJson& Binary = Record.at("binary");
	const fs::path BinaryPath = ResolvePath(LinkPath.parent_path(), Text(Binary, "path"));
	if (Text(Binary, "id") != Text(Request.OwningBinary, "id") ||
	    Text(Binary, "kind") != Text(Request.OwningBinary, "kind") || !fs::equivalent(BinaryPath, Request.BinaryPath))
	{
		throw std::runtime_error("The linked image is not the context's owning binary.");
	}
	const FJson& Modules = Binary.at("ownerModules");
	if (!Modules.is_array() || Modules.empty())
	{
		throw std::runtime_error("The link record must identify its owning modules.");
	}
	std::set<std::string> SeenModules;
	for (const FJson& Module : Modules)
	{
		const std::string Name = Module.get<std::string>();
		if (Name.empty() || !SeenModules.insert(Name).second)
		{
			throw std::runtime_error("Invalid or duplicate owning module in link record.");
		}
	}
	if (!SeenModules.contains(Request.Owner))
	{
		throw std::runtime_error("The linked binary does not own this host module.");
	}
	const FJson& Bindings = Record.at("hostLibraries");
	if (!Bindings.is_array())
	{
		throw std::runtime_error("hostLibraries must be an array.");
	}
	const FJson* Binding = nullptr;
	std::set<std::string> SeenLibraries;
	for (const FJson& Candidate : Bindings)
	{
		const std::string Owner = Text(Candidate, "ownerModule");
		if (!SeenLibraries.insert(Owner).second)
		{
			throw std::runtime_error("Duplicate host library owner in link record: " + Owner);
		}
		if (Owner == Request.Owner)
		{
			Binding = &Candidate;
		}
	}
	if (Binding == nullptr)
	{
		throw std::runtime_error("The successful link record does not include this host archive.");
	}
	const std::string HostReceiptHash = HashFile(Request.Receipt);
	const FJson HostReceipt = ReadJson(Request.Receipt);
	const fs::path LinkedLibrary = ResolvePath(LinkPath.parent_path(), Text(*Binding, "path"));
	if (!fs::equivalent(LinkedLibrary, Request.Library) ||
	    Binding->at("contentHash") != HostReceipt.at("libraryHash") ||
	    Text(*Binding, "hostReceiptHash") != HostReceiptHash || Text(*Binding, "contextHash") != Request.ContextHash ||
	    Text(*Binding, "generationId") != Request.GenerationId ||
	    Text(*Binding, "registrationAnchor") != Request.Anchor || !Binding->at("anchorRetained").get<bool>())
	{
		throw std::runtime_error("Linked archive identity, receipt, batch, or anchor retention does not match.");
	}
	const std::string BinaryHash = Text(Binary, "contentHash");
	RequireDigest(BinaryHash, "binary.contentHash");
	const std::string Witness = Request.bPeLinkWitness ? Text(HostReceipt, "linkWitness") : "";
	if (Request.bPeLinkWitness && Record.value("linkWitness", Witness) != Witness)
	{
		throw std::runtime_error("The link record names a different PE link witness.");
	}
	ValidateHostBinary(BinaryPath, Request.Platform, Request.Architecture, Text(Binary, "kind"), Witness);
	if (HashFile(BinaryPath) != BinaryHash || HashFile(Request.Library) != Text(HostReceipt, "libraryHash") ||
	    HashFile(Request.Receipt) != HostReceiptHash ||
	    ReadRequest(Request.ContextPath, Request.ManifestPath).Inputs != Request.Inputs)
	{
		throw std::runtime_error("InputsChanged: linked artifacts or their input records changed during finalization.");
	}
	const FJson CurrentPlan = MakePlan(Request);
	if (!CurrentPlan.at("upToDate").get<bool>() || CurrentPlan.at("inputFingerprint") != Plan.at("inputFingerprint"))
	{
		throw std::runtime_error("InputsChanged: compiler dependencies changed during finalization.");
	}
	FJson Result = Plan;
	Result["format"] = "UMF.ModHostLinkedArtifacts";
	Result["status"] = "Linked";
	Result["binary"] = {{"id", Text(Binary, "id")},
	                    {"path", PathToUtf8(BinaryPath)},
	                    {"kind", Text(Binary, "kind")},
	                    {"contentHash", BinaryHash},
	                    {"ownerModules", Modules}};
	Result["libraryHash"] = HostReceipt.at("libraryHash");
	Result["hostBuildReceipt"] = {{"path", PathToUtf8(Request.Receipt)}, {"contentHash", HostReceiptHash}};
	Result["linkReceipt"] = {{"path", PathToUtf8(LinkPath)}, {"contentHash", LinkHash}};
	return Result;
}

fs::path UeProductPath(const fs::path& ProjectRoot, std::string_view Path)
{
	constexpr std::string_view ProjectPrefix = "$(ProjectDir)/";
	return Path.starts_with(ProjectPrefix) ? ResolvePath(ProjectRoot, Path.substr(ProjectPrefix.size())) : fs::path();
}

void RequireUeBinary(const FRequest& Request, const FJson& TargetReceipt, const fs::path& ProjectRoot)
{
	const FJson& Target = Request.Context.at("target");
	for (const auto& [ReceiptKey, ContextKey] : {std::pair{"TargetName", "name"},
	                                             {"TargetType", "type"},
	                                             {"Platform", "platform"},
	                                             {"Architecture", "architecture"},
	                                             {"Configuration", "configuration"}})
	{
		if (Text(TargetReceipt, ReceiptKey) != Text(Target, ContextKey))
		{
			throw std::runtime_error("UBT target receipt does not match the host context: " + std::string(ReceiptKey));
		}
	}
	const FJson& Products = TargetReceipt.at("BuildProducts");
	const std::string Kind = Text(Request.OwningBinary, "kind");
	const bool bOwnsProduct =
	    std::any_of(Products.begin(), Products.end(),
	                [&](const FJson& Product)
	                {
		                return Product.value("Type", "") == Kind &&
		                       UeProductPath(ProjectRoot, Text(Product, "Path")) == Request.BinaryPath;
	                });
	if (!bOwnsProduct)
	{
		throw std::runtime_error("UBT target receipt has no matching owning binary: " + PathToUtf8(Request.BinaryPath));
	}
	if (Kind == "Executable")
	{
		if (Text(Target, "linkType") == "Monolithic" &&
		    UeProductPath(ProjectRoot, Text(TargetReceipt, "Launch")) == Request.BinaryPath)
		{
			return;
		}
	}
	else if (Text(Target, "linkType") == "Modular")
	{
		for (const auto& Product : Products)
		{
			if (Product.value("Type", "") != "RequiredResource")
			{
				continue;
			}
			const auto Path = UeProductPath(ProjectRoot, Text(Product, "Path"));
			if (Path.extension() == ".modules" && Path.parent_path() == Request.BinaryPath.parent_path())
			{
				const auto Modules = ReadJson(Path).at("Modules");
				if (Modules.value(Request.Owner, "") == PathToUtf8(Request.BinaryPath.filename()))
				{
					return;
				}
			}
		}
	}
	throw std::runtime_error("UBT binary ownership could not be verified for module: " + Request.Owner);
}
} // namespace

FJson FinalizeHost(const fs::path& Context, const fs::path& Manifest, const fs::path& LinkReceipt)
{
	const FBuildLock Generation(fs::canonical(Manifest).parent_path() / ".mht.lock", "");
	const FRequest Request = ReadRequest(Context, Manifest);
	const FBuildLock Library(Request.Library);
	const FJson Plan = CurrentHostPlan(Request);
	const fs::path LinkPath = fs::canonical(LinkReceipt);
	const std::string Bytes = ReadFile(LinkPath);
	const std::string LinkHash = HashBytes(Bytes);
	FJson Result = VerifyLink(Request, Plan, LinkPath, FJson::parse(Bytes), LinkHash);
	if (HashFile(LinkPath) != LinkHash)
	{
		throw std::runtime_error("InputsChanged: the link record changed during finalization.");
	}
	return Result;
}

void RefreshUeWatches(const fs::path& ContextSetPath)
{
	if (!fs::is_regular_file(ContextSetPath))
	{
		return;
	}
	const fs::path ContextSet = fs::canonical(ContextSetPath);
	const FJson Pipeline = FJson::parse(ReadFile(ContextSet), nullptr, false);
	if (!Pipeline.is_object() || !Pipeline.contains("generatedManifest"))
	{
		return;
	}
	const fs::path Manifest = ResolvePath(ContextSet.parent_path(), Text(Pipeline, "generatedManifest"));
	if (!fs::is_regular_file(Manifest))
	{
		return;
	}
	const FJson Batch = FJson::parse(ReadFile(Manifest), nullptr, false);
	if (!Batch.is_object() || Batch.value("status", "") != "Complete" || !Batch.contains("watchedDirectories") ||
	    !Batch.contains("inputFiles"))
	{
		return;
	}
	fs::path Changed = ChangedWatchedDirectory(Batch.at("watchedDirectories"));
	if (Changed.empty())
	{
		for (const auto& File : Batch.at("inputFiles"))
		{
			if (File.at("contentHash") == "")
			{
				const auto Path = PathFromUtf8(Text(File, "path"));
				if (fs::exists(Path))
				{
					Changed = Path;
					break;
				}
			}
		}
	}
	if (!Changed.empty())
	{
		WriteFileAtomically(ContextSet.parent_path() / ".mht-watch.stamp", UniqueSuffix() + '\n');
		std::cerr << "ModBuildTool: MHT watch changed: " << PathToUtf8(Changed) << '\n';
	}
}

FJson FinalizeUeTarget(const fs::path& ContextSetPath, const fs::path& TargetReceiptPath)
{
	const fs::path ContextSet = fs::canonical(ContextSetPath);
	const FJson Pipeline = ReadJson(ContextSet);
	RequireFormat(Pipeline, "UMF.ModHostContextSet");
	const fs::path Manifest = ResolvePath(ContextSet.parent_path(), Text(Pipeline, "generatedManifest"));
	const fs::path Project = ResolvePath(ContextSet.parent_path(), Text(Pipeline, "project"));
	const fs::path UbtReceipt = fs::canonical(TargetReceiptPath);
	const std::string UbtBytes = ReadFile(UbtReceipt);
	const FJson Target = FJson::parse(UbtBytes);
	if (ResolvePath(UbtReceipt.parent_path(), Text(Target, "Project")) != Project)
	{
		throw std::runtime_error("UBT target receipt belongs to a different project.");
	}
	const FBuildLock Generation(Manifest.parent_path() / ".mht.lock", "");
	const auto ContextHashes = ReadJson(Manifest).at("hostBuildContexts");
	const auto& Contexts = Pipeline.at("contexts");
	if (!Contexts.is_object() || Contexts.size() != ContextHashes.size())
	{
		throw std::runtime_error("UE finalization requires every generated producer context.");
	}
	FJson Results = FJson::array();
	const auto UbtHash = HashBytes(UbtBytes);
	for (const auto& [Owner, Entry] : Contexts.items())
	{
		const fs::path Context = ResolvePath(ContextSet.parent_path(), Entry.get<std::string>());
		const auto Recipe = ReadJson(Context);
		if (Text(Recipe, "ownerModule") != Owner || HashBytes(Recipe.dump()) != Text(ContextHashes, Owner))
		{
			throw std::runtime_error("UE finalization context does not match the generation: " + Owner);
		}
		if (Recipe.value("clientAbi", "") == "ContextHandlesV1" && !Recipe.contains("owningBinary"))
		{
			continue;
		}
		const FRequest Request = ReadRequest(Context, Manifest);
		const FBuildLock Library(Request.Library);
		const FJson Plan = CurrentHostPlan(Request);
		if (!Request.bPeLinkWitness)
		{
			throw std::runtime_error("UE finalization requires PeExportV1 link verification.");
		}
		RequireUeBinary(Request, Target, Project.parent_path());
		const auto HostBytes = ReadFile(Request.Receipt);
		const auto Host = FJson::parse(HostBytes);
		const fs::path LinkPath = fs::path(Request.Library).concat(".link.json");
		const fs::path LinkedPath = fs::path(Request.Library).concat(".linked.json");
		FJson Binary = Request.OwningBinary;
		Binary["ownerModules"] = {Owner};
		Binary["contentHash"] = HashFile(Request.BinaryPath);
		const FJson Record = {{"format", "UMF.ModHostLinkReceipt"},
		                      {"schemaVersion", 1},
		                      {"status", "Succeeded"},
		                      {"target", Request.Context.at("target")},
		                      {"binary", Binary},
		                      {"ueTargetReceipt", {{"path", PathToUtf8(UbtReceipt)}, {"contentHash", UbtHash}}},
		                      {"linkWitness", Text(Host, "linkWitness")},
		                      {"hostLibraries", FJson::array({{{"ownerModule", Owner},
		                                                       {"path", PathToUtf8(Request.Library)},
		                                                       {"contentHash", Host.at("libraryHash")},
		                                                       {"hostReceiptHash", HashBytes(HostBytes)},
		                                                       {"contextHash", Request.ContextHash},
		                                                       {"generationId", Request.GenerationId},
		                                                       {"registrationAnchor", Request.Anchor},
		                                                       {"anchorRetained", true}}})}};
		const std::string LinkBytes = Record.dump(2) + '\n';
		const FJson Linked = VerifyLink(Request, Plan, LinkPath, Record, HashBytes(LinkBytes));
		if (HashFile(UbtReceipt) != UbtHash)
		{
			throw std::runtime_error("InputsChanged: the UBT target receipt changed during finalization.");
		}
		WriteFileAtomically(LinkPath, LinkBytes);
		WriteFileAtomically(LinkedPath, Linked.dump(2) + '\n');
		Results.push_back(
		    {{"ownerModule", Owner}, {"binary", Request.OwningBinary}, {"linkedArtifacts", PathToUtf8(LinkedPath)}});
	}
	if (Results.empty())
	{
		throw std::runtime_error("UE finalization found no owning modules.");
	}
	return Results;
}

void ValidateResultOutput(const fs::path& Output, const fs::path& Context, const fs::path& Manifest,
                          const fs::path& LinkReceipt)
{
	if (Output.empty())
	{
		return;
	}
	const fs::path Destination = fs::weakly_canonical(fs::absolute(Output));
	const FRequest Request = ReadRequest(Context, Manifest);
	if (Request.Inputs.contains(Destination) || Destination == Request.Library || Destination == Request.Receipt ||
	    (!Request.BinaryPath.empty() && Destination == Request.BinaryPath) ||
	    (!LinkReceipt.empty() && Destination == fs::canonical(LinkReceipt)))
	{
		throw std::runtime_error("JSON output would overwrite a build input or artifact.");
	}
}

void WriteResult(const FJson& Result, const fs::path& Output, const fs::path& Context, const fs::path& Manifest,
                 const fs::path& LinkReceipt)
{
	if (Output.empty())
	{
		std::cout << Result.dump(2) << '\n';
		if (!std::cout)
		{
			throw std::runtime_error("Cannot write the JSON result.");
		}
		return;
	}
	ValidateResultOutput(Output, Context, Manifest, LinkReceipt);
	WriteFileAtomically(fs::weakly_canonical(fs::absolute(Output)), Result.dump(2) + "\n");
}
} // namespace Mod::BuildTool
