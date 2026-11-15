// Copyright (c) 2026 Nelaric Contributors
#include "ModAPIHostSubsystem.h"
#include "Engine/World.h"
#include "ModAPIHost.h"
#include "ModExtensionLibrary.h"
#include "ModExtensionSubsystem.h"
#include "ModInstance.h"
#include "ModLoader.h"
#include "Modules/ModuleManager.h"

namespace Mod::API
{
namespace
{
UModExtensionSubsystem* FindExtensions(const UModInstance& Mod)
{
	auto* World = Mod.GetWorld();
	return World ? World->GetSubsystem<UModExtensionSubsystem>() : nullptr;
}

UModAPIHostSubsystem* FindHost(const UModInstance& Mod)
{
	auto* Game = Mod.GetTypedOuter<UGameInstance>();
	return Game ? Game->GetSubsystem<UModAPIHostSubsystem>() : nullptr;
}
} // namespace

class FModAPIModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Mod::Loader::IModLoaderModule::Get().ValidateExtensions().BindLambda(
		    [](const Mod::Loader::FMountedPackage& Package, FString& OutError)
		    {
			    TArray<Mod::Extension::FDefinition> Definitions;
			    Mod::Extension::FGeneratedRegistration::DescribeAll(Definitions);
			    for (const auto& Requirement : Package.Extensions.Contracts)
			    {
				    const auto* Definition =
				        Definitions.FindByPredicate([&](const auto& Entry) { return Entry.Id == Requirement.Id; });
				    if (!Definition || Definition->Signature != Requirement.Signature)
				    {
					    OutError = Package.ModId + TEXT(": incompatible extension ") + Requirement.Id.ToString();
					    return false;
				    }
			    }
			    if (!Package.Extensions.Provides.IsEmpty())
			    {
				    FRegistry Registry;
				    auto Types = FGeneratedHostRegistration::RegisterAll(Registry);
				    if (!Types.IsSuccess())
				    {
					    OutError = TEXT("Could not register the host service catalog.");
					    return false;
				    }
				    for (const auto& Offer : Package.Extensions.Provides)
				    {
					    if (!Registry.HasServiceType(Offer.TypeId, *Offer.Signature))
					    {
						    OutError =
						        Package.ModId + TEXT(": incompatible World service type ") + Offer.TypeId.ToString();
						    return false;
					    }
				    }
			    }
			    return true;
		    });
		UModInstance::SetServiceBridge(
		    [](UModInstance& Mod, FName Id, FName TypeId, const TCHAR* Signature, uint64& Context, uint64& Object)
		    {
			    auto* Extensions = FindExtensions(Mod);
			    return Extensions && Extensions->AcquireNativeService(Mod, Id, TypeId, Signature, Context, Object);
		    });
		UModExtensionLibrary::SetBridge(
		    {[](UModInstance& Mod, FName Id, FName Key, FName Function, EModExtensionStage Stage,
		        EModExtensionOperation Operation, int32 Priority, const TArray<FString>& Before,
		        const TArray<FString>& After, float CancelValue, FModExtensionToken& Token)
		     {
			     auto* Extensions = FindExtensions(Mod);
			     if (!Extensions || Stage > EModExtensionStage::On || Operation > EModExtensionOperation::Replace)
			     {
				     return static_cast<int32>(EError::InvalidArgument);
			     }
			     using namespace Mod::Extension;
			     constexpr EOperation Operations[]{EOperation::None, EOperation::ModifyInputs, EOperation::Cancel,
			                                       EOperation::ModifyResult, EOperation::Replace};
			     const auto Result = Extensions->RegisterBlueprint(Mod, Id, Key, Function, static_cast<EStage>(Stage),
			                                                       Operations[static_cast<uint8>(Operation)],
			                                                       {Priority, Before, After}, CancelValue);
			     if (Result.IsSuccess())
			     {
				     const auto& Value = Result.GetValue();
				     Token.Id = Value.Id;
				     Token.Generation = Value.Generation;
				     Token.Serial = Value.Serial;
			     }
			     return static_cast<int32>(Result.GetError());
		     },
		     [](UModInstance& Mod, const FModExtensionToken& Token)
		     {
			     if (auto* Extensions = FindExtensions(Mod))
			     {
				     Extensions->Unregister(Mod, {Token.Id, Token.Generation, Token.Serial});
			     }
		     },
		     [](UModInstance& Mod, FName Id, FName TypeId, const FString& Signature, const FString& Version,
		        FName Permission, UObject* Instance)
		     {
			     auto* Extensions = FindExtensions(Mod);
			     return static_cast<int32>(
			         Extensions
			             ? Extensions->Provide(Mod, {Id, TypeId, Version, Signature, Permission}, Instance).GetError()
			             : EError::NotReady);
		     },
		     [](UModInstance& Mod, FName Id)
		     {
			     if (auto* Extensions = FindExtensions(Mod))
			     {
				     Extensions->Withdraw(Mod, Id);
			     }
		     }});
		UModInstance::SetHostAPIBridge(
		    [](UModInstance& Mod, FName TypeId, const TCHAR* Signature, uint64& Context, uint64& Object)
		    {
			    auto* Host = FindHost(Mod);
			    if (!Host)
			    {
				    return false;
			    }
			    const auto Result = Host->AuthorizeWorldCaller(Mod, TypeId, Signature);
			    if (!Result.IsSuccess())
			    {
				    return false;
			    }
			    Context = Result.GetValue().Native.Context;
			    Object = Result.GetValue().Object;
			    return true;
		    },
		    &UModAPIHostSubsystem::ResolveHostQuery,
		    [](UModInstance& Mod)
		    {
			    if (auto* Host = FindHost(Mod))
			    {
				    Host->RevokeModCallers(Mod);
			    }
		    });
	}

	virtual void ShutdownModule() override
	{
		if (auto* Loader = FModuleManager::GetModulePtr<Mod::Loader::IModLoaderModule>(TEXT("ModLoader")))
		{
			Loader->ValidateExtensions().Unbind();
		}
		UModInstance::SetServiceBridge(nullptr);
		UModExtensionLibrary::SetBridge({});
		UModInstance::SetHostAPIBridge(nullptr, nullptr, nullptr);
	}
};
} // namespace Mod::API

IMPLEMENT_MODULE(Mod::API::FModAPIModule, ModAPI);
