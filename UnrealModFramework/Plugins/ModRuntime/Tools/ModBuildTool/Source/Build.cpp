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
	std::string NativeABIHash;
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
	RequireFormat(Request.Context, "UMF.ModHostContext", {2});
	RequireFormat(Request.Manifest, "UMF.GeneratedArtifacts", {1, 2, 3});
	const int ManifestVersion = Request.Manifest.at("schemaVersion").get<int>();
	const bool bHasBatchMetadata = ManifestVersion >= 2;
	if (Request.Manifest.value("status", "") != "Complete" || !Request.Manifest.contains("unresolvedTypeCount") ||
	    Request.Manifest["unresolvedTypeCount"] != 0)
	{
		throw std::runtime_error("A host build requires a Complete batch with zero unresolved types.");
	}
	Request.Owner = Text(Request.Context, "ownerModule");
	Request.ContextHash = HashBytes(Request.Context.dump());
	const std::string ExpectedContext = bHasBatchMetadata || Request.Manifest.contains("hostBuildContexts")
	                                        ? Text(Request.Manifest.at("hostBuildContexts"), Request.Owner)
	                                        : Text(Request.Manifest, "buildContextHash");
	if (ExpectedContext != Request.ContextHash)
	{
		throw std::runtime_error("Generated batch does not match the exact host compilation context.");
	}
	Request.GenerationId = Text(Request.Manifest, "generationId");
	RequireDigest(Request.GenerationId, "generationId");
	for (std::string_view Key : {"inputHash", "contractHash", "projectionHash"})
	{
		RequireDigest(Text(Request.Manifest, Key), Key);
	}
	// Generated artifact schemas v1 and v2 use the old acronym casing.
	Request.NativeABIHash = Text(Request.Manifest, ManifestVersion >= 3 ? "nativeABIHash"
	                                                                    : "nativeA"
	                                                                      "biHash");
	RequireDigest(Request.NativeABIHash, "nativeABIHash");
	Request.Anchor = Text(Request.Context, "registrationAnchor");
	RequireSymbol(Request.Anchor);
	std::set<std::string> Symbols{Request.Anchor};
	std::set<std::pair<std::string, std::string>> SymbolRecords;
	std::set<std::pair<std::string, std::string>> ExportIds;
	if (bHasBatchMetadata || Request.Manifest.contains("requiredSymbols"))
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
	if (Request.Context.value("moduleAPIMode", "") != "Producer" || Request.Context.value("lto", true) ||
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
	if (bHasBatchMetadata && (Text(Request.Manifest, "target") != Text(Target, "name") ||
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
	if (bHasBatchMetadata)
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
		if (bHasBatchMetadata)
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
	if (bHasBatchMetadata)
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
			RequireFormat(Receipt, "UMF.ModHostReceipt", {2});
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
			    Receipt.at("nativeABIHash") != Request.NativeABIHash)
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
	        {"schemaVersion", 2},
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
	        {"nativeABIHash", Request.NativeABIHash},
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
} // namespace Mod::BuildTool
