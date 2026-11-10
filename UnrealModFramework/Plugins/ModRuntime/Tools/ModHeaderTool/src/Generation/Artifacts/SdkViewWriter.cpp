// Copyright (c) 2026 Nelaric Contributors
#include "SdkViewWriter.h"

#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/APIParameter.h"
#include "ModHeaderTool/src/Generation/APIType.h"
#include "ModHeaderTool/src/Generation/Artifacts/CppSignature.h"
#include "ModHeaderTool/src/Generation/Artifacts/GeneratedSource.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"
#include "ModHeaderTool/src/Generation/ModuleInput.h"
#include "ModHeaderTool/src/Generation/ResolvedType.h"
#include "ModHeaderTool/src/Generation/TypeIdentity.h"

#include <map>
#include <set>
#include <sstream>
#include <string>

namespace Mod::HeaderTool
{
using Files = std::map<std::string, std::string>;

void SdkViewWriter::WriteSdk(const GenerationModel& Model, Files& Files)
{
	const auto& Game = Model.Input.GameId;
	std::map<std::string, std::ostringstream> Implementations;
	for (const auto& Module : Model.Input.Modules)
	{
		Implementations[Module.Name] << GeneratedSource::Prologue(Module.Name + ".modviews.cpp") << "#include \""
		                             << Game << ".modviews.h\"\n\n";
	}
	std::ostringstream Calls;
	Calls << GeneratedSource::Prologue(Game + ".modcalls.h") << "#include \"ModAPIObject.h\"\n\n";
	for (const auto& Module : Model.Input.Modules)
	{
		const auto API = GeneratedSource::APIMacro(Module.Name);
		Calls << "#if defined(" << API << ")\n#define UMF_" << API << " " << API << "\n#else\n#define UMF_" << API
		      << " DLLIMPORT\n#endif\n\n";
	}
	std::set<std::string> ArgumentClasses;
	for (const auto& API : Model.APIs)
	{
		for (const auto& Parameter : API.Parameters)
		{
			if (Parameter.Type.Kind == APIType::EKind::ObjectInput && !Model.Types.contains(Parameter.Type.Object))
			{
				ArgumentClasses.insert(Parameter.Type.Object);
			}
		}
	}
	for (const auto& Name : ArgumentClasses)
	{
		Calls << GeneratedSource::Forward(Name);
	}
	for (const auto& [Name, Type] : Model.Types)
	{
		Calls << GeneratedSource::Forward(Name) << "template<> class FModRuntimeObject<" << Name << ">;\n"
		      << "template<> class FModRuntimeObject<const " << Name << ">;\n";
	}
	Calls << "\nnamespace Mod::Generated\n{\n";
	for (const auto& [Name, Type] : Model.Types)
	{
		Calls << "struct " << Type.Symbol << "Calls\n{\n";
		for (auto Index : Type.APIs)
		{
			const auto& API = Model.APIs[Index];
			Calls << "    " << CppSignature::ResultType(API) << " (*" << API.Symbol
			      << ")(const Mod::API::FObjectBinding&";
			if (!API.Parameters.empty())
			{
				Calls << ", " << CppSignature::Parameters(API);
			}
			Calls << ");\n";
		}
		if (Type.APIs.empty())
		{
			Calls << "    uint8 Reserved = 0;\n";
		}
		Calls << "};\n";
	}
	Calls << "}\n\nnamespace Mod::API\n{\n";
	for (const auto& [Name, Type] : Model.Types)
	{
		Calls << "template<> struct TBindingTraits<" << Name << ">\n{\n"
		      << "    static UMF_" << GeneratedSource::APIMacro(Type.Module) << " FName TypeId();\n"
		      << "    static constexpr const TCHAR* Signature = " << GeneratedSource::Wide(Type.Signature) << ";\n};\n"
		      << "template<> struct TBindingTraits<const " << Name << "> : TBindingTraits<" << Name << "> {};\n";
		Implementations[Type.Module] << GeneratedSource::APIMacro(Type.Module) << " FName Mod::API::TBindingTraits<"
		                             << Name << ">::TypeId()\n{\n    static const FName Id("
		                             << GeneratedSource::Wide(Type.Identity.Id) << ");\n    return Id;\n}\n\n";
	}
	Calls << "}\n";
	Files["SDK/Public/" + Game + ".modcalls.h"] = Calls.str();

	std::ostringstream Views;
	Views << GeneratedSource::Prologue(Game + ".modviews.h") << "#include \"" << Game << ".modcalls.h\"\n\n";
	for (const auto& [Name, Type] : Model.Types)
	{
		for (bool ReadOnly : {false, true})
		{
			const std::string Owner = (ReadOnly ? "const " : "") + Name;
			Views << "template<> class FModRuntimeObject<" << Owner << ">\n{\npublic:\n";
			for (auto Index : Type.APIs)
			{
				const auto& API = Model.APIs[Index];
				if (ReadOnly && (!API.bConst || API.bFullViewMember))
				{
					continue;
				}
				Views << "    UMF_" << GeneratedSource::APIMacro(Type.Module) << " " << CppSignature::ResultType(API)
				      << " " << API.ExportName << "(" << CppSignature::Parameters(API, false, true) << ")"
				      << (API.bConst ? " const" : "") << ";\n";
			}
			Views << "private:\n    friend struct Mod::API::FViewFactory;\n"
			      << "    UMF_" << GeneratedSource::APIMacro(Type.Module)
			      << " explicit FModRuntimeObject(Mod::API::FObjectBinding InBinding);\n"
			      << "    Mod::API::FObjectBinding Binding;\n};\n\n";
			Implementations[Type.Module] << GeneratedSource::APIMacro(Type.Module) << " FModRuntimeObject<" << Owner
			                             << ">::FModRuntimeObject(Mod::API::FObjectBinding InBinding)\n"
			                                "    : Binding(MoveTemp(InBinding)) {}\n\n";
		}
	}
	for (const auto& [Name, Type] : Model.Types)
	{
		for (bool ReadOnly : {false, true})
		{
			const std::string Owner = (ReadOnly ? "const " : "") + Name;
			for (auto Index : Type.APIs)
			{
				const auto& API = Model.APIs[Index];
				if (ReadOnly && (!API.bConst || API.bFullViewMember))
				{
					continue;
				}
				Implementations[Type.Module]
				    << GeneratedSource::APIMacro(Type.Module) << " " << CppSignature::ResultType(API)
				    << " FModRuntimeObject<" << Owner << ">::" << API.ExportName << "(" << CppSignature::Parameters(API)
				    << ")" << (API.bConst ? " const" : "") << "\n{\n"
				    << "    auto Calls = this->Binding.ResolveCalls(Mod::API::TBindingTraits<" << Name
				    << ">::TypeId(), Mod::API::TBindingTraits<" << Name << ">::Signature);\n"
				    << "    if (!Calls.IsSuccess()) { return " << CppSignature::ResultType(API)
				    << "::Failure(Calls.GetError()); }\n"
				    << "    return static_cast<const Mod::Generated::" << Type.Symbol << "Calls*>(Calls.GetValue())->"
				    << API.Symbol << "(this->Binding" << (API.Parameters.empty() ? "" : ", ")
				    << CppSignature::Arguments(API) << ");\n}\n\n";
			}
		}
		const auto Leaf = Name.substr(Name.rfind("::") == Name.npos ? 0 : Name.rfind("::") + 2);
		Files["SDK/Public/" + Type.Module + "/" + Leaf + "_" + Type.Symbol + ".modview.h"] =
		    GeneratedSource::Prologue(Leaf + ".modview.h") + "#include \"" + Game + ".modviews.h\"\n";
	}
	Files["SDK/Public/" + Game + ".modviews.h"] = Views.str();
	for (auto& [Module, Implementation] : Implementations)
	{
		Files["SDK/Private/" + Module + ".modviews.cpp"] = Implementation.str();
	}
}
} // namespace Mod::HeaderTool
