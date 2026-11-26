// Copyright (c) 2026 Nelaric Contributors
#include "ExtensionWriter.h"

#include "Common/ValueLayout.h"

#include "ModHeaderTool/src/Generation/Artifacts/GeneratedSource.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <cctype>
#include <set>
#include <stdexcept>
#include <sstream>
#include <vector>

namespace Mod::HeaderTool
{
namespace
{
std::string Name(const std::string& Text)
{
	return Text.empty() ? "NAME_None" : "FName(" + GeneratedSource::Wide(Text) + ")";
}
std::string NativeIdentifier(std::string_view Id)
{
	std::string Symbol = "UMF_Ext_";
	for (const char Ch : Id)
	{
		Symbol.push_back(std::isalnum(static_cast<unsigned char>(Ch)) ? Ch : '_');
	}
	return Symbol;
}

void WriteAutomaticDispatch(const Extension& Entry, std::ostringstream& Header)
{
	const bool bFloating = Entry.Scalar == "float" || Entry.Scalar == "double";
	Header << "    static FValue Dispatch(" << Entry.Owner
	       << "& Source, FValue Value, TFunctionRef<FValue(FValue)> Original)\n"
	       << "    {\n        const auto Validate = "
	       << (bFloating ? "[](FValue Input) { return FMath::IsFinite(Input); }" : "[](FValue) { return true; }")
	       << ";\n        if (!Validate(Value))\n        {\n            return Original(Value);\n        }\n"
	       << "        Mod::Extension::FEndpoint Endpoint;\n"
	       << "        const auto Result = Invoke(Source, Endpoint, Value,\n"
	       << "            [&Original](FValue Effective) { return Mod::Extension::TCoreResult<FValue>"
	          "{Original(Effective), Mod::API::EError::None, true}; },\n"
	       << "            Validate, Validate);\n"
	       << "        if (Result.Error != Mod::API::EError::None)\n"
	       << "        {\n            UE_LOG(LogTemp, Warning, "
	       << GeneratedSource::Wide(Entry.Id + " failed (%d), committed=%d.")
	       << ", static_cast<int32>(Result.Error), Result.bCommitted);\n"
	       << "            return Result.bCommitted && Validate(Result.Value) ? Result.Value : FValue{};\n"
	       << "        }\n        return Result.Value;\n    }\n";
}

void WriteWireFields(const std::vector<Mod::Tools::ValueField>& Fields, std::ostringstream& Out, unsigned Depth)
{
	const std::string Indent((Depth + 1) * 4, ' ');
	for (const auto& Field : Fields)
	{
		if (!Field.Children.empty())
		{
			Out << Indent << "struct\n" << Indent << "{\n";
			WriteWireFields(Field.Children, Out, Depth + 1);
			Out << Indent << "} " << Field.Name << ";\n";
			continue;
		}
		const auto Type = Field.Scalar;
		const char* Native = Type == "FloatProperty"    ? "float"
		                     : Type == "DoubleProperty" ? "double"
		                     : Type == "IntProperty"    ? "int32_t"
		                     : Type == "Int64Property"  ? "int64_t"
		                                                : "uint8_t";
		Out << Indent << Native << " " << Field.Name << ";\n";
	}
}

void WriteNativeExtensions(const GenerationModel& Model, std::map<std::string, std::string>& Files)
{
	if (Model.LifecycleSource.empty() || Model.Extensions.empty())
	{
		return;
	}
	const auto HeaderName = Model.Input.GameId + ".modextensionsabi.h";
	std::ostringstream Header;
	Header << GeneratedSource::Prologue(HeaderName) << "#include \"ModExtensionABI.h\"\n\n#pragma pack(push, 1)\n";
	std::set<std::string> Symbols;
	for (const auto& [Id, Entry] : Model.Extensions)
	{
		const auto Symbol = NativeIdentifier(Id);
		if (!Symbols.insert(Symbol).second)
		{
			throw std::runtime_error("Native extension identifiers collide: " + Id);
		}
		Header << "#define " << Symbol << "_ID \"" << Id << "\"\n"
		       << "#define " << Symbol << "_SIGNATURE \"" << Entry.Signature() << "\"\n";
		if (Entry.PayloadKind == "Struct")
		{
			Header << "typedef struct " << Symbol << "_Value\n{\n";
			WriteWireFields(Mod::Tools::ValueLayout::Parse(Entry.PayloadLayout), Header, 0);
			Header << "} " << Symbol << "_Value;\n\n";
		}
		else
		{
			const auto& Scalar = Entry.Scalar;
			const std::string Type = Entry.PayloadKind == "Object"            ? "uint64_t"
			                         : Scalar == "bool" || Scalar == "uint8"  ? "uint8_t"
			                         : Scalar == "int32" || Scalar == "int64" ? Scalar + "_t"
			                                                                  : Scalar;
			Header << "typedef " << Type << " " << Symbol << "_Value;\n\n";
		}
	}
	Header << "#pragma pack(pop)\n";
	Files["SDK/Public/" + HeaderName] = Header.str();
}

void WriteEventMember(const Extension& Entry, const std::string& Symbol, std::ostringstream& Body)
{
	const auto& Host = Entry.HostMember;
	Body << "\n"
	     << Host.ReturnType << " " << Entry.Owner << "::" << Entry.Name << "(" << Host.ParameterType << " Input)"
	     << (Host.bNoexcept ? " noexcept" : "") << "\n{\n    ::Mod::Generated::" << Entry.Module << "::" << Symbol
	     << "::Publish(*this, this->" << Host.Endpoint << ", Input);\n}\n";
}
} // namespace

void WriteExtensions(const GenerationModel& Model, std::map<std::string, std::string>& Files)
{
	WriteNativeExtensions(Model, Files);
	for (const auto& Module : Model.Input.Modules)
	{
		std::set<std::string> Includes, Symbols;
		std::ostringstream Header, Definitions, Members;
		bool bFound = false;
		for (const auto& [Id, Entry] : Model.Extensions)
		{
			if (Entry.Module != Module.Name)
			{
				continue;
			}
			bFound = true;
			Definitions << "    {\n    auto& Entry = Out.AddDefaulted_GetRef();\n"
			            << "    Entry.Id = " << Name(Id) << ";\n"
			            << "    Entry.Version = " << GeneratedSource::Wide(Entry.Version) << ";\n"
			            << "    Entry.Signature = " << GeneratedSource::Wide(Entry.Signature()) << ";\n"
			            << "    Entry.Kind = Mod::Extension::EKind::" << (Entry.bHook ? "Hook" : "Event") << ";\n"
			            << "    Entry.Scalar = Mod::Extension::ScalarType<"
			            << (Entry.PayloadKind.empty() ? Entry.Scalar : "Mod::Extension::FPayload") << ">();\n"
			            << "    Entry.bQueued = " << (Entry.bQueued ? "true" : "false") << ";\n"
			            << "    Entry.bImmediateUnsubscribe = " << (Entry.bImmediateUnsubscribe ? "true" : "false")
			            << ";\n"
			            << "    Entry.bPre = " << (Entry.bPre ? "true" : "false") << ";\n"
			            << "    Entry.bPost = " << (Entry.bPost ? "true" : "false") << ";\n"
			            << "    Entry.bOn = " << (Entry.bOn ? "true" : "false") << ";\n"
			            << "    Entry.bAuthorityOnly = " << (Entry.bAuthorityOnly ? "true" : "false") << ";\n"
			            << "    Entry.Operations = static_cast<Mod::Extension::EOperation>("
			            << unsigned(Entry.Operations) << ");\n    Entry.Permission = " << Name(Entry.Permission)
			            << ";\n    Entry.PublishPermission = " << Name(Entry.PublishPermission) << ";\n";
			if (!Entry.DispatchId.empty())
			{
				Definitions << "    Entry.DispatchId = " << Name(Entry.DispatchId) << ";\n    }\n";
				continue;
			}
			if (Entry.PayloadKind == "Struct")
			{
				Definitions << "    Entry.Payload.Struct = FSoftObjectPath("
				            << (Entry.PayloadCpp.empty() ? GeneratedSource::Wide(Entry.PayloadPath)
				                                         : Entry.PayloadCpp + "::StaticStruct()")
				            << ");\n"
				            << "    Entry.Payload.Layout = " << GeneratedSource::Wide(Entry.PayloadLayout) << ";\n"
				            << "    Entry.Payload.BlueprintType = " << Name(Entry.BlueprintPayloadType()) << ";\n";
			}
			else if (Entry.PayloadKind == "Object")
			{
				Definitions << "    Entry.Payload.TypeId = " << Name(Entry.PayloadTypeId) << ";\n";
				if (Entry.PayloadCpp.empty())
				{
					const auto View = std::ranges::find_if(Model.Types,
					                                       [&](const auto& Item) {
						                                       return Item.second.Identity.Id == Entry.PayloadTypeId &&
						                                              Item.second.Identity.Scope == "World";
					                                       });
					if (View == Model.Types.end() ||
					    (!View->second.BlueprintClass.empty() && View->second.BlueprintClass != Entry.PayloadPath))
					{
						throw std::runtime_error(Entry.Location +
						                         ": object payload requires a matching generated World view.");
					}
					Definitions << "    Entry.Payload.Object = FSoftClassPath("
					            << GeneratedSource::Wide(Entry.PayloadPath) << ");\n"
					            << "    Entry.Payload.Signature = " << GeneratedSource::Wide(View->second.Signature)
					            << ";\n";
				}
				else
				{
					Includes.insert(Entry.PayloadHeader);
					Definitions << "    Entry.Payload.Object = FSoftClassPath(" << Entry.PayloadCpp
					            << "::StaticClass()->GetPathName());\n"
					            << "    Entry.Payload.Signature = Mod::API::TBindingTraits<" << Entry.PayloadCpp
					            << ">::Signature;\n";
				}
			}
			if (!Entry.BlueprintClass.empty())
			{
				Definitions << "    Entry.BlueprintClass = FSoftClassPath("
				            << GeneratedSource::Wide(Entry.BlueprintClass) << ");\n";
				if (Entry.bHook)
				{
					Definitions << "    Entry.BlueprintCore = " << Name(Entry.Name) << ";\n"
					            << "    Entry.BlueprintParameter = " << Name(Entry.Input) << ";\n"
					            << "    Entry.BlueprintResult = " << Name(Entry.Output) << ";\n"
					            << "    Entry.BlueprintResultRole = Mod::API::EBlueprintParameter::" << Entry.OutputRole
					            << ";\n";
				}
			}
			else
			{
				Includes.insert(Entry.Header);
				const auto Symbol = Entry.CppHelperName();
				if (!Symbols.insert(Symbol).second)
				{
					throw std::runtime_error(Entry.Location + ": colliding generated extension entry " + Symbol);
				}
				Header << "struct " << Symbol << "\n{\n"
				       << "    static FName Id() { return " << Name(Id) << "; }\n"
				       << "    static const TCHAR* Signature() { return " << GeneratedSource::Wide(Entry.Signature())
				       << "; }\n"
				       << "    using FValue = "
				       << (Entry.PayloadKind == "Struct"   ? Entry.PayloadCpp
				           : Entry.PayloadKind == "Object" ? "const " + Entry.PayloadCpp + "*"
				                                           : Entry.Scalar)
				       << ";\n"
				       << "    using FCallbacks = Mod::Extension::THandlerCalls<"
				       << (Entry.PayloadKind.empty() ? "FValue" : "Mod::Extension::FPayload") << ">;\n";
				if (!Entry.PayloadKind.empty())
				{
					const bool bStruct = Entry.PayloadKind == "Struct";
					Header
					    << "    static Mod::Extension::FPayload Pack(FValue Value) { return Mod::Extension::FPayload::"
					    << (bStruct ? "FromStruct" : "FromObject") << "(Value); }\n"
					    << "    static FValue Unpack(const Mod::Extension::FPayload& Value) { return "
					    << (bStruct
					            ? "Value.GetStruct().GetPtr<FValue>() ? *Value.GetStruct().GetPtr<FValue>() : FValue{}"
					            : "Cast<" + Entry.PayloadCpp + ">(Value.GetObject())")
					    << "; }\n";
				}
				if (Entry.bHook)
				{
					Header << "    static Mod::Extension::THookResult<FValue> Invoke(" << Entry.Owner
					       << "& Source, Mod::Extension::FEndpoint& Endpoint, FValue Value, "
					       << "TFunctionRef<Mod::Extension::TCoreResult<FValue>(FValue)> Core, "
					       << "TFunctionRef<bool(FValue)> ValidateInput, TFunctionRef<bool(FValue)> ValidateResult)\n"
					       << "    {\n        return UModExtensionSubsystem::InvokeScalar<FValue>(Source, Id(), "
					          "Endpoint, Value, Core, "
					          "ValidateInput, ValidateResult);\n    }\n";
				}
				else
				{
					Header << "    static Mod::API::EError Publish(" << Entry.Owner
					       << "& Source, Mod::Extension::FEndpoint& Endpoint, FValue Payload)\n"
					       << "    {\n        return UModExtensionSubsystem::EmitScalar<"
					       << (Entry.PayloadKind.empty() ? "FValue" : "Mod::Extension::FPayload")
					       << ">(Source, Id(), "
					          "Endpoint, "
					       << (Entry.PayloadKind.empty() ? "Payload" : "Pack(Payload)")
					       << ");\n"
					          "    }\n";
				}
				if (!Entry.DefinitionUsr.empty())
				{
					Includes.insert("CoreGlobals.h");
					Includes.insert("Logging/LogMacros.h");
					Includes.insert("Math/UnrealMathUtility.h");
					WriteAutomaticDispatch(Entry, Header);
				}
				Header << "};\n\n";
				if (!Entry.HostMember.Endpoint.empty())
				{
					WriteEventMember(Entry, Symbol, Members);
				}
			}
			Definitions << "    }\n";
		}
		if (!bFound)
		{
			continue;
		}
		const std::string Prefix = "Host/" + Module.Name + "/" + Module.Name;
		const std::string HeaderName = Module.Name + ".modextensions.h";
		Files["SDK/Public/" + HeaderName] =
		    GeneratedSource::Prologue(HeaderName) + "#include \"../../" + Prefix + ".modextensions.h\"\n";
		std::string Output = GeneratedSource::Prologue(HeaderName) + "#include \"ModExtensionSubsystem.h\"\n";
		for (const auto& Include : Includes)
		{
			Output += "#include \"" + Include + "\"\n";
		}
		Files[Prefix + ".modextensions.h"] =
		    Output + "\nnamespace Mod::Generated::" + Module.Name + "\n{\n" + Header.str() + "}\n";
		// Keep the descriptor in the existing retained host translation unit.
		auto& HostSource = Files.at(Prefix + ".modhost.cpp");
		std::string WitnessDeclarations, WitnessCalls;
		if (const auto Map = Model.HookSources.find("Diagnostics/HookSources.json"); Map != Model.HookSources.end())
		{
			const auto Plan = nlohmann::json::parse(Map->second);
			for (const auto& Unit : Plan.at("units"))
			{
				if (Unit.at("owner") == Module.Name)
				{
					const auto Witness = Unit.at("witness").get<std::string>();
					WitnessDeclarations += "extern \"C\" UMF_HOOK_LOCAL void " + Witness + "();\n";
					WitnessCalls += "    " + Witness + "();\n";
				}
			}
		}
		if (!WitnessCalls.empty())
		{
			HostSource += "\n#if defined(__GNUC__) && !defined(_WIN32)\n"
			              "#define UMF_HOOK_LOCAL __attribute__((visibility(\"hidden\")))\n#else\n#define "
			              "UMF_HOOK_LOCAL\n#endif\n" +
			              WitnessDeclarations + "#undef UMF_HOOK_LOCAL\n";
		}
		HostSource +=
		    "\n#include \"" + HeaderName +
		    "\"\n\nnamespace\n{\nvoid DescribeExtensions(TArray<Mod::Extension::FDefinition>& Out)\n{\n" +
		    WitnessCalls + Definitions.str() +
		    "}\nconst Mod::Extension::FGeneratedRegistration ExtensionRegistration(&DescribeExtensions);\n}\n";
		const auto MemberCode = Members.str();
		if (!MemberCode.empty())
		{
			HostSource += MemberCode;
		}
	}
}
} // namespace Mod::HeaderTool
