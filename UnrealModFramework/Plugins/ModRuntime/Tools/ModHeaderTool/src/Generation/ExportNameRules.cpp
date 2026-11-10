// Copyright (c) 2026 Nelaric Contributors
#include "ExportNameRules.h"

#include "ModHeaderTool/src/Generation/TypeIdentity.h"
#include "ModHeaderTool/src/Hash/Hash.h"
#include "ModHeaderTool/src/Metadata/MetadataName.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Mod::HeaderTool
{
std::string ExportNameRules::Fold(std::string Text)
{
	std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char C) { return std::tolower(C); });
	return Text;
}

void ExportNameRules::SetName(API& API, const ResolvedType& Type, const std::string& Name)
{
	API.ExportName = Name;
	API.Id = Fold("api/" + Type.Identity.Id + "/" + Name);
	API.Symbol = "A_" + Hash::HashBytes(API.Id).substr(0, 16);
}

void ExportNameRules::AddAlias(std::vector<API>& Aliases, const API& Original, const ResolvedType& Type,
                               const std::string& Name, const std::string& Source)
{
	if (!MetadataName::IsCompatibleFunctionName(Name) || Fold(Name) == "none" || Name == "Binding" ||
	    Name == "FModRuntimeObject")
	{
		throw std::runtime_error(Source + ": error: invalid compatible export name '" + Name + "'.");
	}
	if (Fold(Name) == Fold(Original.Name))
	{
		throw std::runtime_error(Source + ": error: compatible export name '" + Name +
		                         "' duplicates the current name at " + Original.NameLocation + ".");
	}
	API Alias = Original;
	Alias.NameLocation = Source;
	SetName(Alias, Type, Name);
	Aliases.push_back(std::move(Alias));
}
} // namespace Mod::HeaderTool
