// Copyright (c) 2026 Nelaric Contributors
#include "Sdk.h"

#include "Hash.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Mod::SdkTool
{
bool SamePath(const fs::path& Left, const fs::path& Right)
{
#ifdef _WIN32
	const std::wstring LeftText = Left.lexically_normal().native();
	const std::wstring RightText = Right.lexically_normal().native();
	return CompareStringOrdinal(LeftText.c_str(), -1, RightText.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
	return Left.lexically_normal() == Right.lexically_normal();
#endif
}

bool IsInside(const fs::path& Path, const fs::path& Root)
{
	const fs::path NormalPath = Path.lexically_normal();
	const fs::path NormalRoot = Root.lexically_normal();
	auto Part = NormalPath.begin();
	for (const fs::path& RootPart : NormalRoot)
	{
		if (RootPart.empty())
		{
			continue;
		}
		if (Part == NormalPath.end() || !SamePath(*Part, RootPart))
		{
			return false;
		}
		++Part;
	}
	return true;
}

namespace
{
using namespace Mod::BuildTool;

std::string Fold(std::string Value)
{
	for (char& Character : Value)
	{
		Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
	}
	return Value;
}

void AddInput(FSdkRequest& Request, const fs::path& File)
{
	const std::string Path = PathToUtf8(fs::canonical(File));
	for (const FJson& Input : Request.Inputs)
	{
		if (Input.at("path") == Path)
		{
			return;
		}
	}
	Request.Inputs.push_back({{"path", Path}, {"contentHash", HashFile(File)}});
}

void Identifier(std::string_view Value)
{
	static const std::set<std::string_view> Reserved = {
	    "alignas",     "alignof",  "asm",      "auto",         "bool",      "break",    "case",      "catch",
	    "char",        "class",    "concept",  "const",        "constexpr", "continue", "decltype",  "default",
	    "delete",      "do",       "double",   "else",         "enum",      "explicit", "export",    "extern",
	    "false",       "float",    "for",      "friend",       "if",        "inline",   "int",       "long",
	    "namespace",   "new",      "noexcept", "nullptr",      "operator",  "private",  "protected", "public",
	    "register",    "requires", "return",   "short",        "signed",    "sizeof",   "static",    "struct",
	    "switch",      "template", "this",     "thread_local", "throw",     "true",     "try",       "typedef",
	    "typename",    "union",    "unsigned", "using",        "virtual",   "void",     "volatile",  "while",
	    "StaticClass", "GetClass", "GetWorld", "Serialize"};
	if (Value.empty() || Value.size() > 80 || !std::isalpha(static_cast<unsigned char>(Value.front())) ||
	    Value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
	        std::string_view::npos ||
	    Reserved.contains(Value))
	{
		throw std::runtime_error("Invalid or reserved generated identifier: " + std::string(Value));
	}
}

void PortablePath(std::string_view Value)
{
	if (Value.empty() || Value.find_first_of("\\:\r\n\"<>|?*") != std::string_view::npos)
	{
		throw std::runtime_error("Expected a portable relative public path.");
	}
	const fs::path Path = PathFromUtf8(Value);
	if (Path.is_absolute() || Path.has_root_name())
	{
		throw std::runtime_error("Public include paths must be relative.");
	}
	for (const fs::path& Part : Path)
	{
		const std::string Name = PathToUtf8(Part);
		if (Name.empty() || Name == "." || Name == ".." || Name.back() == '.' || Name.back() == ' ')
		{
			throw std::runtime_error("Public paths cannot contain traversal or ambiguous components.");
		}
	}
}

void ValidateContract(FSdkRequest& Request)
{
	const FJson& Contract = Request.Contract;
	RequireFormat(Contract, "UMF.SdkContract");
	if (Contract.value("status", "") != "Complete" || Contract.value("unresolvedTypeCount", -1) != 0 ||
	    Contract.value("adapterAbi", "") != "ContextHandlesV1" ||
	    Text(Contract, "gameId") != Text(Request.Document, "gameId"))
	{
		throw std::runtime_error("SDK requires a Complete ContextHandlesV1 contract for the requested game.");
	}
	for (const char* Key : {"contractHash", "projectionHash", "nativeAbiHash"})
	{
		RequireDigest(Text(Contract, Key), Key);
	}
	if (Contract.contains("connectionDescriptor"))
	{
		PortablePath(Text(Contract, "connectionDescriptor"));
		if (Text(Contract, "connectionDescriptor") == "Contract/SdkContract.json")
		{
			throw std::runtime_error("Connection descriptor conflicts with the published SDK contract path.");
		}
	}
	if (Contract.contains("modLifecycle"))
	{
		const FJson& Lifecycle = Contract.at("modLifecycle");
		if (!Lifecycle.is_object() || Lifecycle.value("schemaVersion", 0) != 1 ||
		    Text(Lifecycle, "module") != "ModLoader" || Text(Lifecycle, "resource") != "Contract/ModLifecycle.json")
		{
			throw std::runtime_error("Unsupported global Mod lifecycle projection.");
		}
		RequireDigest(Text(Lifecycle, "resourceHash"), "modLifecycle.resourceHash");
	}
	const FJson& Types = Contract.at("types");
	const FJson& Apis = Contract.at("apis");
	if (!Types.is_array() || Types.empty() || Types.size() > 128 || !Apis.is_array() || Apis.empty() ||
	    Apis.size() > 1024)
	{
		throw std::runtime_error("SDK types/APIs must be bounded, nonempty arrays.");
	}
	std::set<std::string> TypeIds;
	std::set<std::string> Names;
	const std::string Plugin = Text(Request.Document, "pluginName");
	for (const std::string& Name :
	     {"F" + Plugin + "PlayerContext", "E" + Plugin + "CallOutcome", "U" + Plugin + "Library"})
	{
		Names.insert(Fold(Name));
	}
	for (const FJson& Type : Types)
	{
		const std::string Name = Text(Type, "cppName");
		Identifier(Name);
		if (Type.value("kind", "") != "ObjectReference" || Name.front() != 'F' ||
		    !TypeIds.insert(Text(Type, "id")).second || !Names.insert(Fold(Name)).second)
		{
			throw std::runtime_error("SDK types must be unique public ObjectReference structures.");
		}
	}
	std::set<std::string> ApiIds;
	std::set<std::string> Functions;
	bool bHasGetter = false;
	bool bHasMethod = false;
	for (const FJson& Api : Apis)
	{
		const std::string Id = Text(Api, "id");
		const std::string Name = Text(Api, "name");
		Identifier(Name);
		if (!ApiIds.insert(Id).second || !Functions.insert(Fold(Name)).second ||
		    (Api.value("scope", "") != "Player" && Api.value("scope", "") != "World") ||
		    Api.value("thread", "") != "GameThread" || Api.value("failureRule", "") != "ReturnError")
		{
			throw std::runtime_error("Unsupported or duplicate SDK API: " + Id);
		}
		Text(Api, "displayName");
		RequireDigest(Text(Api, "signatureHash"), "API signatureHash");
		RequireDigest(Text(Api, "projectionHash"), "API projectionHash");
		const FJson& Permissions = Api.at("permissions");
		if (!Permissions.is_array() || Permissions.size() > 128)
		{
			throw std::runtime_error("API permissions must be a bounded array: " + Id);
		}
		std::set<std::string> SeenPermissions;
		for (const FJson& Permission : Permissions)
		{
			if (!Permission.is_string() || Permission.get<std::string>().empty() ||
			    !SeenPermissions.insert(Permission.get<std::string>()).second)
			{
				throw std::runtime_error("Empty or duplicate permission: " + Id);
			}
		}
		const FJson& Native = Api.at("native");
		Identifier(Text(Native, "symbol"));
		PortablePath(Text(Native, "header"));
		const std::string Shape = Text(Api, "shape");
		if (Shape == "ObjectGetter")
		{
			if (!TypeIds.contains(Text(Api, "resultTypeId")) || Api.value("provider", "") != "LocalPlayerSubsystem" ||
			    Api.value("scope", "") != "Player" || Api.value("nullResult", "") != "Error")
			{
				throw std::runtime_error("ObjectGetter needs a resolved player provider, type, and null policy.");
			}
			bHasGetter = true;
		}
		else if (Shape == "FloatMethod")
		{
			if (!TypeIds.contains(Text(Api, "targetTypeId")))
			{
				throw std::runtime_error("FloatMethod target is not a public object reference.");
			}
			const std::string Parameter = Text(Api, "parameterName");
			Identifier(Parameter);
			if (Parameter == "Target" || Parameter == "Outcome" || Parameter == "ErrorCode" || Parameter == "Value" ||
			    Parameter == "NativeValue" || Parameter == "Status")
			{
				throw std::runtime_error("Float parameter collides with an SDK adapter parameter.");
			}
			Text(Api, "parameterId");
			bHasMethod = true;
		}
		else
		{
			throw std::runtime_error("Unsupported projected API shape: " + Shape);
		}
	}
	if (!bHasGetter || !bHasMethod)
	{
		throw std::runtime_error("This SDK adapter requires an object getter and a float instance method.");
	}
	for (const FJson& Api : Apis)
	{
		if (Api.at("shape") == "FloatMethod")
		{
			const bool bReachable = std::any_of(Apis.begin(), Apis.end(),
			                                    [&](const FJson& Getter) {
				                                    return Getter.at("shape") == "ObjectGetter" &&
				                                           Getter.at("resultTypeId") == Api.at("targetTypeId");
			                                    });
			if (!bReachable)
			{
				throw std::runtime_error("No getter returns the method's public target type: " + Text(Api, "id"));
			}
		}
	}
}
} // namespace

FJson ReadJson(const fs::path& Path)
{
	if (!fs::is_regular_file(Path) || fs::file_size(Path) > 16 * 1024 * 1024)
	{
		throw std::runtime_error("Missing or oversized JSON input: " + Mod::BuildTool::PathToUtf8(Path));
	}
	try
	{
		return FJson::parse(Mod::BuildTool::ReadFile(Path));
	}
	catch (const FJson::exception& Error)
	{
		throw std::runtime_error("Invalid JSON in " + Mod::BuildTool::PathToUtf8(Path) + ": " + Error.what());
	}
}

std::string Text(const FJson& Document, const char* Key)
{
	const std::string Value = Document.at(Key).get<std::string>();
	if (Value.empty() || Value.size() > 32768 ||
	    std::any_of(Value.begin(), Value.end(), [](unsigned char Character) { return Character < 32; }))
	{
		throw std::runtime_error(std::string(Key) + " must be a bounded nonempty single-line string.");
	}
	return Value;
}

void RequireFormat(const FJson& Document, std::string_view Format)
{
	if (Format == "UMF.SdkContract" && Document.contains("projectionVersion") && Document.contains("exports"))
	{
		throw std::runtime_error(
		    "UnsupportedProjection: native UE view contracts are not ContextHandlesV1 client contracts. "
		    "A matching export producer must supply UMF.SdkContract and checked client endpoints.");
	}
	if (!Document.is_object() || Document.value("format", "") != Format || !Document.contains("schemaVersion") ||
	    Document.at("schemaVersion") != 1)
	{
		throw std::runtime_error("Expected " + std::string(Format) + " schemaVersion 1.");
	}
}

FSdkRequest ReadSdkRequest(const fs::path& File)
{
	using namespace Mod::BuildTool;
	FSdkRequest Request;
	Request.File = fs::canonical(File);
	Request.Document = ReadJson(Request.File);
	RequireFormat(Request.Document, "UMF.SdkBuildRequest");
	const fs::path Root = Request.File.parent_path();
	const FJson& Document = Request.Document;
	Text(Document, "gameId");
	Identifier(Text(Document, "pluginName"));
	const std::string Version = Text(Document, "sdkVersion");
	if (Version.find_first_not_of("0123456789.") != std::string::npos || Version.front() == '.' ||
	    Version.back() == '.' || std::count(Version.begin(), Version.end(), '.') != 2 ||
	    Version.find("..") != std::string::npos)
	{
		throw std::runtime_error("sdkVersion must contain three numeric components.");
	}
	Request.Project = fs::canonical(ResolvePath(Root, Text(Document, "project")));
	if (Request.Project.extension() != ".uproject")
	{
		throw std::runtime_error("project must identify a real .uproject file.");
	}
	ReadJson(Request.Project);
	Request.Output = ResolvePath(Root, Text(Document, "outputDirectory"));
	while (Request.Output.has_relative_path() && !Request.Output.has_filename())
	{
		Request.Output = Request.Output.parent_path();
	}
	Request.ContractFile = fs::canonical(ResolvePath(Root, Text(Document, "contract")));
	Request.Contract = ReadJson(Request.ContractFile);
	ValidateContract(Request);
	Request.Mbt = fs::canonical(ResolvePath(Root, Text(Document.at("tools"), "mbt")));
	Request.Dotnet = fs::canonical(ResolvePath(Root, Text(Document.at("tools"), "dotnet")));
	Request.AutomationTool = fs::canonical(ResolvePath(Root, Text(Document.at("tools"), "automationTool")));
	Request.DevelopmentPlugin = fs::canonical(ResolvePath(Root, Text(Document, "developmentPlugin")));
	if (Fold(Text(Document, "pluginName")) == Fold(PathToUtf8(Request.DevelopmentPlugin.stem())))
	{
		throw std::runtime_error("The generated SDK and independent development plugin need distinct names.");
	}
	const FJson Development = ReadJson(Request.DevelopmentPlugin);
	if (Request.DevelopmentPlugin.extension() != ".uplugin" || !Development.at("Modules").is_array() ||
	    !std::any_of(Development.at("Modules").begin(), Development.at("Modules").end(),
	                 [](const FJson& Module) {
		                 return Module.value("Name", "") == "DevelopmentEditor" && Module.value("Type", "") == "Editor";
	                 }))
	{
		throw std::runtime_error("developmentPlugin must contain the independent DevelopmentEditor module.");
	}
	const FJson& Editor = Document.at("editor");
	const std::string Platform = Text(Editor, "platform");
	const std::string Architecture = Text(Editor, "architecture");
	if (
#ifdef _WIN32
	    Platform != "Win64"
#elif defined(__APPLE__)
	    Platform != "Mac"
#else
	    Platform != "Linux"
#endif
	    || (Architecture != "x64" && Architecture != "arm64") || Editor.value("configuration", "") != "Development")
	{
		throw std::runtime_error("Author Editor builds require the current host platform and Development.");
	}
	const fs::path Modules = fs::canonical(ResolvePath(Root, Text(Editor, "modules")));
	if (Text(ReadJson(Modules), "BuildId") != Text(Editor, "buildId"))
	{
		throw std::runtime_error("Author Editor BuildId does not match its real engine module manifest.");
	}
	if (Document.contains("timeoutSeconds") &&
	    (!Document.at("timeoutSeconds").is_number_integer() || Document.at("timeoutSeconds") < 1 ||
	     Document.at("timeoutSeconds") > 86400))
	{
		throw std::runtime_error("timeoutSeconds must be between 1 and 86400.");
	}
	for (const fs::path& Input : {Request.File, Request.Project, Request.ContractFile, Request.Mbt, Request.Dotnet,
	                              Request.AutomationTool, Modules, ExecutablePath()})
	{
		AddInput(Request, Input);
	}
	for (const char* Name : {"UnrealModFramework.LICENSE", "nlohmann.LICENSE.MIT", "NOTICE.md"})
	{
		AddInput(Request, ExecutablePath().parent_path() / "Licenses" / Name);
	}
	const fs::path TemplateRoot = Request.DevelopmentPlugin.parent_path();
	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(TemplateRoot))
	{
		if (Entry.is_symlink())
		{
			throw std::runtime_error("Developer plugin templates cannot contain symbolic links.");
		}
		const fs::path Relative = Entry.path().lexically_relative(TemplateRoot);
		if (Entry.is_regular_file() && *Relative.begin() != "Binaries" && *Relative.begin() != "Intermediate")
		{
			AddInput(Request, Entry.path());
		}
	}
	const FJson& Libraries = Document.at("libraries");
	if (!Libraries.is_array() || Libraries.empty() || Libraries.size() > 256)
	{
		throw std::runtime_error("libraries must be a bounded nonempty array.");
	}
	for (const FJson& Library : Libraries)
	{
		FLibrary Job;
		Job.Role = Text(Library, "role");
		if (Job.Role != "Host" && Job.Role != "Client")
		{
			throw std::runtime_error("Library role must be Host or Client.");
		}
		Job.Context = fs::canonical(ResolvePath(Root, Text(Library, "context")));
		Job.Manifest = fs::canonical(ResolvePath(Root, Text(Library, "generatedManifest")));
		const FJson Context = ReadJson(Job.Context);
		const FJson Manifest = ReadJson(Job.Manifest);
		if (Manifest.value("format", "") != "UMF.GeneratedArtifacts" || Manifest.value("status", "") != "Complete")
		{
			throw std::runtime_error("MST needs complete generated artifacts; MHT scan reports are not SDK inputs.");
		}
		for (const char* Key : {"contractHash", "projectionHash", "nativeAbiHash"})
		{
			if (Manifest.at(Key) != Request.Contract.at(Key))
			{
				throw std::runtime_error(std::string("Mixed SDK batches: ") + Key);
			}
		}
		AddInput(Request, Job.Context);
		AddInput(Request, Job.Manifest);
		for (const FJson& Generated : Manifest.at("generatedFiles"))
		{
			const fs::path Path = ResolveGeneratedPath(Job.Manifest.parent_path(), Text(Generated, "path"));
			AddInput(Request, Path);
		}
		const fs::path LibraryPath = ResolvePath(Job.Context.parent_path(), Text(Context, "outputLibrary"));
		if (IsInside(LibraryPath, Request.Output) || IsInside(Request.Output, Job.Manifest.parent_path()) ||
		    IsInside(Request.Output, LibraryPath))
		{
			throw std::runtime_error("SDK output overlaps a library or generated input root.");
		}
		Request.Libraries.push_back(std::move(Job));
	}
	for (const FJson& Input : Request.Inputs)
	{
		if (IsInside(PathFromUtf8(Input.at("path").get<std::string>()), Request.Output))
		{
			throw std::runtime_error("SDK output would contain or replace an input.");
		}
	}
	if (IsInside(Request.Output, TemplateRoot))
	{
		throw std::runtime_error("SDK output cannot be inside its developer plugin template.");
	}
	if (Document.contains("workDirectory"))
	{
		const fs::path WorkRoot = ResolvePath(Root, Text(Document, "workDirectory"));
		if (IsInside(WorkRoot, Request.Output) || IsInside(WorkRoot, TemplateRoot))
		{
			throw std::runtime_error("workDirectory overlaps the SDK output or plugin source.");
		}
		for (const FLibrary& Job : Request.Libraries)
		{
			if (IsInside(WorkRoot, Job.Manifest.parent_path()))
			{
				throw std::runtime_error("workDirectory cannot be inside generated input roots.");
			}
		}
	}
	std::sort(Request.Inputs.begin(), Request.Inputs.end(), [](const FJson& Left, const FJson& Right)
	          { return Left.at("path").get<std::string>() < Right.at("path").get<std::string>(); });
	return Request;
}

void ValidateResultOutput(const FSdkRequest& Request, const fs::path& Output)
{
	if (Output.empty())
	{
		return;
	}
	const fs::path Destination = fs::weakly_canonical(fs::absolute(Output));
	if (IsInside(Destination, Request.Output))
	{
		throw std::runtime_error("Result output must be outside the SDK bundle.");
	}
	for (const FJson& Input : Request.Inputs)
	{
		if (SamePath(Destination, Mod::BuildTool::PathFromUtf8(Input.at("path").get<std::string>())))
		{
			throw std::runtime_error("Result output would replace an SDK input.");
		}
	}
	for (const FLibrary& Job : Request.Libraries)
	{
		const FJson Context = ReadJson(Job.Context);
		const fs::path Library = Mod::BuildTool::ResolvePath(Job.Context.parent_path(), Text(Context, "outputLibrary"));
		if (SamePath(Destination, Library) ||
		    SamePath(Destination, fs::path(Library.native() + fs::path(".mbt.json").native())))
		{
			throw std::runtime_error("Result output would replace a native artifact.");
		}
	}
}

FJson PlanSdk(FSdkRequest& Request)
{
	using namespace Mod::BuildTool;
	ReportStage("Plan", "Validating complete contracts and native library inputs.");
	const fs::path Temporary = fs::temp_directory_path() / PathFromUtf8("umf-mst-plan-" + UniqueSuffix());
	if (!fs::create_directory(Temporary))
	{
		throw std::runtime_error("Cannot create an exclusive MST planning directory.");
	}
	try
	{
		Request.Headers = FJson::array();
		Request.ConnectionArtifact = nullptr;
		Request.LifecycleArtifact = nullptr;
		std::map<std::string, FJson> Headers;
		std::set<std::string> Outputs;
		std::map<std::string, std::set<std::string>> ClientBindings;
		std::set<std::string> ClientDestinations;
		std::map<std::string, std::set<std::string>> HostBindings;
		bool bContractDeclared = false;
		bool bHasHost = false;
		bool bHasEditorClient = false;
		FJson Jobs = FJson::array();
		for (std::size_t Index = 0; Index < Request.Libraries.size(); ++Index)
		{
			FLibrary& Job = Request.Libraries[Index];
			const fs::path ResultFile = Temporary / (std::to_string(Index) + ".json");
			if (RunTool(Request.Mbt,
			            {"plan", "--context", PathToUtf8(Job.Context), "--generated-manifest", PathToUtf8(Job.Manifest),
			             "--output", PathToUtf8(ResultFile)},
			            Request.Project.parent_path(), Request.Document.value("timeoutSeconds", 1800u)) != 0)
			{
				throw std::runtime_error("MBT rejected the library plan: " + PathToUtf8(Job.Context));
			}
			Job.Plan = ReadJson(ResultFile);
			RequireFormat(Job.Plan, "UMF.ModHostPlan");
			if (!Outputs.insert(Fold(Text(Job.Plan, "library"))).second)
			{
				throw std::runtime_error("Two SDK jobs write the same native library.");
			}
			for (const FJson& Artifact : Job.Plan.value("publicArtifacts", FJson::array()))
			{
				if (Artifact.at("role") == "Contract" && Artifact.at("contentHash") == HashFile(Request.ContractFile))
				{
					bContractDeclared = true;
				}
				if (Job.Role == "Client" && Artifact.at("role") == "SdkHeader")
				{
					const std::string Include = Text(Artifact, "relativePath");
					PortablePath(Include);
					const std::string Key = Fold(Include);
					if (Headers.contains(Key) && (Headers.at(Key).at("contentHash") != Artifact.at("contentHash") ||
					                              Headers.at(Key).at("relativePath") != Artifact.at("relativePath")))
					{
						throw std::runtime_error("Conflicting public headers across SDK profiles: " + Include);
					}
					Headers[Key] = Artifact;
				}
				if (Job.Role == "Client" && Artifact.at("role") == "Contract" &&
				    Request.Contract.contains("connectionDescriptor") &&
				    Artifact.at("relativePath") == Request.Contract.at("connectionDescriptor"))
				{
					if (!Request.ConnectionArtifact.is_null() &&
					    Request.ConnectionArtifact.at("contentHash") != Artifact.at("contentHash"))
					{
						throw std::runtime_error("Conflicting native connection descriptors across client profiles.");
					}
					Request.ConnectionArtifact = Artifact;
				}
				if (Job.Role == "Client" && Artifact.at("role") == "SdkResource" &&
				    Request.Contract.contains("modLifecycle") &&
				    Artifact.at("relativePath") == Request.Contract.at("modLifecycle").at("resource"))
				{
					if (Artifact.at("contentHash") != Request.Contract.at("modLifecycle").at("resourceHash"))
					{
						throw std::runtime_error("The Mod lifecycle export differs across SDK profiles.");
					}
					Request.LifecycleArtifact = Artifact;
				}
			}
			const FJson& Target = Job.Plan.at("target");
			const std::string Profile = Text(Target, "platform") + "/" + Text(Target, "architecture") + "/" +
			                            Text(Target, "configuration") + "/" + Text(Target, "type");
			if (Job.Role == "Client")
			{
				Identifier(Text(Job.Plan, "ownerModule"));
				if (!ClientDestinations.insert(Fold(Profile + "/" + Text(Job.Plan, "ownerModule"))).second)
				{
					throw std::runtime_error("Duplicate public client library for the same target profile.");
				}
				ClientBindings[Profile];
				for (const FJson& Binding : Job.Plan.value("requiredBindings", FJson::array()))
				{
					for (const FJson& Api : Request.Contract.at("apis"))
					{
						if (Binding.value("exportId", "") == Text(Api, "id") &&
						    Binding.value("symbol", "") == Text(Api.at("native"), "symbol") &&
						    Binding.value("signatureHash", "") == Text(Api, "signatureHash") &&
						    Binding.value("projectionHash", "") == Text(Api, "projectionHash"))
						{
							if (!ClientBindings[Profile].insert(Text(Api, "id")).second)
							{
								throw std::runtime_error("Duplicate client endpoint for " + Text(Api, "id") + " in " +
								                         Profile);
							}
						}
					}
				}
				bHasEditorClient |= Target.at("platform") == Request.Document.at("editor").at("platform") &&
				                    Target.at("architecture") == Request.Document.at("editor").at("architecture") &&
				                    Target.at("configuration") == "Development" && Target.at("type") == "Editor";
			}
			else
			{
				bHasHost = true;
				HostBindings[Profile];
				for (const FJson& Binding : Job.Plan.value("requiredBindings", FJson::array()))
				{
					for (const FJson& Api : Request.Contract.at("apis"))
					{
						if (Binding.value("exportId", "") == Text(Api, "id") &&
						    Binding.value("signatureHash", "") == Text(Api, "signatureHash") &&
						    Binding.value("projectionHash", "") == Text(Api, "projectionHash"))
						{
							if (!HostBindings[Profile].insert(Text(Api, "id")).second)
							{
								throw std::runtime_error("Duplicate host binding for " + Text(Api, "id") + " in " +
								                         Profile);
							}
						}
					}
				}
			}
			Jobs.push_back({{"role", Job.Role},
			                {"context", PathToUtf8(Job.Context)},
			                {"generatedManifest", PathToUtf8(Job.Manifest)},
			                {"mbtPlan", Job.Plan}});
		}
		if (!bHasHost || !bHasEditorClient || !bContractDeclared)
		{
			throw std::runtime_error(
			    "SDK requires Host jobs, an author Editor Client job, and a declared public contract.");
		}
		if (Request.Contract.contains("modLifecycle") && Request.LifecycleArtifact.is_null())
		{
			throw std::runtime_error("Missing the declared Mod lifecycle export in the generated artifacts.");
		}
		if (Request.Contract.contains("connectionDescriptor"))
		{
			if (Request.ConnectionArtifact.is_null())
			{
				throw std::runtime_error("Missing declared public native connection descriptor.");
			}
			const fs::path Path = PathFromUtf8(Text(Request.ConnectionArtifact, "sourcePath"));
			if (HashFile(Path) != Text(Request.ConnectionArtifact, "contentHash"))
			{
				throw std::runtime_error("Native connection descriptor changed during planning.");
			}
			const FJson Connection = ReadJson(Path);
			RequireFormat(Connection, "UMF.ClientAbi");
			if (Connection.value("status", "") != "Complete" || Connection.value("abiVersion", 0) != 1 ||
			    Connection.value("adapterAbi", "") != "ContextHandlesV1" ||
			    Connection.at("gameId") != Request.Contract.at("gameId"))
			{
				throw std::runtime_error("Native connection descriptor does not match the SDK contract.");
			}
			for (const char* Key : {"contractHash", "projectionHash", "nativeAbiHash"})
			{
				if (Connection.at(Key) != Request.Contract.at(Key))
				{
					throw std::runtime_error(std::string("Native connection descriptor mismatch: ") + Key);
				}
			}
			for (const FJson& Api : Request.Contract.at("apis"))
			{
				if (!std::any_of(Connection.at("apis").begin(), Connection.at("apis").end(),
				                 [&](const FJson& Entry)
				                 {
					                 return Entry.at("id") == Api.at("id") &&
					                        Entry.at("symbol") == Api.at("native").at("symbol") &&
					                        Entry.at("signatureHash") == Api.at("signatureHash") &&
					                        Entry.at("projectionHash") == Api.at("projectionHash");
				                 }))
				{
					throw std::runtime_error("Native connection descriptor is missing a matching API: " +
					                         Text(Api, "id"));
				}
			}
		}
		for (const auto& [Name, Header] : Headers)
		{
			Request.Headers.push_back(Header);
		}
		for (const FJson& Api : Request.Contract.at("apis"))
		{
			if (!Headers.contains(Fold(Text(Api.at("native"), "header"))))
			{
				throw std::runtime_error("Missing public header for " + Text(Api, "id"));
			}
			for (const auto& [Profile, Bindings] : HostBindings)
			{
				if (!Bindings.contains(Text(Api, "id")))
				{
					throw std::runtime_error("Missing checked host binding for " + Text(Api, "id") + " in profile " +
					                         Profile);
				}
			}
			for (const auto& [Profile, Bindings] : ClientBindings)
			{
				if (!Bindings.contains(Text(Api, "id")))
				{
					throw std::runtime_error("Missing checked client endpoint for " + Text(Api, "id") + " in profile " +
					                         Profile);
				}
			}
		}
		const FJson Identity = {{"toolVersion", "0.1.0"},
		                        {"request", Request.Document},
		                        {"contract", Request.Contract},
		                        {"inputs", Request.Inputs},
		                        {"libraries", Jobs}};
		// Cache status is observable state, not part of the SDK identity.
		FJson Stable = Identity;
		for (FJson& Job : Stable["libraries"])
		{
			Job["mbtPlan"].erase("upToDate");
			Job["mbtPlan"].erase("reason");
		}
		Request.Fingerprint = HashBytes(Stable.dump());
		fs::remove_all(Temporary);
		return {{"format", "UMF.SdkPlan"},
		        {"schemaVersion", 1},
		        {"status", "Planned"},
		        {"gameId", Request.Document.at("gameId")},
		        {"sdkVersion", Request.Document.at("sdkVersion")},
		        {"pluginName", Request.Document.at("pluginName")},
		        {"inputFingerprint", Request.Fingerprint},
		        {"outputDirectory", PathToUtf8(Request.Output)},
		        {"editor", Request.Document.at("editor")},
		        {"contract", Request.Contract},
		        {"libraries", Jobs},
		        {"publicHeaders", Request.Headers},
		        {"connectionDescriptor", Request.ConnectionArtifact},
		        {"hostFinalLinkVerified", false},
		        {"runtimeBindingVerified", false},
		        {"stages", {"BuildLibraries", "GenerateSdk", "BuildSdkPlugin", "BuildDevelopmentPlugin", "Publish"}}};
	}
	catch (...)
	{
		std::error_code Ignored;
		fs::remove_all(Temporary, Ignored);
		throw;
	}
}
} // namespace Mod::SdkTool
