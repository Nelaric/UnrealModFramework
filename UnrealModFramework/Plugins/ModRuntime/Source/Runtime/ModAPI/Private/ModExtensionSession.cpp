// Copyright (c) 2026 Nelaric Contributors
#include "ModExtensionSession.h"

#include "Engine/NetConnection.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "ModAPIHost.h"
#include "ModExtensionWorld.h"
#include "Net/DataBunch.h"
#include "Net/DataChannel.h"
#include "Misc/NetworkVersion.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"

namespace Mod::Extension
{
namespace
{
constexpr uint8 PlanMessage = 0xB6;
constexpr uint8 AcknowledgmentMessage = 0xB7;
constexpr int32 Protocol = 1;
constexpr int32 DigestLength = 64;
constexpr TCHAR LoginKey[] = TEXT("UMF");

bool IsDigest(const FString& Value)
{
	if (Value.Len() != DigestLength)
	{
		return false;
	}
	for (const TCHAR Ch : Value)
	{
		if (!FChar::IsHexDigit(Ch) || (Ch >= TEXT('A') && Ch <= TEXT('F')))
		{
			return false;
		}
	}
	return true;
}
} // namespace

FString FModNetworkHandshake::GetContractDigest()
{
	if (!IsInGameThread())
	{
		return {};
	}
	TArray<FDefinition> Definitions;
	FGeneratedRegistration::DescribeAll(Definitions);
	Definitions.Sort([](const auto& A, const auto& B) { return A.Id.LexicalLess(B.Id); });
	TArray<FString> Digests;
	Mod::API::FGeneratedHostRegistration::DescribeContracts(Digests);
	Digests.Sort();
	FString Data = FString::Join(Digests, TEXT("\n")) + TEXT("\n");
	for (const auto& Definition : Definitions)
	{
		Data += Definition.Id.ToString().ToLower() + TEXT("|") + Definition.Signature + TEXT("\n");
	}
	return Mod::Loader::HashPackagePlan(Data);
}

static FString GetReleaseDigest(TConstArrayView<const Mod::Loader::FMountedPackage*> Packages)
{
	FString GameId, BaseRelease, BaseRegistry, NetworkBase;
	if (GConfig)
	{
		GConfig->GetString(TEXT("ModLoader"), TEXT("GameId"), GameId, GGameIni);
		GConfig->GetString(TEXT("ModLoader"), TEXT("BaseReleaseVersion"), BaseRelease, GGameIni);
		GConfig->GetString(TEXT("ModLoader"), TEXT("BaseRegistrySha256"), BaseRegistry, GGameIni);
		GConfig->GetString(TEXT("ModLoader"), TEXT("NetworkBaseCompatibilitySha256"), NetworkBase, GGameIni);
	}
	const bool bUseTargetRegistry = NetworkBase.IsEmpty();
	NetworkBase.ToLowerInline();
	if (!bUseTargetRegistry && !IsDigest(NetworkBase))
	{
		return {};
	}
	const FString Contract = FModNetworkHandshake::GetContractDigest();
	FString Identity = GameId + TEXT("|") + BaseRelease + TEXT("|") +
	                   (bUseTargetRegistry ? BaseRegistry.ToLower() : NetworkBase) + TEXT("|") + Contract + TEXT("\n");
	for (const auto* Item : Packages)
	{
		const auto& Package = *Item;
		Identity += Package.ModId + TEXT("|") + Package.Version + TEXT("|") + Package.GameplayRelease.ReleaseId +
		            TEXT("|") + Package.GameplayRelease.CompatibilityDigest + TEXT("\n");
	}
	return Mod::Loader::HashPackagePlan(Identity);
}

FString FModNetworkHandshake::MakeLoginOption(const UModExtensionSubsystem* Extensions)
{
	if (!IsInGameThread())
	{
		return {};
	}
	TArray<Mod::Loader::FMountedPackage> Mounted;
	TArray<const Mod::Loader::FMountedPackage*> Selected;
	if (Extensions && Extensions->IsReady())
	{
		for (const auto& Mod : Extensions->State->Mods)
		{
			Selected.Add(Mod.Package.Get());
		}
	}
	else
	{
		using namespace Mod::Loader;
		auto& Loader = IModLoaderModule::Get();
		if (Loader.IsRestartRequired())
		{
			return {};
		}
		Mounted = Loader.GetMountedPackages();
		TArray<const FMountedPackage*> Candidates;
		for (const auto& Package : Mounted)
		{
			Candidates.Add(&Package);
		}
		TArray<int32> Order;
		FString Error;
		if (Candidates.Num() > 128 || !ResolvePackageOrder(Candidates, {}, Order, Error))
		{
			return {};
		}
		for (int32 Index : Order)
		{
			Selected.Add(Candidates[Index]);
		}
	}
	const FString Digest = GetReleaseDigest(Selected);
	return Digest.IsEmpty() ? FString()
	                        : FString::Printf(TEXT("%s=%d.%u.%s"), LoginKey, Protocol,
	                                          FNetworkVersion::GetLocalNetworkVersion(), *Digest);
}

bool FModNetworkHandshake::VerifyLogin(const UModExtensionSubsystem& Extensions, const FString& Options, FString& Error)
{
	if (!Extensions.IsReady())
	{
		Error = TEXT("UMF_WORLD_NOT_READY");
		return false;
	}
	const FString Hello = UGameplayStatics::ParseOption(Options, LoginKey);
	TArray<FString> Parts;
	if (Hello.Len() > 96 || Hello.ParseIntoArray(Parts, TEXT("."), false) != 3 || Parts[0] != LexToString(Protocol) ||
	    !IsDigest(Parts[2]))
	{
		Error = TEXT("UMF_HANDSHAKE_REQUIRED");
		return false;
	}
	uint32 LocalVersion = 0;
	if (!LexTryParseString(LocalVersion, *Parts[1]) ||
	    !FNetworkVersion::IsNetworkCompatible(FNetworkVersion::GetLocalNetworkVersion(), LocalVersion))
	{
		Error = TEXT("UMF_NETWORK_VERSION_MISMATCH");
		return false;
	}
	TArray<const Mod::Loader::FMountedPackage*> Selected;
	for (const auto& Mod : Extensions.State->Mods)
	{
		Selected.Add(Mod.Package.Get());
	}
	const FString ServerDigest = GetReleaseDigest(Selected);
	if (ServerDigest.IsEmpty())
	{
		Error = TEXT("UMF_INVALID_SERVER_BASE_IDENTITY");
		return false;
	}
	if (Parts[2] != ServerDigest)
	{
		Error = TEXT("UMF_GAMEPLAY_RELEASES_MISMATCH");
		return false;
	}
	return true;
}

bool FModNetworkHandshake::SendPlan(const UModExtensionSubsystem& Extensions, UNetConnection& Connection)
{
	if (!Extensions.IsReady() || !Extensions.GetWorld() || Extensions.GetWorld()->GetNetMode() == NM_Client)
	{
		return false;
	}
	const FString Map = UWorld::RemovePIEPrefix(Extensions.GetWorld()->GetOutermost()->GetName());
	FString Message = FString::Printf(TEXT("%d|%s|%s"), Protocol, *Map, *Extensions.GetGameplayDigest());
	uint8 Kind = PlanMessage;
	FNetControlMessage<NMT_GameSpecific>::Send(&Connection, Kind, Message);
	return true;
}

bool FModNetworkHandshake::ReceivePlan(UNetConnection& Connection, uint8 Message, const FString& Value, FString& Error)
{
	if (!IsInGameThread() || Message != PlanMessage)
	{
		return false;
	}
	TArray<FString> Parts;
	if (Value.Len() > 384 || Value.ParseIntoArray(Parts, TEXT("|"), false) != 3 || Parts[0] != LexToString(Protocol) ||
	    !FPackageName::IsValidLongPackageName(Parts[1]) || !IsDigest(Parts[2]))
	{
		Error = TEXT("UMF_INVALID_PLAN");
		return true;
	}
	if (Peer.Get() == &Connection && (ExpectedMap != Parts[1] || ExpectedDigest != Parts[2]))
	{
		Error = TEXT("UMF_CONFLICTING_PLAN");
		return true;
	}
	Peer = &Connection;
	ExpectedMap = MoveTemp(Parts[1]);
	ExpectedDigest = MoveTemp(Parts[2]);
	return true;
}

bool FModNetworkHandshake::GetExpectedPlan(const UNetConnection& Connection, const FURL& URL, FString& Digest,
                                           FString& Error) const
{
	if (!IsInGameThread() || Peer.Get() != &Connection)
	{
		Error = TEXT("UMF_SERVER_PLAN_REQUIRED");
		return false;
	}
	if (ExpectedMap != UWorld::RemovePIEPrefix(URL.Map))
	{
		Error = TEXT("UMF_SERVER_MAP_MISMATCH");
		return false;
	}
	Digest = ExpectedDigest;
	return true;
}

bool FModNetworkHandshake::ConfirmPlan(const UModExtensionSubsystem& Extensions, UNetConnection& Connection,
                                       const FString& Digest)
{
	if (!IsInGameThread() || !IsDigest(Digest) || !Extensions.MatchesGameplayDigest(Digest))
	{
		return false;
	}
	FString Value = Digest;
	uint8 Kind = AcknowledgmentMessage;
	FNetControlMessage<NMT_GameSpecific>::Send(&Connection, Kind, Value);
	return true;
}

bool FModNetworkHandshake::ReceiveAcknowledgment(const UModExtensionSubsystem& Extensions, uint8 Message,
                                                 const FString& Value, bool& bAccepted)
{
	if (Message != AcknowledgmentMessage)
	{
		return false;
	}
	bAccepted = IsInGameThread() && IsDigest(Value) && Extensions.MatchesGameplayDigest(Value);
	return true;
}

void FModNetworkHandshake::Reset()
{
	Peer.Reset();
	ExpectedMap.Reset();
	ExpectedDigest.Reset();
}
} // namespace Mod::Extension
