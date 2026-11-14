// Copyright (c) 2026 Nelaric Contributors
#include "ExtensionWriter.h"

#include "ModHeaderTool/src/Generation/Artifacts/GeneratedSource.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <sstream>

namespace Mod::HeaderTool
{
namespace
{
std::string Name(const std::string& Text)
{
	return Text.empty() ? "NAME_None" : "FName(" + GeneratedSource::Wide(Text) + ")";
}
} // namespace

void WriteExtensions(const GenerationModel& Model, std::map<std::string, std::string>& Files)
{
	for (const auto& Module : Model.Input.Modules)
	{
		std::set<std::string> Includes, Symbols;
		std::ostringstream Header, Definitions;
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
			            << "    Entry.bPre = " << (Entry.bPre ? "true" : "false") << ";\n"
			            << "    Entry.bPost = " << (Entry.bPost ? "true" : "false") << ";\n"
			            << "    Entry.bAuthorityOnly = " << (Entry.bAuthorityOnly ? "true" : "false") << ";\n"
			            << "    Entry.Operations = static_cast<Mod::Extension::EOperation>("
			            << unsigned(Entry.Operations) << ");\n    Entry.Permission = " << Name(Entry.Permission)
			            << ";\n    Entry.PublishPermission = " << Name(Entry.PublishPermission) << ";\n";
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
				auto Symbol = Entry.Owner + "_" + Entry.Name;
				std::replace(Symbol.begin(), Symbol.end(), ':', '_');
				if (!Symbols.insert(Symbol).second)
				{
					throw std::runtime_error(Entry.Location + ": colliding generated extension entry " + Symbol);
				}
				Header << "struct " << Symbol << "\n{\n"
				       << "    static FName Id() { return " << Name(Id) << "; }\n"
				       << "    static const TCHAR* Signature() { return " << GeneratedSource::Wide(Entry.Signature())
				       << "; }\n";
				if (Entry.bHook)
				{
					Header
					    << "    static Mod::Extension::FHookResult Invoke(" << Entry.Owner
					    << "& Source, Mod::Extension::FEndpoint& Endpoint, float Value, "
					    << "TFunctionRef<Mod::Extension::FCoreResult(float)> Core, "
					    << "TFunctionRef<bool(float)> ValidateInput, TFunctionRef<bool(float)> ValidateResult)\n"
					    << "    {\n        return UModExtensionSubsystem::Invoke(Source, Id(), Endpoint, Value, Core, "
					       "ValidateInput, ValidateResult);\n    }\n";
				}
				else
				{
					Header << "    static Mod::API::EError Publish(" << Entry.Owner
					       << "& Source, Mod::Extension::FEndpoint& Endpoint, float Payload)\n"
					       << "    {\n        return UModExtensionSubsystem::Emit(Source, Id(), Endpoint, Payload);\n"
					          "    }\n";
				}
				Header << "};\n\n";
			}
			Definitions << "    }\n";
		}
		if (!bFound)
		{
			continue;
		}
		const std::string Prefix = "Host/" + Module.Name + "/" + Module.Name;
		const std::string HeaderName = Module.Name + ".modextensions.h";
		std::string Output = GeneratedSource::Prologue(HeaderName) + "#include \"ModExtensionSubsystem.h\"\n";
		for (const auto& Include : Includes)
		{
			Output += "#include \"" + Include + "\"\n";
		}
		Files[Prefix + ".modextensions.h"] =
		    Output + "\nnamespace Mod::Generated::" + Module.Name + "\n{\n" + Header.str() + "}\n";
		// Keep the descriptor in the existing retained host translation unit.
		Files.at(Prefix + ".modhost.cpp") +=
		    "\n#include \"" + HeaderName +
		    "\"\n\nnamespace\n{\nvoid DescribeExtensions(TArray<Mod::Extension::FDefinition>& Out)\n{\n" +
		    Definitions.str() +
		    "}\nconst Mod::Extension::FGeneratedRegistration ExtensionRegistration(&DescribeExtensions);\n}\n";
	}
}
} // namespace Mod::HeaderTool
