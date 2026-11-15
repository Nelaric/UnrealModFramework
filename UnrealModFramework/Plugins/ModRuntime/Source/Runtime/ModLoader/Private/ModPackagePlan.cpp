// Copyright (c) 2026 Nelaric Contributors
#include "ModLoader.h"

namespace Mod::Loader
{
namespace
{
struct FPackageNode
{
	int32 Candidate = INDEX_NONE;
	int32 Incoming = 0;
	TArray<FString> Dependents;
};

bool IsSelected(const FMountedPackage& Package, const FPackageSelection& Selection)
{
	const FString* Digest = Selection.Archives.Find(Package.ModId);
	return (!Selection.Enabled || Selection.Enabled->Contains(Package.ModId)) &&
	       (!Digest || Package.ArchiveSha256.Equals(*Digest, ESearchCase::IgnoreCase));
}

bool MeetsService(const FMountedPackage& Provider, const FServiceDependency& Need)
{
	if (Provider.Extensions.SchemaVersion < 2)
	{
		// Legacy manifests declare services during World registration.
		return true;
	}
	return Provider.Extensions.Provides.ContainsByPredicate(
	    [&Need](const FServiceOffer& Offer)
	    {
		    return Offer.Id == Need.Id && Offer.TypeId == Need.TypeId && Offer.Signature == Need.Signature &&
		           Need.Versions.Contains(Offer.Version);
	    });
}
} // namespace

bool ResolvePackageOrder(TConstArrayView<const FMountedPackage*> Candidates, const FPackageSelection& Selection,
                         TArray<int32>& OutOrder, FString& OutError)
{
	OutOrder.Reset();
	OutError.Reset();
	TMap<FString, FPackageNode> Nodes;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const auto& Package = *Candidates[Index];
		if (!IsSelected(Package, Selection))
		{
			continue;
		}
		auto& Node = Nodes.FindOrAdd(Package.ModId);
		if (Node.Candidate != INDEX_NONE)
		{
			if (Candidates[Node.Candidate]->ArchiveSha256 != Package.ArchiveSha256)
			{
				OutError = Package.ModId + TEXT(": multiple eligible archives. Select an exact archive digest.");
				return false;
			}
			continue;
		}
		Node.Candidate = Index;
	}
	TArray<FString> Required = Selection.Required.Array();
	for (const auto& Archive : Selection.Archives)
	{
		if (!Selection.Enabled || Selection.Enabled->Contains(Archive.Key))
		{
			Required.AddUnique(Archive.Key);
		}
	}
	Required.Sort();
	for (const FString& ModId : Required)
	{
		if (!Nodes.Contains(ModId))
		{
			OutError = ModId + TEXT(": the required archive is unavailable or excluded.");
			return false;
		}
	}
	TArray<FString> Ids;
	Nodes.GetKeys(Ids);
	Ids.Sort();
	TMap<FName, FString> Providers;
	for (const FString& ModId : Ids)
	{
		auto& Node = Nodes.FindChecked(ModId);
		const auto& Package = *Candidates[Node.Candidate];
		for (const FString& Other : Package.Extensions.IncompatibleMods)
		{
			if (Nodes.Contains(Other))
			{
				OutError = ModId + TEXT(": incompatible with ") + Other;
				return false;
			}
		}
		for (const FServiceOffer& Offer : Package.Extensions.Provides)
		{
			if (const FString* Previous = Providers.Find(Offer.Id))
			{
				OutError = Offer.Id.ToString() + TEXT(": multiple providers: ") + *Previous + TEXT(", ") + ModId;
				return false;
			}
			Providers.Add(Offer.Id, ModId);
		}
		TSet<FString> Dependencies;
		for (const FModDependency& Need : Package.Extensions.Mods)
		{
			const auto* Provider = Nodes.Find(Need.ModId);
			if (Provider && Need.Versions.Contains(Candidates[Provider->Candidate]->Version))
			{
				Dependencies.Add(Need.ModId);
			}
			else if (Need.bRequired)
			{
				OutError = ModId + TEXT(": missing or incompatible dependency ") + Need.ModId;
				return false;
			}
		}
		for (const FServiceDependency& Need : Package.Extensions.Services)
		{
			const auto* Provider = Nodes.Find(Need.Provider);
			if (Provider && MeetsService(*Candidates[Provider->Candidate], Need))
			{
				Dependencies.Add(Need.Provider);
			}
			else if (Need.bRequired)
			{
				OutError = ModId + TEXT(": missing or incompatible service ") + Need.Id.ToString() + TEXT(" from ") +
				           Need.Provider;
				return false;
			}
		}
		Node.Incoming = Dependencies.Num();
		for (const FString& Dependency : Dependencies)
		{
			Nodes.FindChecked(Dependency).Dependents.Add(ModId);
		}
	}

	TArray<int32> Order;
	Order.Reserve(Ids.Num());
	while (Order.Num() < Ids.Num())
	{
		const FString* Next =
		    Ids.FindByPredicate([&Nodes](const FString& Id) { return Nodes.FindChecked(Id).Incoming == 0; });
		if (!Next)
		{
			TArray<FString> Unresolved;
			for (const FString& Id : Ids)
			{
				if (Nodes.FindChecked(Id).Incoming > 0)
				{
					Unresolved.Add(Id);
				}
			}
			OutError = TEXT("Package dependency cycle affects: ") + FString::Join(Unresolved, TEXT(", "));
			return false;
		}
		auto& Node = Nodes.FindChecked(*Next);
		Order.Add(Node.Candidate);
		Node.Incoming = -1;
		for (const FString& Dependent : Node.Dependents)
		{
			--Nodes.FindChecked(Dependent).Incoming;
		}
	}
	OutOrder = MoveTemp(Order);
	return true;
}
} // namespace Mod::Loader
