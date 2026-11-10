// Copyright (c) 2026 Nelaric Contributors
#include "SdkPluginGenerator.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModSDKTool/src/Generation/CppLiteral.h"
#include "ModSDKTool/src/Generation/GeneratedFile.h"
#include "ModSDKTool/src/Generation/ReferenceWriter.h"
#include "ModSDKTool/src/Generation/TypedMethodWriter.h"
#include "ModSDKTool/src/Generation/WrapperErrors.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"
#include "ModSDKTool/src/Manifest/Library.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void SdkPluginGenerator::GenerateSdkPlugin(const SdkRequest& Request, const fs::path& Directory)
{
	using namespace Mod::BuildTool;
	const std::string Plugin = JsonDocument::Text(Request.Document, "pluginName");
	const std::string Module = Plugin + "Runtime";
	std::string APIMacro = Module + "_API";
	std::transform(APIMacro.begin(), APIMacro.end(), APIMacro.begin(),
	               [](unsigned char Character) { return static_cast<char>(std::toupper(Character)); });
	const std::string Context = "F" + Plugin + "PlayerContext";
	const std::string Outcome = "E" + Plugin + "CallOutcome";
	const std::string LibraryClass = "U" + Plugin + "Library";
	const WrapperErrors Errors =
	    Request.Contract.contains("errorHeader")
	        ? WrapperErrors{"MOD_ERROR_INVALIDARGUMENT", "MOD_ERROR_WRONGTHREAD", "MOD_ERROR_OBJECTUNAVAILABLE"}
	        : WrapperErrors{"-1", "-3", "-2"};
	std::map<std::string, std::string> Types;
	for (const Json& Type : Request.Contract.at("types"))
	{
		Types[JsonDocument::Text(Type, "id")] = JsonDocument::Text(Type, "cppName");
	}
	std::ostringstream Header;
	Header << "// Copyright (c) 2026 Nelaric Contributors\n/" << "** @file " << Plugin << "API.h */\n#pragma once\n\n"
	       << "#include \"CoreMinimal.h\"\n#include \"Kismet/BlueprintFunctionLibrary.h\"\n";
	std::set<std::string> EngineIncludes;
	std::set<std::string> ExtraModules;
	for (const auto& API : Request.Contract.at("apis"))
	{
		if (API.at("shape") == "TypedMethod")
		{
			for (const auto& Parameter : API.at("parameters"))
			{
				if (Parameter.at("kind") == "ObjectInput")
				{
					EngineIncludes.insert(JsonDocument::Text(Parameter, "include"));
					const auto Dependency = JsonDocument::Text(Parameter, "module");
					if (Dependency != "Engine" && Dependency != "Core" && Dependency != "CoreUObject")
					{
						ExtraModules.insert(Dependency);
					}
				}
			}
		}
	}
	for (const auto& Include : EngineIncludes)
	{
		Header << "#include " << CppLiteral::Literal(Include) << "\n";
	}
	Header << "#include \"" << Plugin << "API.generated.h\"\n\n"
	       << CppLiteral::LineDoc << " Reports native invocation success without hiding an error branch.\n"
	       << "UENUM(BlueprintType)\nenum class " << Outcome << " : uint8\n{\n"
	       << "\t" << CppLiteral::LineDoc << " Call succeeded.\n\tSucceeded,\n\t" << CppLiteral::LineDoc
	       << " Call failed.\n\tFailed,\n};\n\n";
	ReferenceWriter::WriteReference(Header, Context, "PlayerScope", APIMacro);
	for (const auto& [Id, Name] : Types)
	{
		ReferenceWriter::WriteReference(Header, Name, "Object", APIMacro);
	}
	Header << CppLiteral::LineDoc << " Typed game SDK calls; all operations execute on the game thread.\n"
	       << "UCLASS(MinimalAPI)\nclass " << LibraryClass << " : public UBlueprintFunctionLibrary\n"
	       << "{\n\tGENERATED_BODY()\n\npublic:\n";
	std::ostringstream Source;
	Source << "// Copyright (c) 2026 Nelaric Contributors\n#include \"" << Plugin << "API.h\"\n\n"
	       << "#include <cstdint>\n#include <type_traits>\n";
	if (!EngineIncludes.empty())
	{
		Source << "#include \"Containers/StringConv.h\"\n";
	}
	std::set<std::string> Includes;
	if (Request.Contract.contains("errorHeader"))
	{
		Includes.insert(JsonDocument::Text(Request.Contract, "errorHeader"));
	}
	for (const Json& API : Request.Contract.at("apis"))
	{
		Includes.insert(JsonDocument::Text(API.at("native"), "header"));
	}
	for (const std::string& Include : Includes)
	{
		Source << "#include \"" << Include << "\"\n";
	}
	Source << "\n";
	for (const Json& API : Request.Contract.at("apis"))
	{
		if (API.at("shape") == "TypedMethod")
		{
			TypedMethodWriter::WriteTypedMethod(Header, Source, API, Types, Context, Outcome, LibraryClass, APIMacro,
			                                    Plugin, Errors);
			continue;
		}
		const std::string Name = JsonDocument::Text(API, "name");
		const std::string Symbol = JsonDocument::Text(API.at("native"), "symbol");
		const bool bGetter = API.at("shape") == "ObjectGetter";
		const std::string Type = Types.at(JsonDocument::Text(API, bGetter ? "resultTypeId" : "targetTypeId"));
		const std::string Parameter = bGetter ? "" : JsonDocument::Text(API, "parameterName");
		Header << "\t" << CppLiteral::LineDoc
		       << " Invokes the bound API on the game thread; failure has an error pin.\n"
		       << "\tUFUNCTION(BlueprintCallable, Category = \"Mod|" << Plugin
		       << "\", meta = (DisplayName = " << CppLiteral::Literal(JsonDocument::Text(API, "displayName"))
		       << ", ExpandEnumAsExecs = \"Outcome\", Keywords = " << CppLiteral::Literal(JsonDocument::Text(API, "id"))
		       << "))\n"
		       << "\tstatic " << APIMacro << " void " << Name << "(";
		Source << "static_assert(std::is_same_v<decltype(&::" << Symbol << "), std::uint32_t (*)("
		       << "std::uint64_t, std::uint64_t, " << (bGetter ? "std::uint64_t*" : "float, float*")
		       << ")>, \"Generated native client ABI does not match ContextHandlesV1\");\n\n"
		       << "void " << LibraryClass << "::" << Name << "(";
		const std::string Signature =
		    bGetter
		        ? Context + " Context, " + Type + "& Object, " + Outcome + "& Outcome, int64& ErrorCode)"
		        : Type + " Target, float " + Parameter + ", float& Value, " + Outcome + "& Outcome, int64& ErrorCode)";
		Header << Signature << ";\n\n";
		Source << Signature << "\n{\n\tOutcome = " << Outcome << "::Failed;\n\tErrorCode = " << Errors.InvalidArgument
		       << ";\n"
		       << (bGetter ? "\tObject = " + Type + "();\n" : "\tValue = 0.0f;\n")
		       << "\tif (!IsInGameThread())\n\t{\n\t\tErrorCode = " << Errors.WrongThread << ";\n\t\treturn;\n\t}\n";
		if (bGetter)
		{
			Source << "\tif (Context.GetNativeContext() == 0 || Context.GetNativePlayerScope() == 0)\n\t{\n"
			       << "\t\treturn;\n\t}\n\tstd::uint64_t NativeObject = 0;\n"
			       << "\tconst std::uint32_t Status = ::" << Symbol
			       << "(static_cast<std::uint64_t>(Context.GetNativeContext()),\n"
			       << "\t\tstatic_cast<std::uint64_t>(Context.GetNativePlayerScope()), &NativeObject);\n"
			       << "\tErrorCode = static_cast<int64>(Status);\n"
			       << "\tif (Status == 0 && NativeObject == 0)\n\t{\n\t\tErrorCode = " << Errors.ObjectUnavailable
			       << ";\n\t\treturn;\n\t}\n"
			       << "\tif (Status == 0)\n\t{\n\t\tObject = " << Type
			       << "::FromNative(Context.GetNativeContext(), static_cast<uint64>(NativeObject));\n"
			       << "\t\tOutcome = " << Outcome << "::Succeeded;\n\t}\n}\n\n";
		}
		else
		{
			Source << "\tif (Target.GetNativeContext() == 0 || Target.GetNativeObject() == 0)\n\t{\n"
			       << "\t\treturn;\n\t}\n\tfloat NativeValue = 0.0f;\n"
			       << "\tconst std::uint32_t Status = ::" << Symbol
			       << "(static_cast<std::uint64_t>(Target.GetNativeContext()),\n"
			       << "\t\tstatic_cast<std::uint64_t>(Target.GetNativeObject()), " << Parameter
			       << ", &NativeValue);\n\tErrorCode = static_cast<int64>(Status);\n"
			       << "\tif (Status == 0)\n\t{\n\t\tValue = NativeValue;\n"
			       << "\t\tOutcome = " << Outcome << "::Succeeded;\n\t}\n}\n\n";
		}
	}
	Header << "};\n";
	const fs::path ModuleRoot = Directory / "Source" / Module;
	GeneratedFile::WriteSource(ModuleRoot / "Public" / (Plugin + "API.h"), Header.str());
	GeneratedFile::WriteSource(ModuleRoot / "Private" / (Plugin + "API.cpp"), Source.str());
	GeneratedFile::WriteSource(ModuleRoot / "Private" / (Module + "Module.cpp"),
	                           "// Copyright (c) 2026 Nelaric Contributors\n#include \"Modules/ModuleManager.h\"\n\n"
	                           "IMPLEMENT_MODULE(FDefaultModuleImpl, " +
	                               Module + ");\n");
	std::map<std::string, std::vector<const Library*>> Profiles;
	for (const Library& Job : Request.Libraries)
	{
		if (Job.Role == "Client")
		{
			const Json& Target = Job.Plan.at("target");
			Profiles[JsonDocument::Text(Target, "platform") + "/" + JsonDocument::Text(Target, "architecture") + "/" +
			         JsonDocument::Text(Target, "configuration") + "/" + JsonDocument::Text(Target, "type")]
			    .push_back(&Job);
		}
	}
	std::ostringstream Build;
	Build << "// Copyright (c) 2026 Nelaric Contributors\nusing System.IO;\nusing UnrealBuildTool;\n\n"
	      << "public class " << Module << " : ModuleRules\n{\n\tpublic " << Module
	      << "(ReadOnlyTargetRules Target) : base(Target)\n\t{\n"
	      << "\t\tPCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;\n"
	      << "\t\tPublicDependencyModuleNames.AddRange(new[] { \"Core\", \"CoreUObject\", \"Engine\"";
	for (const auto& Dependency : ExtraModules)
	{
		Build << ", " << CppLiteral::Literal(Dependency);
	}
	Build << " });\n"
	      << "\t\tPrivateIncludePaths.Add(Path.Combine(PluginDirectory, \"Source\", \"ThirdParty\", \"ModClient\", "
	         "\"Include\"));\n";
	bool bFirst = true;
	for (const auto& [Profile, Jobs] : Profiles)
	{
		const Json& Target = Jobs.front()->Plan.at("target");
		Build << "\t\t" << (bFirst ? "if" : "else if") << " (Target.Platform == UnrealTargetPlatform."
		      << JsonDocument::Text(Target, "platform") << " && Target.Architecture.ToString() == "
		      << CppLiteral::Literal(JsonDocument::Text(Target, "architecture"))
		      << " && Target.Configuration == UnrealTargetConfiguration." << JsonDocument::Text(Target, "configuration")
		      << " && Target.Type == TargetType." << JsonDocument::Text(Target, "type") << ")\n\t\t{\n";
		bFirst = false;
		for (const Library* Job : Jobs)
		{
			const std::string Name =
			    JsonDocument::Text(Job->Plan, "ownerModule") +
			    Mod::BuildTool::Path::PathToUtf8(
			        Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(Job->Plan, "library")).extension());
			Build << "\t\t\tPublicAdditionalLibraries.Add(Path.Combine(PluginDirectory, \"Source\", \"ThirdParty\", "
			         "\"ModClient\", \"Lib\", "
			      << CppLiteral::Literal(JsonDocument::Text(Target, "platform")) << ", "
			      << CppLiteral::Literal(JsonDocument::Text(Target, "architecture")) << ", "
			      << CppLiteral::Literal(JsonDocument::Text(Target, "configuration")) << ", "
			      << CppLiteral::Literal(JsonDocument::Text(Target, "type")) << ", " << CppLiteral::Literal(Name)
			      << "));\n";
		}
		Build << "\t\t}\n";
	}
	Build << "\t\telse\n\t\t{\n\t\t\tthrow new BuildException(\"No matching generated ModClient library for this "
	         "target.\");\n"
	      << "\t\t}\n\t}\n}\n";
	GeneratedFile::WriteSource(ModuleRoot / (Module + ".Build.cs"), Build.str());
	Json Modules = Json::array({{{"Name", Module}, {"Type", "Runtime"}, {"LoadingPhase", "Default"}}});
	if (!Request.LifecycleArtifact.is_null())
	{
		const Json& Artifact = Request.LifecycleArtifact;
		const std::string Export = Mod::BuildTool::File::ReadFile(
		    Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(Artifact, "sourcePath")));
		if (Mod::BuildTool::Hash::HashBytes(Export) != JsonDocument::Text(Artifact, "contentHash"))
		{
			throw std::runtime_error("The Mod lifecycle export changed during SDK generation.");
		}
		const Json Projection = Json::parse(Export);
		JsonDocument::RequireFormat(Projection, "UMF.ModLifecycleExport");
		const Json& Declarations = Projection.at("declarations");
		if (!Declarations.is_object() || Declarations.size() != 2)
		{
			throw std::runtime_error("The global Mod lifecycle export must declare both SDK base classes.");
		}
		// Match the game's /Script/ModLoader package for Blueprint DLC imports.
		const fs::path LifecycleModule = Directory / "Source" / "ModLoader";
		const std::map<std::string, std::string> LifecycleIncludes{
		    {"ModInstance", "#include \"Engine/GameInstance.h\"\n#include \"UObject/Object.h\"\n"},
		    {"ModInstanceSubSystem", "#include \"ModInstance.h\"\n"}};
		for (const auto& [Name, Includes] : LifecycleIncludes)
		{
			const std::string& Declaration = Declarations.at(Name).get_ref<const std::string&>();
			if (Declaration.empty() || Declaration.size() > 1024 * 1024)
			{
				throw std::runtime_error("Empty or oversized Mod lifecycle declaration: " + Name);
			}
			const std::string Header = "// Copyright (c) 2026 Nelaric Contributors\n"
			                           "// Generated by ModSDKTool from the host's lifecycle export.\n"
			                           "/"
			                           "** @file " +
			                           Name + ".h */\n#pragma once\n\n" + Includes + "\n#include \"" + Name +
			                           ".generated.h\"\n\n" + Declaration;
			GeneratedFile::WriteSource(LifecycleModule / "Public" / (Name + ".h"), Header);
		}
		Mod::BuildTool::File::WriteFileAtomically(
		    Mod::BuildTool::Path::ResolveGeneratedPath(Directory, JsonDocument::Text(Artifact, "relativePath")),
		    Export);
		GeneratedFile::WriteSource(
		    LifecycleModule / "ModLoader.Build.cs",
		    "// Copyright (c) 2026 Nelaric Contributors\nusing UnrealBuildTool;\n\n"
		    "public class ModLoader : ModuleRules\n{\n\tpublic ModLoader(ReadOnlyTargetRules Target) : "
		    "base(Target)\n\t{\n\t\tPCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;\n"
		    "\t\tPublicDependencyModuleNames.AddRange(new[] { \"Core\", \"CoreUObject\", \"Engine\" });\n"
		    "\t}\n}\n");
		GeneratedFile::WriteSource(
		    LifecycleModule / "Private" / "ModLoaderModule.cpp",
		    "// Copyright (c) 2026 Nelaric Contributors\n#include \"Modules/ModuleManager.h\"\n\n"
		    "IMPLEMENT_MODULE(FDefaultModuleImpl, ModLoader);\n");
		Modules.push_back({{"Name", "ModLoader"}, {"Type", "Runtime"}, {"LoadingPhase", "Default"}});
	}
	const Json Descriptor = {{"FileVersion", 3},
	                         {"Version", 1},
	                         {"VersionName", Request.Document.at("sdkVersion")},
	                         {"FriendlyName", Plugin},
	                         {"Description", "Generated typed authoring SDK."},
	                         {"Category", "Mod SDK"},
	                         {"CanContainContent", false},
	                         {"IsBetaVersion", true},
	                         {"Installed", false},
	                         {"EnabledByDefault", true},
	                         {"Modules", Modules}};
	Mod::BuildTool::File::WriteFileAtomically(Directory / (Plugin + ".uplugin"), Descriptor.dump(2) + "\n");
	Mod::BuildTool::File::WriteFileAtomically(Directory / "Config" / "FilterPlugin.ini",
	                                          "; Copyright (c) 2026 Nelaric Contributors\n[FilterPlugin]\n"
	                                          "/Source/ThirdParty/ModClient/...\n/Contract/...\n");
	Mod::BuildTool::File::WriteFileAtomically(Directory / "Contract" / "SdkContract.json",
	                                          Request.Contract.dump(2) + "\n");
	if (!Request.ConnectionArtifact.is_null())
	{
		const fs::path SourcePath =
		    Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(Request.ConnectionArtifact, "sourcePath"));
		if (Mod::BuildTool::Hash::HashFile(SourcePath) != JsonDocument::Text(Request.ConnectionArtifact, "contentHash"))
		{
			throw std::runtime_error("Native connection descriptor changed during SDK generation.");
		}
		const fs::path Destination = Mod::BuildTool::Path::ResolveGeneratedPath(
		    Directory, JsonDocument::Text(Request.ConnectionArtifact, "relativePath"));
		fs::create_directories(Destination.parent_path());
		fs::copy_file(SourcePath, Destination);
	}
	for (const Json& HeaderFile : Request.Headers)
	{
		const fs::path SourcePath = Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(HeaderFile, "sourcePath"));
		if (Mod::BuildTool::Hash::HashFile(SourcePath) != JsonDocument::Text(HeaderFile, "contentHash"))
		{
			throw std::runtime_error("Public header changed during SDK generation.");
		}
		const fs::path Destination =
		    Mod::BuildTool::Path::ResolveGeneratedPath(Directory / "Source" / "ThirdParty" / "ModClient" / "Include",
		                                               JsonDocument::Text(HeaderFile, "relativePath"));
		fs::create_directories(Destination.parent_path());
		fs::copy_file(SourcePath, Destination);
	}
}
} // namespace Mod::SdkTool
