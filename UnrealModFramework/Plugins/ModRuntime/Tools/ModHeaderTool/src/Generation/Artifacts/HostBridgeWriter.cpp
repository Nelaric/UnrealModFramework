// Copyright (c) 2026 Nelaric Contributors
#include "HostBridgeWriter.h"

#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/APIParameter.h"
#include "ModHeaderTool/src/Generation/APIType.h"
#include "ModHeaderTool/src/Generation/Artifacts/CppSignature.h"
#include "ModHeaderTool/src/Generation/Artifacts/GeneratedSource.h"
#include "ModHeaderTool/src/Generation/Artifacts/ProjectionWriter.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"
#include "ModHeaderTool/src/Generation/ModuleInput.h"
#include "ModHeaderTool/src/Generation/ResolvedType.h"
#include "ModHeaderTool/src/Generation/TypeIdentity.h"

#include <cctype>
#include <cstddef>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace Mod::HeaderTool
{
using Files = std::map<std::string, std::string>;

void HostBridgeWriter::WriteHost(const GenerationModel& Model, Files& Files)
{
	for (const auto& Module : Model.Input.Modules)
	{
		const std::string Prefix = "Host/" + Module.Name + "/";
		std::string ExportMacro = Module.Name;
		for (auto& C : ExportMacro)
		{
			C = static_cast<char>(std::toupper(static_cast<unsigned char>(C)));
		}
		ExportMacro += "_API";
		const std::string Namespace = "Mod::Generated::" + Module.Name;
		Files[Prefix + Module.Name + ".modexports.h"] =
		    GeneratedSource::Prologue(Module.Name + ".modexports.h") + "#include \"ModAPIRegistry.h\"\n#include \"" +
		    Model.Input.GameId + ".modviews.h\"\n\nextern \"C\" UMF_" + ExportMacro + " void UMF_ModHost_" +
		    Module.Name + "(Mod::API::FRegistry& Registry, TModResult<TArray<Mod::API::FRegistration>>& Result);\n" +
		    "extern \"C\" " + ExportMacro + " uint32 UMF_ModHost_" + Module.Name + "_RetainABI(void);\n";

		std::ostringstream Host, Verify;
		Host << GeneratedSource::Prologue(Module.Name + ".modhost.cpp") << "#include \"" << Module.Name
		     << ".modexports.h\"\n"
		     << "#include \"ModAPIHost.h\"\n";
		Verify << GeneratedSource::Prologue(Module.Name + ".modverify.cpp")
		       << "#include <type_traits>\n#include <string_view>\n";
		std::set<std::string> Headers;
		for (const auto& [Name, Type] : Model.Types)
		{
			if (Type.Module == Module.Name)
			{
				Headers.insert(Type.HeaderInclude);
			}
		}
		for (const auto& API : Model.APIs)
		{
			if (API.Module == Module.Name)
			{
				for (const auto& Parameter : API.Parameters)
				{
					if (Parameter.Type.Kind == APIType::EKind::ObjectInput)
					{
						Headers.insert(Parameter.Type.HostInclude);
					}
				}
			}
		}
		for (const auto& Header : Headers)
		{
			Host << "#include " << GeneratedSource::Quote(Header) << "\n";
			Verify << "#include " << GeneratedSource::Quote(Header) << "\n";
		}
		Verify << "\n#define UMF_MHT_STRINGIZE_INNER(Value) #Value\n#define UMF_MHT_STRINGIZE(Value) "
		          "UMF_MHT_STRINGIZE_INNER(Value)\n";
		for (const auto& [Name, Value] : Model.ContextMacros.at(Module.Name))
		{
			Verify << "#ifdef " << Name << "\nstatic_assert(std::string_view(UMF_MHT_STRINGIZE(" << Name
			       << ")) == " << GeneratedSource::Quote(Value) << ", "
			       << GeneratedSource::Quote("MHT Target macro changed: " + Name)
			       << ");\n#else\n#error MHT Target macro is missing: " << Name << "\n#endif\n";
		}
		Verify << "#undef UMF_MHT_STRINGIZE\n#undef UMF_MHT_STRINGIZE_INNER\n";
		Host << "\nnamespace " << Namespace << "\n{\n";
		Verify << "\nnamespace " << Namespace << "::Verification\n{\n";
		for (const auto& API : Model.APIs)
		{
			if (API.Module != Module.Name)
			{
				continue;
			}
			Verify << "using " << API.Symbol << "Member = " << CppSignature::PointerType(API) << ";\n"
			       << "static_assert(std::is_same_v<decltype(static_cast<" << API.Symbol << "Member>(&"
			       << API.DeclaringType << "::" << API.Name << ")), " << API.Symbol << "Member>);\n";
			Host << "static " << CppSignature::ResultType(API) << " " << API.Symbol
			     << "(const Mod::API::FObjectBinding& Binding" << (API.Parameters.empty() ? "" : ", ")
			     << CppSignature::Parameters(API) << ")\n{\n";
			if (!API.Permissions.empty())
			{
				Host << "    static const FName Permissions[] = {";
				for (const auto& Permission : API.Permissions)
				{
					Host << "FName(" << GeneratedSource::Wide(Permission) << "),";
				}
				Host << "};\n";
			}
			Host << "    auto Target = Mod::API::FHostAccess::Enter(Binding, Mod::API::TBindingTraits<" << API.Owner
			     << ">::TypeId(), Mod::API::EScope::" << API.Scope << ", "
			     << (API.Permissions.empty() ? "TConstArrayView<FName>()" : "MakeArrayView(Permissions)") << ", "
			     << (API.bConst && !API.bFullViewMember ? "false" : "true") << ");\n"
			     << "    if (!Target.IsSuccess()) { return " << CppSignature::ResultType(API)
			     << "::Failure(Target.GetError()); }\n";
			for (std::size_t I = 0; I < API.Parameters.size(); ++I)
			{
				if (API.Parameters[I].Type.Kind == APIType::EKind::ObjectInput)
				{
					Host << "    if (auto Status = Mod::API::FHostAccess::ValidateObjectInput(Binding, Arg" << I
					     << "); !Status.IsSuccess()) { return " << CppSignature::ResultType(API)
					     << "::Failure(Status.GetError()); }\n";
				}
			}
			Host << "    using FMember = " << CppSignature::PointerType(API) << ";\n"
			     << "    constexpr FMember Member = static_cast<FMember>(&" << API.DeclaringType << "::" << API.Name
			     << ");\n"
			     << "    " << (API.Return.Kind == APIType::EKind::Void ? "" : "auto Value = ") << "(CastChecked<"
			     << API.Owner << ">(Target.GetValue())->*Member)(" << CppSignature::Arguments(API, true) << ");\n";
			if (API.Return.Kind == APIType::EKind::Object)
			{
				Host << "    auto Projected = Mod::API::FHostAccess::ProjectObject(Binding, const_cast<"
				     << API.Return.Object << "*>(Value), Mod::API::TBindingTraits<" << API.Return.Object
				     << ">::TypeId(), Mod::API::TBindingTraits<" << API.Return.Object << ">::Signature, "
				     << (API.Return.bConst ? "true" : "false") << ");\n"
				     << "    if (!Projected.IsSuccess()) { return " << CppSignature::ResultType(API)
				     << "::Failure(Projected.GetError()); }\n"
				     << "    return " << CppSignature::ResultType(API) << "::Success(Mod::API::FViewFactory::Make<"
				     << (API.Return.bConst ? "const " : "") << API.Return.Object
				     << ">(MoveTemp(Projected.GetValue())));\n";
			}
			else
			{
				Host << "    return " << CppSignature::ResultType(API) << "::Success("
				     << (API.Return.Kind == APIType::EKind::Void ? "" : "Value") << ");\n";
			}
			Host << "}\n\n";
		}
		for (const auto& [Name, Type] : Model.Types)
		{
			if (Type.Module != Module.Name)
			{
				continue;
			}
			Host << "static const Mod::Generated::" << Type.Symbol << "Calls " << Type.Symbol << "Table = {";
			for (auto Index : Type.APIs)
			{
				Host << "&" << Model.APIs[Index].Symbol << ",";
			}
			Host << "};\n";
		}
		Host << "\nstatic TModResult<TArray<Mod::API::FRegistration>> Register(Mod::API::FRegistry& Registry)\n{\n"
		     << "    TArray<Mod::API::FRegistration> Registrations;\n";
		for (const auto& [Name, Type] : Model.Types)
		{
			if (Type.Module != Module.Name)
			{
				continue;
			}
			Host << "    {\n    Mod::API::FTypeDescriptor Descriptor;\n"
			     << "    Descriptor.TypeId = Mod::API::TBindingTraits<" << Name << ">::TypeId();\n"
			     << "    Descriptor.Class = " << Name << "::StaticClass();\n"
			     << "    Descriptor.Scope = Mod::API::EScope::" << Type.Identity.Scope << ";\n"
			     << "    Descriptor.Signature = Mod::API::TBindingTraits<" << Name << ">::Signature;\n"
			     << "    Descriptor.Calls = &" << Type.Symbol << "Table;\n";
			if (Type.bAllowWorldless)
			{
				Host << "    Descriptor.bAllowWorldless = true;\n";
			}
			if (Type.bFullView)
			{
				Host << "    Descriptor.bFullView = true;\n";
			}
			if (Type.bLocalPlayerSubsystem && Type.Identity.Scope == "Player")
			{
				Host << "    Descriptor.bLocalPlayerSubsystem = true;\n";
			}
			for (auto Index : Type.APIs)
			{
				const auto& API = Model.APIs[Index];
				Host << "    Descriptor.APIs.Add({" << GeneratedSource::Wide(API.Id) << ", "
				     << GeneratedSource::Wide(API.Version) << ", "
				     << GeneratedSource::Wide(ProjectionWriter::APIJson(Model, API)["signatureHash"].get<std::string>())
				     << "});\n";
			}
			Host << "    auto Registration = Registry.RegisterType(MoveTemp(Descriptor));\n"
			     << "    if (!Registration.IsSuccess()) { return "
			        "TModResult<TArray<Mod::API::FRegistration>>::Failure(Registration.GetError()); }\n"
			     << "    Registrations.Add(MoveTemp(Registration.GetValue()));\n    }\n";
		}
		Host << "    return TModResult<TArray<Mod::API::FRegistration>>::Success(MoveTemp(Registrations));\n}\n}\n";
		Verify << "}\n";
		Host << "\n#if defined(UMF_MOD_HOST_LINK_WITNESS)\n"
		        "extern \"C\" __declspec(dllexport) decltype(&UMF_ModHost_"
		     << Module.Name << ") UMF_MOD_HOST_LINK_WITNESS(void)\n{\n    return &UMF_ModHost_" << Module.Name
		     << ";\n}\n#endif\n";
		Host << "\nextern \"C\" " << ExportMacro << " void UMF_ModHost_" << Module.Name
		     << "(Mod::API::FRegistry& Registry, TModResult<TArray<Mod::API::FRegistration>>& Result)\n{\n"
		     << "    if (UMF_ModHost_" << Module.Name << "_RetainABI() != 1)\n    {\n"
		     << "        Result = "
		        "TModResult<TArray<Mod::API::FRegistration>>::Failure(Mod::API::EError::SignatureMismatch);\n"
		     << "        return;\n    }\n    Result = " << Namespace << "::Register(Registry);\n}\n";
		Host << "\nstatic const Mod::API::FGeneratedHostRegistration GeneratedHostRegistration(&UMF_ModHost_"
		     << Module.Name << ");\n";
		Files[Prefix + Module.Name + ".modhost.cpp"] = Host.str();
		Files[Prefix + Module.Name + ".modverify.cpp"] = Verify.str();
	}
}
} // namespace Mod::HeaderTool
