// Copyright (c) 2026 Nelaric Contributors
#include "Sdk.h"

#include "Hash.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Mod::SdkTool
{
namespace
{
using namespace Mod::BuildTool;
const std::string LineDoc(3, '/');

void WriteSource(const fs::path& File, std::string_view TextValue)
{
	std::string Encoded = "\xef\xbb\xbf";
	for (char Character : TextValue)
	{
		if (Character == '\n')
		{
			Encoded.push_back('\r');
		}
		Encoded.push_back(Character);
	}
	WriteFileAtomically(File, Encoded);
}

std::string Literal(const std::string& Value)
{
	return FJson(Value).dump();
}

void WriteReference(std::ostringstream& Header, const std::string& Name, const std::string& SecondName,
                    const std::string& APIMacro)
{
	Header << LineDoc << " Opaque values supplied by the host; no game object layout is exposed.\n"
	       << "USTRUCT(BlueprintType)\nstruct " << APIMacro << " " << Name << "\n{\n\tGENERATED_BODY()\n\npublic:\n"
	       << "\t" << LineDoc << " Creates a reference from host-owned, process-local tokens.\n"
	       << "\tstatic " << Name << " FromNative(uint64 InContext, uint64 In" << SecondName << ")\n\t{\n"
	       << "\t\t" << Name << " Result;\n\t\tResult.Context = InContext;\n\t\tResult." << SecondName << " = In"
	       << SecondName << ";\n\t\treturn Result;\n\t}\n\n"
	       << "\t" << LineDoc << " Returns the host-issued calling context token.\n"
	       << "\tuint64 GetNativeContext() const { return Context; }\n\n"
	       << "\t" << LineDoc << " Returns the host-issued target token.\n"
	       << "\tuint64 GetNative" << SecondName << "() const { return " << SecondName << "; }\n\n"
	       << "private:\n\tUPROPERTY(Transient)\n\tuint64 Context = 0;\n\n"
	       << "\tUPROPERTY(Transient)\n\tuint64 " << SecondName << " = 0;\n};\n\n";
}

std::string NativeScalar(const std::string& Type)
{
	if (Type == "float" || Type == "double")
	{
		return Type;
	}
	return Type == "bool" ? "std::uint8_t" : "std::" + Type + "_t";
}

void WriteTypedMethod(std::ostringstream& Header, std::ostringstream& Source, const FJson& API,
                      const std::map<std::string, std::string>& Types, const std::string& Context,
                      const std::string& Outcome, const std::string& LibraryClass, const std::string& APIMacro,
                      const std::string& Plugin)
{
	const bool bProvider = API.contains("provider");
	std::string Signature = (bProvider ? Context : Types.at(Text(API, "targetTypeId"))) + " Target";
	const std::string TargetToken = bProvider ? "GetNativePlayerScope()" : "GetNativeObject()";
	std::string NativeSignature = "std::uint64_t, std::uint64_t";
	std::string Arguments = "static_cast<std::uint64_t>(Target.GetNativeContext()), "
	                        "static_cast<std::uint64_t>(Target." +
	                        TargetToken + ")";
	std::ostringstream Prepare;
	std::size_t Index = 0;
	for (const auto& Parameter : API.at("parameters"))
	{
		const std::string Arg = "Arg" + std::to_string(Index++);
		if (Parameter.at("kind") == "ObjectInput")
		{
			Signature += ", " + std::string(Parameter.at("const").get<bool>() ? "const " : "") +
			             Text(Parameter, "class") + "* " + Arg;
			NativeSignature += ", const char*, std::uint32_t";
			Prepare << "\tif (" << Arg << " && !IsValid(" << Arg << ")) { return; }\n"
			        << "\tconst FString " << Arg << "Path = " << Arg << " ? " << Arg << "->GetPathName() : FString();\n"
			        << "\tconst FTCHARToUTF8 " << Arg << "Utf8(*" << Arg << "Path, " << Arg << "Path.Len());\n";
			Arguments +=
			    ", " + Arg + " ? " + Arg + "Utf8.Get() : nullptr, static_cast<std::uint32_t>(" + Arg + "Utf8.Length())";
		}
		else
		{
			const auto Type = Text(Parameter, "cppType");
			Signature += ", " + Type + " " + Arg;
			NativeSignature += ", " + NativeScalar(Type);
			Arguments += ", static_cast<" + NativeScalar(Type) + ">(" + Arg + ")";
		}
	}
	const auto& Result = API.at("result");
	const auto ResultKind = Text(Result, "kind");
	const bool bObject = ResultKind == "ObjectReference";
	const bool bResult = ResultKind != "Void";
	std::string ResultType;
	std::string NativeResult;
	if (bResult)
	{
		ResultType = bObject ? Types.at(Text(Result, "typeId")) : Text(Result, "cppType");
		NativeResult = bObject ? "std::uint64_t" : NativeScalar(ResultType);
		Signature += ", " + ResultType + "& Value";
		NativeSignature += ", " + NativeResult + "*";
		Arguments += ", &NativeValue";
	}
	Signature += ", " + Outcome + "& Outcome, int64& ErrorCode";
	const auto Name = Text(API, "name");
	const auto Symbol = Text(API.at("native"), "symbol");
	Header << "\t" << LineDoc << " Invokes the bound API on the game thread.\n";
	if (API.at("blueprint").get<bool>())
	{
		Header << "\tUFUNCTION(BlueprintCallable, Category = \"Mod|" << Plugin
		       << "\", meta = (DisplayName = " << Literal(Text(API, "displayName"))
		       << ", ExpandEnumAsExecs = \"Outcome\", Keywords = " << Literal(Text(API, "id")) << "))\n";
	}
	Header << "\tstatic " << APIMacro << " void " << Name << "(" << Signature << ");\n\n";
	Source << "static_assert(std::is_same_v<decltype(&::" << Symbol << "), std::uint32_t (*)(" << NativeSignature
	       << ")>, \"Generated native client ABI does not match ContextHandlesV1\");\n\n"
	       << "void " << LibraryClass << "::" << Name << "(" << Signature << ")\n{\n\tOutcome = " << Outcome
	       << "::Failed;\n\tErrorCode = -1;\n";
	if (bResult)
	{
		Source << "\tValue = {};\n";
	}
	Source << "\tif (!IsInGameThread()) { ErrorCode = -3; return; }\n"
	       << "\tif (Target.GetNativeContext() == 0 || Target." << TargetToken << " == 0) { return; }\n"
	       << Prepare.str();
	if (bResult)
	{
		Source << "\t" << NativeResult << " NativeValue = {};\n";
	}
	Source << "\tErrorCode = static_cast<int64>(::" << Symbol << "(" << Arguments << "));\n"
	       << "\tif (ErrorCode != 0) { return; }\n";
	if (bObject)
	{
		Source << "\tif (NativeValue == 0) { ErrorCode = -2; return; }\n"
		       << "\tValue = " << ResultType
		       << "::FromNative(Target.GetNativeContext(), static_cast<uint64>(NativeValue));\n";
	}
	else if (bResult)
	{
		Source << "\tValue = static_cast<" << ResultType << ">(NativeValue);\n";
	}
	Source << "\tOutcome = " << Outcome << "::Succeeded;\n}\n\n";
}
} // namespace

void GenerateSdkPlugin(const FSdkRequest& Request, const fs::path& Directory)
{
	using namespace Mod::BuildTool;
	const std::string Plugin = Text(Request.Document, "pluginName");
	const std::string Module = Plugin + "Runtime";
	std::string APIMacro = Module + "_API";
	std::transform(APIMacro.begin(), APIMacro.end(), APIMacro.begin(),
	               [](unsigned char Character) { return static_cast<char>(std::toupper(Character)); });
	const std::string Context = "F" + Plugin + "PlayerContext";
	const std::string Outcome = "E" + Plugin + "CallOutcome";
	const std::string LibraryClass = "U" + Plugin + "Library";
	std::map<std::string, std::string> Types;
	for (const FJson& Type : Request.Contract.at("types"))
	{
		Types[Text(Type, "id")] = Text(Type, "cppName");
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
					EngineIncludes.insert(Text(Parameter, "include"));
					const auto Dependency = Text(Parameter, "module");
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
		Header << "#include " << Literal(Include) << "\n";
	}
	Header << "#include \"" << Plugin << "API.generated.h\"\n\n"
	       << LineDoc << " Reports native invocation success without hiding an error branch.\n"
	       << "UENUM(BlueprintType)\nenum class " << Outcome << " : uint8\n{\n"
	       << "\t" << LineDoc << " Call succeeded.\n\tSucceeded,\n\t" << LineDoc << " Call failed.\n\tFailed,\n};\n\n";
	WriteReference(Header, Context, "PlayerScope", APIMacro);
	for (const auto& [Id, Name] : Types)
	{
		WriteReference(Header, Name, "Object", APIMacro);
	}
	Header << LineDoc << " Typed game SDK calls; all operations execute on the game thread.\n"
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
	for (const FJson& API : Request.Contract.at("apis"))
	{
		Includes.insert(Text(API.at("native"), "header"));
	}
	for (const std::string& Include : Includes)
	{
		Source << "#include \"" << Include << "\"\n";
	}
	Source << "\n";
	for (const FJson& API : Request.Contract.at("apis"))
	{
		if (API.at("shape") == "TypedMethod")
		{
			WriteTypedMethod(Header, Source, API, Types, Context, Outcome, LibraryClass, APIMacro, Plugin);
			continue;
		}
		const std::string Name = Text(API, "name");
		const std::string Symbol = Text(API.at("native"), "symbol");
		const bool bGetter = API.at("shape") == "ObjectGetter";
		const std::string Type = Types.at(Text(API, bGetter ? "resultTypeId" : "targetTypeId"));
		const std::string Parameter = bGetter ? "" : Text(API, "parameterName");
		Header << "\t" << LineDoc << " Invokes the bound API on the game thread; failure has an error pin.\n"
		       << "\tUFUNCTION(BlueprintCallable, Category = \"Mod|" << Plugin
		       << "\", meta = (DisplayName = " << Literal(Text(API, "displayName"))
		       << ", ExpandEnumAsExecs = \"Outcome\", Keywords = " << Literal(Text(API, "id")) << "))\n"
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
		Source << Signature << "\n{\n\tOutcome = " << Outcome << "::Failed;\n\tErrorCode = -1;\n"
		       << (bGetter ? "\tObject = " + Type + "();\n" : "\tValue = 0.0f;\n")
		       << "\tif (!IsInGameThread())\n\t{\n\t\tErrorCode = -3;\n\t\treturn;\n\t}\n";
		if (bGetter)
		{
			Source << "\tif (Context.GetNativeContext() == 0 || Context.GetNativePlayerScope() == 0)\n\t{\n"
			       << "\t\treturn;\n\t}\n\tstd::uint64_t NativeObject = 0;\n"
			       << "\tconst std::uint32_t Status = ::" << Symbol
			       << "(static_cast<std::uint64_t>(Context.GetNativeContext()),\n"
			       << "\t\tstatic_cast<std::uint64_t>(Context.GetNativePlayerScope()), &NativeObject);\n"
			       << "\tErrorCode = static_cast<int64>(Status);\n"
			       << "\tif (Status == 0 && NativeObject == 0)\n\t{\n\t\tErrorCode = -2;\n\t\treturn;\n\t}\n"
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
	WriteSource(ModuleRoot / "Public" / (Plugin + "API.h"), Header.str());
	WriteSource(ModuleRoot / "Private" / (Plugin + "API.cpp"), Source.str());
	WriteSource(ModuleRoot / "Private" / (Module + "Module.cpp"),
	            "// Copyright (c) 2026 Nelaric Contributors\n#include \"Modules/ModuleManager.h\"\n\n"
	            "IMPLEMENT_MODULE(FDefaultModuleImpl, " +
	                Module + ");\n");
	std::map<std::string, std::vector<const FLibrary*>> Profiles;
	for (const FLibrary& Job : Request.Libraries)
	{
		if (Job.Role == "Client")
		{
			const FJson& Target = Job.Plan.at("target");
			Profiles[Text(Target, "platform") + "/" + Text(Target, "architecture") + "/" +
			         Text(Target, "configuration") + "/" + Text(Target, "type")]
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
		Build << ", " << Literal(Dependency);
	}
	Build << " });\n"
	      << "\t\tPrivateIncludePaths.Add(Path.Combine(PluginDirectory, \"Source\", \"ThirdParty\", \"ModClient\", "
	         "\"Include\"));\n";
	bool bFirst = true;
	for (const auto& [Profile, Jobs] : Profiles)
	{
		const FJson& Target = Jobs.front()->Plan.at("target");
		Build << "\t\t" << (bFirst ? "if" : "else if") << " (Target.Platform == UnrealTargetPlatform."
		      << Text(Target, "platform")
		      << " && Target.Architecture.ToString() == " << Literal(Text(Target, "architecture"))
		      << " && Target.Configuration == UnrealTargetConfiguration." << Text(Target, "configuration")
		      << " && Target.Type == TargetType." << Text(Target, "type") << ")\n\t\t{\n";
		bFirst = false;
		for (const FLibrary* Job : Jobs)
		{
			const std::string Name =
			    Text(Job->Plan, "ownerModule") + PathToUtf8(PathFromUtf8(Text(Job->Plan, "library")).extension());
			Build << "\t\t\tPublicAdditionalLibraries.Add(Path.Combine(PluginDirectory, \"Source\", \"ThirdParty\", "
			         "\"ModClient\", \"Lib\", "
			      << Literal(Text(Target, "platform")) << ", " << Literal(Text(Target, "architecture")) << ", "
			      << Literal(Text(Target, "configuration")) << ", " << Literal(Text(Target, "type")) << ", "
			      << Literal(Name) << "));\n";
		}
		Build << "\t\t}\n";
	}
	Build << "\t\telse\n\t\t{\n\t\t\tthrow new BuildException(\"No matching generated ModClient library for this "
	         "target.\");\n"
	      << "\t\t}\n\t}\n}\n";
	WriteSource(ModuleRoot / (Module + ".Build.cs"), Build.str());
	FJson Modules = FJson::array({{{"Name", Module}, {"Type", "Runtime"}, {"LoadingPhase", "Default"}}});
	if (!Request.LifecycleArtifact.is_null())
	{
		const FJson& Artifact = Request.LifecycleArtifact;
		const std::string Export = ReadFile(PathFromUtf8(Text(Artifact, "sourcePath")));
		if (HashBytes(Export) != Text(Artifact, "contentHash"))
		{
			throw std::runtime_error("The Mod lifecycle export changed during SDK generation.");
		}
		const FJson Projection = FJson::parse(Export);
		RequireFormat(Projection, "UMF.ModLifecycleExport");
		const FJson& Declarations = Projection.at("declarations");
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
			WriteSource(LifecycleModule / "Public" / (Name + ".h"), Header);
		}
		WriteFileAtomically(ResolveGeneratedPath(Directory, Text(Artifact, "relativePath")), Export);
		WriteSource(LifecycleModule / "ModLoader.Build.cs",
		            "// Copyright (c) 2026 Nelaric Contributors\nusing UnrealBuildTool;\n\n"
		            "public class ModLoader : ModuleRules\n{\n\tpublic ModLoader(ReadOnlyTargetRules Target) : "
		            "base(Target)\n\t{\n\t\tPCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;\n"
		            "\t\tPublicDependencyModuleNames.AddRange(new[] { \"Core\", \"CoreUObject\", \"Engine\" });\n"
		            "\t}\n}\n");
		WriteSource(LifecycleModule / "Private" / "ModLoaderModule.cpp",
		            "// Copyright (c) 2026 Nelaric Contributors\n#include \"Modules/ModuleManager.h\"\n\n"
		            "IMPLEMENT_MODULE(FDefaultModuleImpl, ModLoader);\n");
		Modules.push_back({{"Name", "ModLoader"}, {"Type", "Runtime"}, {"LoadingPhase", "Default"}});
	}
	const FJson Descriptor = {{"FileVersion", 3},
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
	WriteFileAtomically(Directory / (Plugin + ".uplugin"), Descriptor.dump(2) + "\n");
	WriteFileAtomically(Directory / "Config" / "FilterPlugin.ini",
	                    "; Copyright (c) 2026 Nelaric Contributors\n[FilterPlugin]\n"
	                    "/Source/ThirdParty/ModClient/...\n/Contract/...\n");
	WriteFileAtomically(Directory / "Contract" / "SdkContract.json", Request.Contract.dump(2) + "\n");
	if (!Request.ConnectionArtifact.is_null())
	{
		const fs::path SourcePath = PathFromUtf8(Text(Request.ConnectionArtifact, "sourcePath"));
		if (HashFile(SourcePath) != Text(Request.ConnectionArtifact, "contentHash"))
		{
			throw std::runtime_error("Native connection descriptor changed during SDK generation.");
		}
		const fs::path Destination = ResolveGeneratedPath(Directory, Text(Request.ConnectionArtifact, "relativePath"));
		fs::create_directories(Destination.parent_path());
		fs::copy_file(SourcePath, Destination);
	}
	for (const FJson& HeaderFile : Request.Headers)
	{
		const fs::path SourcePath = PathFromUtf8(Text(HeaderFile, "sourcePath"));
		if (HashFile(SourcePath) != Text(HeaderFile, "contentHash"))
		{
			throw std::runtime_error("Public header changed during SDK generation.");
		}
		const fs::path Destination = ResolveGeneratedPath(Directory / "Source" / "ThirdParty" / "ModClient" / "Include",
		                                                  Text(HeaderFile, "relativePath"));
		fs::create_directories(Destination.parent_path());
		fs::copy_file(SourcePath, Destination);
	}
}
} // namespace Mod::SdkTool
