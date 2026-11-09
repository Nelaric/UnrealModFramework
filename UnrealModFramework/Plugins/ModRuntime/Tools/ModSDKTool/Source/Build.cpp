// Copyright (c) 2026 Nelaric Contributors
#include "Sdk.h"

#include "Hash.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace Mod::SdkTool
{
namespace
{
using namespace Mod::BuildTool;

struct FWorkspace
{
	fs::path Root;
	explicit FWorkspace(const FSdkRequest& Request)
	{
		const fs::path Base = Request.Document.contains("workDirectory")
		                          ? ResolvePath(Request.File.parent_path(), Text(Request.Document, "workDirectory"))
		                          : fs::temp_directory_path();
		fs::create_directories(Base);
		Root = Base / PathFromUtf8("umf-mst-" + UniqueSuffix());
		if (!fs::create_directory(Root))
		{
			throw std::runtime_error("Cannot create an exclusive MST workspace.");
		}
	}
	~FWorkspace()
	{
		std::error_code Ignored;
		fs::remove_all(Root, Ignored);
	}
};

void CopyFile(const fs::path& Source, const fs::path& Destination)
{
	fs::create_directories(Destination.parent_path());
	fs::copy_file(Source, Destination);
}

void CopyTree(const fs::path& Source, const fs::path& Destination)
{
	if (!fs::is_directory(Source))
	{
		throw std::runtime_error("Missing package directory: " + PathToUtf8(Source));
	}
	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(Source))
	{
		if (Entry.is_symlink())
		{
			throw std::runtime_error("SDK packages cannot contain symbolic links.");
		}
		if (Entry.is_regular_file())
		{
			CopyFile(Entry.path(), Destination / Entry.path().lexically_relative(Source));
		}
	}
}

void CheckInputs(const FSdkRequest& Request)
{
	for (const FJson& Input : Request.Inputs)
	{
		if (HashFile(PathFromUtf8(Text(Input, "path"))) != Text(Input, "contentHash"))
		{
			throw std::runtime_error("InputsChanged: SDK input changed: " + Text(Input, "path"));
		}
	}
}

FJson Inventory(const fs::path& Root)
{
	FJson Result = FJson::array();
	std::set<std::string> Seen;
	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(Root))
	{
		if (Entry.is_symlink())
		{
			throw std::runtime_error("SDK bundles cannot contain symbolic links.");
		}
		if (Entry.is_regular_file() && Entry.path() != Root / "Manifest.json")
		{
			const std::string Relative = PathToUtf8(Entry.path().lexically_relative(Root));
			if (!Seen.insert(Relative).second)
			{
				throw std::runtime_error("Duplicate bundle inventory path.");
			}
			Result.push_back({{"path", Relative}, {"contentHash", HashFile(Entry.path())}});
		}
	}
	std::sort(Result.begin(), Result.end(), [](const FJson& Left, const FJson& Right)
	          { return Left.at("path").get<std::string>() < Right.at("path").get<std::string>(); });
	return Result;
}

void BuildPlugin(const FSdkRequest& Request, const fs::path& Plugin, const fs::path& Package)
{
	const FJson& Editor = Request.Document.at("editor");
	const int Code = RunTool(Request.Dotnet,
	                         {PathToUtf8(Request.AutomationTool), "BuildPlugin", "-Plugin=" + PathToUtf8(Plugin),
	                          "-Package=" + PathToUtf8(Package), "-HostPlatforms=" + Text(Editor, "platform"),
	                          "-Architecture_" + Text(Editor, "platform") + "=" + Text(Editor, "architecture"),
	                          "-NoTargetPlatforms", "-StrictIncludes", "-Unattended", "-UTF8Output"},
	                         Request.AutomationTool.parent_path(), Request.Document.value("timeoutSeconds", 1800u));
	if (Code != 0)
	{
		throw std::runtime_error("UE BuildPlugin failed with exit code " + std::to_string(Code) + ": " +
		                         PathToUtf8(Plugin));
	}
}

void VerifyEditorModule(const FSdkRequest& Request, const fs::path& Package, const std::string& Module)
{
	const fs::path Binaries = Package / "Binaries";
	if (!fs::is_directory(Binaries))
	{
		throw std::runtime_error("BuildPlugin did not produce Editor binaries.");
	}
	bool bFound = false;
	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(Binaries))
	{
		if (Entry.is_symlink())
		{
			throw std::runtime_error("Unexpected symbolic link in UE binaries.");
		}
		if (!Entry.is_regular_file() || Entry.path().extension() != ".modules")
		{
			continue;
		}
		const FJson Modules = ReadJson(Entry.path());
		if (!Modules.at("Modules").contains(Module))
		{
			continue;
		}
		if (bFound || Modules.at("BuildId") != Request.Document.at("editor").at("buildId"))
		{
			throw std::runtime_error("Duplicate or incompatible UE Editor module: " + Module);
		}
		const std::string BinaryName = Modules.at("Modules").at(Module).get<std::string>();
		if (PathFromUtf8(BinaryName).filename() != PathFromUtf8(BinaryName))
		{
			throw std::runtime_error("UE module metadata contains a non-local binary name.");
		}
		const fs::path Binary = Entry.path().parent_path() / PathFromUtf8(BinaryName);
		const int Code = RunTool(Request.Mbt,
		                         {"inspect-binary", "--file", PathToUtf8(Binary), "--platform",
		                          Text(Request.Document.at("editor"), "platform"), "--architecture",
		                          Text(Request.Document.at("editor"), "architecture"), "--kind", "DynamicLibrary"},
		                         Request.Project.parent_path(), Request.Document.value("timeoutSeconds", 1800u));
		if (Code != 0)
		{
			throw std::runtime_error("MBT rejected the built Editor binary: " + Module);
		}
		bFound = true;
	}
	if (!bFound)
	{
		throw std::runtime_error("Missing successful UE Editor module metadata: " + Module);
	}
}

std::string ClientRelativePath(const FLibrary& Job)
{
	const FJson& Target = Job.Plan.at("target");
	return Text(Target, "platform") + "/" + Text(Target, "architecture") + "/" + Text(Target, "configuration") + "/" +
	       Text(Target, "type") + "/" + Text(Job.Plan, "ownerModule") +
	       PathToUtf8(PathFromUtf8(Text(Job.Plan, "library")).extension());
}

void Publish(const FSdkRequest& Request, const fs::path& Stage)
{
	fs::create_directories(Request.Output.parent_path());
	// Keep the sibling stage short: UE precompiled outputs have deep paths.
	const fs::path Incoming = Request.Output.parent_path() / PathFromUtf8(".mst-" + UniqueSuffix());
	if (!fs::create_directory(Incoming))
	{
		throw std::runtime_error("Cannot create the bundle's sibling publication directory.");
	}
	try
	{
		CopyTree(Stage, Incoming);
		InspectSdk(Incoming);
		if (fs::exists(Request.Output))
		{
			throw std::runtime_error("A published SDK is immutable; choose a new versioned output directory.");
		}
		fs::rename(Incoming, Request.Output);
	}
	catch (...)
	{
		std::error_code Ignored;
		fs::remove_all(Incoming, Ignored);
		throw;
	}
}
} // namespace

FJson InspectSdk(const fs::path& Directory)
{
	using namespace Mod::BuildTool;
	const fs::path Root = fs::canonical(Directory);
	const FJson Manifest = ReadJson(Root / "Manifest.json");
	RequireFormat(Manifest, "UMF.ModSdkBundle");
	const FJson& Files = Manifest.at("files");
	if (!Files.is_array() || Files.empty() || Files.size() > 100000)
	{
		throw std::runtime_error("Bundle file inventory is empty or oversized.");
	}
	RequireDigest(Text(Manifest, "inputFingerprint"), "inputFingerprint");
	std::set<std::string> Seen;
	for (const FJson& File : Files)
	{
		const std::string Relative = Text(File, "path");
		const fs::path Path = ResolveGeneratedPath(Root, Relative);
		RequireDigest(Text(File, "contentHash"), "file.contentHash");
		if (Path == Root / "Manifest.json" || !Seen.insert(Relative).second ||
		    HashFile(Path) != Text(File, "contentHash"))
		{
			throw std::runtime_error("Bundle hash mismatch, self-reference, or duplicate file: " + Relative);
		}
	}
	if (Inventory(Root) != Files)
	{
		throw std::runtime_error("Bundle inventory does not exactly match the published files.");
	}
	const FJson Contract = ReadJson(Root / "Contract" / "SdkContract.json");
	RequireFormat(Contract, "UMF.SdkContract");
	if (Contract.contains("connectionDescriptor"))
	{
		const std::string Relative = Text(Contract, "connectionDescriptor");
		const fs::path Descriptor = ResolveGeneratedPath(Root, Relative);
		const fs::path PluginCopy =
		    ResolveGeneratedPath(Root, "Plugins/" + Text(Manifest, "pluginName") + "/" + Relative);
		const FJson Connection = ReadJson(Descriptor);
		RequireFormat(Connection, "UMF.ClientAbi");
		if (HashFile(Descriptor) != HashFile(PluginCopy) || Connection.at("gameId") != Manifest.at("gameId"))
		{
			throw std::runtime_error("The bundle and SDK plugin need the same native connection descriptor.");
		}
		for (const char* Key : {"contractHash", "projectionHash", "nativeAbiHash"})
		{
			if (Connection.at(Key) != Contract.at(Key) || Contract.at(Key) != Manifest.at(Key))
			{
				throw std::runtime_error(std::string("Published connection identity mismatch: ") + Key);
			}
		}
	}
	return {{"format", "UMF.SdkInspection"},
	        {"schemaVersion", 1},
	        {"valid", true},
	        {"bundle", PathToUtf8(Root)},
	        {"gameId", Manifest.at("gameId")},
	        {"sdkVersion", Manifest.at("sdkVersion")},
	        {"inputFingerprint", Manifest.at("inputFingerprint")},
	        {"capabilities", Manifest.at("capabilities")}};
}

FJson BuildSdk(FSdkRequest& Request)
{
	using namespace Mod::BuildTool;
	FJson Plan = PlanSdk(Request);
	FBuildLock Lock(Request.Output.parent_path() / PathFromUtf8("." + PathToUtf8(Request.Output.filename()) + ".mst"));
	if (fs::exists(Request.Output))
	{
		const FJson Existing = ReadJson(Request.Output / "Manifest.json");
		RequireFormat(Existing, "UMF.ModSdkBundle");
		if (Existing.at("gameId") != Request.Document.at("gameId") ||
		    Existing.at("sdkVersion") != Request.Document.at("sdkVersion") ||
		    Existing.at("pluginName") != Request.Document.at("pluginName"))
		{
			throw std::runtime_error("Refusing to replace an unrelated output directory.");
		}
		if (Existing.at("inputFingerprint") == Request.Fingerprint)
		{
			InspectSdk(Request.Output);
			CheckInputs(Request);
			ReportStage("UpToDate", "The complete SDK bundle is current.");
			return {{"format", "UMF.SdkResult"},
			        {"schemaVersion", 1},
			        {"status", "UpToDate"},
			        {"bundle", PathToUtf8(Request.Output)},
			        {"inputFingerprint", Request.Fingerprint},
			        {"capabilities", Existing.at("capabilities")}};
		}
		throw std::runtime_error("Published SDK inputs changed; select a new versioned output directory.");
	}
	FWorkspace Work(Request);
	FJson LibraryResults = FJson::array();
	ReportStage("BuildLibraries", "Building the declared generated native libraries with MBT.");
	for (std::size_t Index = 0; Index < Request.Libraries.size(); ++Index)
	{
		const FLibrary& Job = Request.Libraries[Index];
		const fs::path Result = Work.Root / ("library-" + std::to_string(Index) + ".json");
		const int Code = RunTool(Request.Mbt,
		                         {"build-host", "--context", PathToUtf8(Job.Context), "--generated-manifest",
		                          PathToUtf8(Job.Manifest), "--expected-input", Text(Job.Plan, "inputFingerprint"),
		                          "--output", PathToUtf8(Result)},
		                         Request.Project.parent_path(), Request.Document.value("timeoutSeconds", 1800u));
		if (Code != 0)
		{
			throw std::runtime_error("MBT library build failed: " + PathToUtf8(Job.Context));
		}
		LibraryResults.push_back({{"role", Job.Role}, {"result", ReadJson(Result)}});
	}
	// Newly discovered compiler dependencies participate in the final SDK key.
	Plan = PlanSdk(Request);
	CheckInputs(Request);
	const std::string Plugin = Text(Request.Document, "pluginName");
	const fs::path Source = Work.Root / "SdkSource";
	const fs::path SdkPackage = Work.Root / "SdkPackage";
	const fs::path DevelopmentSource = Work.Root / "DevelopmentSource";
	const fs::path DevelopmentPackage = Work.Root / "DevelopmentPackage";
	ReportStage("GenerateSdk", "Creating typed Runtime references and Blueprint-callable adapters.");
	GenerateSdkPlugin(Request, Source);
	for (const FLibrary& Job : Request.Libraries)
	{
		if (!Job.Plan.at("upToDate").get<bool>())
		{
			throw std::runtime_error("Native library was invalidated before SDK assembly.");
		}
		if (Job.Role == "Client")
		{
			CopyFile(PathFromUtf8(Text(Job.Plan, "library")),
			         Source / "Source" / "ThirdParty" / "ModClient" / "Lib" / PathFromUtf8(ClientRelativePath(Job)));
		}
	}
	const fs::path TemplateRoot = Request.DevelopmentPlugin.parent_path();
	for (const FJson& Input : Request.Inputs)
	{
		const fs::path Path = PathFromUtf8(Text(Input, "path"));
		const fs::path Relative = Path.lexically_relative(TemplateRoot);
		if (!Relative.empty() && *Relative.begin() != ".." && *Relative.begin() != "Binaries" &&
		    *Relative.begin() != "Intermediate")
		{
			CopyFile(Path, DevelopmentSource / Relative);
		}
	}
	ReportStage("BuildSdkPlugin", "Building an author-Editor SDK with normal UHT and UBT.");
	BuildPlugin(Request, Source / (Plugin + ".uplugin"), SdkPackage);
	VerifyEditorModule(Request, SdkPackage, Plugin + "Runtime");
	ReportStage("BuildDevelopmentPlugin", "Building the independent SDK development editor plugin.");
	BuildPlugin(Request, DevelopmentSource / Request.DevelopmentPlugin.filename(), DevelopmentPackage);
	VerifyEditorModule(Request, DevelopmentPackage, "DevelopmentEditor");
	const std::string Platform = Text(Request.Document.at("editor"), "platform");
	const fs::path ToolDirectory = DevelopmentPackage / "Binaries" / "ThirdParty" / "ModSDKTool" / Platform;
	CopyFile(ExecutablePath(), ToolDirectory / ExecutablePath().filename());
	CopyFile(Request.Mbt, ToolDirectory / Request.Mbt.filename());
	for (const char* Name : {"UnrealModFramework.LICENSE", "nlohmann.LICENSE.MIT", "NOTICE.md"})
	{
		CopyFile(ExecutablePath().parent_path() / "Licenses" / Name, ToolDirectory / "Licenses" / Name);
	}
	const fs::path Bundle = Work.Root / "Bundle";
	CopyTree(SdkPackage, Bundle / "Plugins" / Plugin);
	CopyTree(DevelopmentPackage, Bundle / "Plugins" / Request.DevelopmentPlugin.stem());
	WriteFileAtomically(Bundle / "Contract" / "SdkContract.json", Request.Contract.dump(2) + "\n");
	if (!Request.ConnectionArtifact.is_null())
	{
		const fs::path SourcePath = PathFromUtf8(Text(Request.ConnectionArtifact, "sourcePath"));
		if (HashFile(SourcePath) != Text(Request.ConnectionArtifact, "contentHash"))
		{
			throw std::runtime_error("Native connection descriptor changed before SDK publication.");
		}
		CopyFile(SourcePath, ResolveGeneratedPath(Bundle, Text(Request.ConnectionArtifact, "relativePath")));
	}
	WriteFileAtomically(Bundle / "Build" / "Plan.json", Plan.dump(2) + "\n");
	WriteFileAtomically(Bundle / "Build" / "NativeResults.json", LibraryResults.dump(2) + "\n");
	for (const FJson& Header : Request.Headers)
	{
		CopyFile(PathFromUtf8(Text(Header, "sourcePath")),
		         ResolveGeneratedPath(Bundle / "Native" / "Include", Text(Header, "relativePath")));
	}
	for (const FLibrary& Job : Request.Libraries)
	{
		if (Job.Role == "Client")
		{
			CopyFile(PathFromUtf8(Text(Job.Plan, "library")),
			         Bundle / "Native" / "Lib" / PathFromUtf8(ClientRelativePath(Job)));
		}
	}
	const FJson Capabilities = {{"nativeLibrariesBuilt", true},     {"authorEditorBuilt", true},
	                            {"typedBlueprintCallsBuilt", true}, {"hostFinalLinkVerified", false},
	                            {"gameSdkRuntimeBuilt", false},     {"runtimeBindingVerified", false}};
	const FJson Manifest = {{"format", "UMF.ModSdkBundle"},
	                        {"schemaVersion", 1},
	                        {"toolVersion", "0.1.0"},
	                        {"gameId", Request.Document.at("gameId")},
	                        {"sdkVersion", Request.Document.at("sdkVersion")},
	                        {"pluginName", Plugin},
	                        {"inputFingerprint", Request.Fingerprint},
	                        {"contractHash", Request.Contract.at("contractHash")},
	                        {"projectionHash", Request.Contract.at("projectionHash")},
	                        {"nativeAbiHash", Request.Contract.at("nativeAbiHash")},
	                        {"editor", Request.Document.at("editor")},
	                        {"capabilities", Capabilities},
	                        {"files", Inventory(Bundle)}};
	WriteFileAtomically(Bundle / "Manifest.json", Manifest.dump(2) + "\n");
	CheckInputs(Request);
	const std::string ExpectedFingerprint = Request.Fingerprint;
	FSdkRequest Current = ReadSdkRequest(Request.File);
	PlanSdk(Current);
	if (Current.Fingerprint != ExpectedFingerprint ||
	    std::any_of(Current.Libraries.begin(), Current.Libraries.end(),
	                [](const FLibrary& Job) { return !Job.Plan.at("upToDate").get<bool>(); }))
	{
		throw std::runtime_error("InputsChanged: native compilation dependencies changed during SDK assembly.");
	}
	for (const FLibrary& Job : Request.Libraries)
	{
		const fs::path Receipt = PathFromUtf8(Text(Job.Plan, "receipt"));
		if (HashFile(PathFromUtf8(Text(Job.Plan, "library"))) != Text(ReadJson(Receipt), "libraryHash"))
		{
			throw std::runtime_error("Native library changed during UE SDK compilation.");
		}
	}
	ReportStage("Publish", "Publishing verified authoring artifacts; game binding is not asserted.");
	Publish(Request, Bundle);
	return {{"format", "UMF.SdkResult"},
	        {"schemaVersion", 1},
	        {"status", "Built"},
	        {"bundle", PathToUtf8(Request.Output)},
	        {"inputFingerprint", Request.Fingerprint},
	        {"capabilities", Capabilities}};
}
} // namespace Mod::SdkTool
