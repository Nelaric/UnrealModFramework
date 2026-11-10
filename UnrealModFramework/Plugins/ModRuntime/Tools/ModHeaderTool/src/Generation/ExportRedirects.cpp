// Copyright (c) 2026 Nelaric Contributors
#include "ExportRedirects.h"

#include "ModHeaderTool/src/Generation/ExportNameRules.h"
#include "ModHeaderTool/src/Generation/TypeIdentity.h"
#include "ModHeaderTool/src/Metadata/MetadataName.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Mod::HeaderTool
{
using RedirectsByTarget = std::multimap<std::string, const FunctionRedirect*>;

bool ExportRedirects::MatchesOwner(const RedirectFunctionName& Name, const RedirectFunctionName& Owner)
{
	return (Name.Package.empty() || ExportNameRules::Fold(Name.Package) == Owner.Package) &&
	       (Name.Class.empty() || ExportNameRules::Fold(Name.Class) == Owner.Class);
}

RedirectFunctionName ExportRedirects::ReflectedOwner(const ResolvedType& Type)
{
	std::string Class = Type.Identity.CppName;
	if (const auto Separator = Class.rfind("::"); Separator != Class.npos)
	{
		Class.erase(0, Separator + 2);
	}
	// UObject names in Core Redirects omit the UHT class prefix.
	return {ExportNameRules::Fold("/Script/" + Type.Module), ExportNameRules::Fold(Class.substr(1)), {}};
}

[[noreturn]] void ExportRedirects::FailRedirect(const FunctionRedirect& Redirect, const std::string& Reason)
{
	throw std::runtime_error(Redirect.Location + ": error: [CoreRedirects] FunctionRedirects=" + Redirect.Entry + ": " +
	                         Reason);
}

RedirectsByTarget ExportRedirects::CollectRedirects(const ResolvedType& Type, const RedirectFunctionName& Owner,
                                                    const MetadataOptions& Options)
{
	RedirectsByTarget Targets;
	for (const auto& Redirect : Options.FunctionRedirects)
	{
		if (MatchesOwner(Redirect.NewName, Owner))
		{
			Targets.emplace(ExportNameRules::Fold(Redirect.NewName.Function), &Redirect);
		}
	}
	std::set<std::string> Resolvable;
	for (const auto& Name : Type.MethodNames)
	{
		Resolvable.insert(ExportNameRules::Fold(Name));
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
				const auto [Name, bNew] = Resolvable.insert(ExportNameRules::Fold(Redirect.OldName.Function));
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

void ExportRedirects::AddRedirectAliases(std::vector<API>& Aliases, const API& API, const ResolvedType& Type,
                                         const RedirectFunctionName& Owner, const RedirectsByTarget& Targets)
{
	std::vector<std::string> Pending{ExportNameRules::Fold(API.Name)};
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
			if (!MetadataName::IsCompatibleFunctionName(Name))
			{
				FailRedirect(Redirect, "invalid source function name '" + Name + "'.");
			}
			std::string Key = ExportNameRules::Fold(Name);
			const auto Other = std::find_if(Targets.begin(), Targets.end(),
			                                [&](const auto& Target)
			                                {
				                                const auto& Candidate = *Target.second;
				                                return &Candidate != &Redirect && !Candidate.bPattern &&
				                                       MatchesOwner(Candidate.OldName, Owner) &&
				                                       ExportNameRules::Fold(Candidate.OldName.Function) == Key;
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
			ExportNameRules::AddAlias(Aliases, API, Type, Name, Redirect.Location);
			Pending.push_back(std::move(Key));
		}
	}
}
} // namespace Mod::HeaderTool
