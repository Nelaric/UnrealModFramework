// Copyright (c) 2026 Nelaric Contributors
#include "GenerationModel.h"

#include "Hash.h"
#include "ModHeaderTool/MetadataOptions.h"
#include "ModHeaderTool/MetadataValidator.h"
#include "SourceLexer.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <stdexcept>

namespace Mod::HeaderTool
{
namespace
{
std::string Fold(std::string Text)
{
	std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char C) { return std::tolower(C); });
	return Text;
}

std::string Location(const fs::path& File, std::size_t Line, std::size_t Column)
{
	return PathToUtf8(File) + "(" + std::to_string(Line) + "," + std::to_string(Column) + ")";
}

void SetName(FAPI& API, const FResolvedType& Type, const std::string& Name)
{
	API.ExportName = Name;
	API.Id = Fold("api/" + Type.Identity.Id + "/" + Name);
	API.Symbol = "A_" + HashBytes(API.Id).substr(0, 16);
}

void AddAlias(std::vector<FAPI>& Aliases, const FAPI& API, const FResolvedType& Type, const std::string& Name,
              const std::string& Source)
{
	if (!IsCompatibleFunctionName(Name) || Fold(Name) == "none" || Name == "Binding" || Name == "FModRuntimeObject")
	{
		throw std::runtime_error(Source + ": error: invalid compatible export name '" + Name + "'.");
	}
	if (Fold(Name) == Fold(API.Name))
	{
		throw std::runtime_error(Source + ": error: compatible export name '" + Name +
		                         "' duplicates the current name at " + API.NameLocation + ".");
	}
	FAPI Alias = API;
	Alias.NameLocation = Source;
	SetName(Alias, Type, Name);
	Aliases.push_back(std::move(Alias));
}

bool MatchesOwner(const FRedirectFunctionName& Name, const FRedirectFunctionName& Owner)
{
	return (Name.Package.empty() || Fold(Name.Package) == Owner.Package) &&
	       (Name.Class.empty() || Fold(Name.Class) == Owner.Class);
}

FRedirectFunctionName ReflectedOwner(const FResolvedType& Type)
{
	std::string Class = Type.Identity.CppName;
	if (const auto Separator = Class.rfind("::"); Separator != Class.npos)
	{
		Class.erase(0, Separator + 2);
	}
	// UObject names in Core Redirects omit the UHT class prefix.
	return {Fold("/Script/" + Type.Module), Fold(Class.substr(1)), {}};
}

[[noreturn]] void FailRedirect(const FFunctionRedirect& Redirect, const std::string& Reason)
{
	throw std::runtime_error(Redirect.Location + ": error: [CoreRedirects] FunctionRedirects=" + Redirect.Entry + ": " +
	                         Reason);
}

using FRedirectsByTarget = std::multimap<std::string, const FFunctionRedirect*>;

FRedirectsByTarget CollectRedirects(const FResolvedType& Type, const FRedirectFunctionName& Owner,
                                    const FMetadataOptions& Options)
{
	FRedirectsByTarget Targets;
	for (const auto& Redirect : Options.FunctionRedirects)
	{
		if (MatchesOwner(Redirect.NewName, Owner))
		{
			Targets.emplace(Fold(Redirect.NewName.Function), &Redirect);
		}
	}
	std::set<std::string> Resolvable;
	for (const auto& Name : Type.MethodNames)
	{
		Resolvable.insert(Fold(Name));
	}
	std::vector<std::string> Pending(Resolvable.begin(), Resolvable.end());
	for (std::size_t Index = 0; Index < Pending.size(); ++Index)
	{
		const auto [Begin, End] = Targets.equal_range(Pending[Index]);
		for (auto It = Begin; It != End; ++It)
		{
			const auto& Redirect = *It->second;
			if (Redirect.bHasNewName && !Redirect.bPattern && MatchesOwner(Redirect.OldName, Owner))
			{
				const auto [Name, bNew] = Resolvable.insert(Fold(Redirect.OldName.Function));
				if (bNew)
				{
					Pending.push_back(*Name);
				}
			}
		}
	}
	for (const auto& [Name, Redirect] : Targets)
	{
		// An unqualified rule may belong to a different class.
		if (Redirect->NewName.Class.empty() || !MatchesOwner(Redirect->OldName, Owner) || Redirect->bPattern)
		{
			continue;
		}
		if (!Redirect->bHasNewName || !Resolvable.contains(Name))
		{
			FailRedirect(*Redirect, "target function is missing from " + Type.Identity.CppName + ".");
		}
	}
	return Targets;
}

void AddRedirectAliases(std::vector<FAPI>& Aliases, const FAPI& API, const FResolvedType& Type,
                        const FRedirectFunctionName& Owner, const FRedirectsByTarget& Targets)
{
	std::vector<std::string> Pending{Fold(API.Name)};
	std::set<std::string> Seen{Pending.front()};
	for (std::size_t Index = 0; Index < Pending.size(); ++Index)
	{
		const auto [Begin, End] = Targets.equal_range(Pending[Index]);
		for (auto It = Begin; It != End; ++It)
		{
			const auto& Redirect = *It->second;
			if (!API.bReflected && Redirect.NewName.Class.empty() && Redirect.NewName.Package.empty())
			{
				continue;
			}
			if (!Redirect.bHasNewName || Redirect.bPattern || !MatchesOwner(Redirect.OldName, Owner))
			{
				FailRedirect(Redirect, "export aliases require an exact function rename within the same class.");
			}
			if (!API.bReflected)
			{
				FailRedirect(
				    Redirect,
				    "target export " + Type.Identity.CppName + "::" + API.Name +
				        " requires UFUNCTION for redirect-based aliases. Use CompatibleNames for a native method.");
			}
			const auto& Name = Redirect.OldName.Function;
			if (!IsCompatibleFunctionName(Name))
			{
				FailRedirect(Redirect, "invalid source function name '" + Name + "'.");
			}
			std::string Key = Fold(Name);
			const auto Other = std::find_if(Targets.begin(), Targets.end(),
			                                [&](const auto& Target)
			                                {
				                                const auto& Candidate = *Target.second;
				                                return &Candidate != &Redirect && !Candidate.bPattern &&
				                                       MatchesOwner(Candidate.OldName, Owner) &&
				                                       Fold(Candidate.OldName.Function) == Key;
			                                });
			if (Other != Targets.end())
			{
				FailRedirect(Redirect,
				             "source function '" + Name + "' is also redirected at " + Other->second->Location + ".");
			}
			if (!Seen.insert(Key).second)
			{
				FailRedirect(Redirect, "repeated or cyclic compatible name '" + Name + "' for " +
				                           Type.Identity.CppName + "::" + API.Name + ".");
			}
			AddAlias(Aliases, API, Type, Name, Redirect.Location);
			Pending.push_back(std::move(Key));
		}
	}
}
} // namespace

void AssignExportNames(FGenerationModel& Model, const FMetadataOptions& Options)
{
	std::map<std::string, FRedirectsByTarget> TargetsByOwner;
	std::vector<FAPI> Aliases;
	for (auto& API : Model.APIs)
	{
		API.NameLocation = Location(API.Macro.File, API.Macro.Line, API.Macro.Column);
		API.ExportName = API.Name;
		if (API.bFullViewMember)
		{
			continue;
		}
		const auto& Type = Model.Types.at(API.Owner);
		SetName(API, Type, API.Name);
		const auto Names = std::find_if(API.Macro.Fields.begin(), API.Macro.Fields.end(),
		                                [](const auto& Field) { return Field.Name == "CompatibleNames"; });
		if (Names != API.Macro.Fields.end())
		{
			const auto Source = ReadTextFile(API.Macro.File);
			const Detail::FLexer Locations(API.Macro.File, Source);
			for (const auto& Name : Names->Value.Items)
			{
				const auto [Line, Column] = Locations.Location(Name.Offset);
				AddAlias(Aliases, API, Type, Name.Text, Location(API.Macro.File, Line, Column));
			}
			continue;
		}
		const auto Owner = ReflectedOwner(Type);
		const auto [Targets, bNewOwner] = TargetsByOwner.try_emplace(API.Owner);
		if (bNewOwner)
		{
			Targets->second = CollectRedirects(Type, Owner, Options);
		}
		AddRedirectAliases(Aliases, API, Type, Owner, Targets->second);
	}
	Model.APIs.insert(Model.APIs.end(), std::make_move_iterator(Aliases.begin()),
	                  std::make_move_iterator(Aliases.end()));
}
} // namespace Mod::HeaderTool
