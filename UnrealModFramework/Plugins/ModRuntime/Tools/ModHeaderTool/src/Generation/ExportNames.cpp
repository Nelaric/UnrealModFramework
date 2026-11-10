// Copyright (c) 2026 Nelaric Contributors
#include "ExportNames.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/ExportNameRules.h"
#include "ModHeaderTool/src/Generation/ExportRedirects.h"
#include "ModHeaderTool/src/Metadata/FunctionRedirect.h"
#include "ModHeaderTool/src/Metadata/MetadataField.h"
#include "ModHeaderTool/src/Metadata/MetadataValue.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"
#include "ModHeaderTool/src/Scan/Lexer.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
using RedirectsByTarget = std::multimap<std::string, const FunctionRedirect*>;

void ExportNames::AssignExportNames(GenerationModel& Model, const MetadataOptions& Options)
{
	std::map<std::string, RedirectsByTarget> TargetsByOwner;
	std::vector<API> Aliases;
	for (auto& API : Model.APIs)
	{
		API.NameLocation =
		    DiagnosticLocation::FormatDiagnosticLocation(API.Macro.File, API.Macro.Line, API.Macro.Column);
		API.ExportName = API.Name;
		if (API.bFullViewMember)
		{
			continue;
		}
		const auto& Type = Model.Types.at(API.Owner);
		ExportNameRules::SetName(API, Type, API.Name);
		const auto Names = std::find_if(API.Macro.Fields.begin(), API.Macro.Fields.end(),
		                                [](const auto& Field) { return Field.Name == "CompatibleNames"; });
		if (Names != API.Macro.Fields.end())
		{
			const auto Source = TextFile::ReadTextFile(API.Macro.File);
			const Detail::Lexer Locations(API.Macro.File, Source);
			for (const auto& Name : Names->Value.Items)
			{
				const auto [Line, Column] = Locations.Location(Name.Offset);
				ExportNameRules::AddAlias(Aliases, API, Type, Name.Text,
				                          DiagnosticLocation::FormatDiagnosticLocation(API.Macro.File, Line, Column));
			}
			continue;
		}
		const auto Owner = ExportRedirects::ReflectedOwner(Type);
		const auto [Targets, bNewOwner] = TargetsByOwner.try_emplace(API.Owner);
		if (bNewOwner)
		{
			Targets->second = ExportRedirects::CollectRedirects(Type, Owner, Options);
		}
		ExportRedirects::AddRedirectAliases(Aliases, API, Type, Owner, Targets->second);
	}
	Model.APIs.insert(Model.APIs.end(), std::make_move_iterator(Aliases.begin()),
	                  std::make_move_iterator(Aliases.end()));
}
} // namespace Mod::HeaderTool
