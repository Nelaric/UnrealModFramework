// Copyright (c) 2026 Nelaric Contributors
#include "BlueprintDiscovery.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "GameDelegates.h"
#include "ModBlueprintExportReader.h"
#include "Modules/ModuleManager.h"

namespace Mod::ExportCodeGenerator
{
class FExportCodeGeneratorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		CookHandle = FGameDelegates::Get().GetModifyCookDelegate().AddRaw(
		    this, &FExportCodeGeneratorModule::AddBlueprintPackagesToCook);
	}

	virtual void ShutdownModule() override
	{
		FGameDelegates::Get().GetModifyCookDelegate().Remove(CookHandle);
	}

private:
	void AddBlueprintPackagesToCook(TConstArrayView<const ITargetPlatform*>, TArray<FName>& Packages, TArray<FName>&)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		for (const FAssetData& Asset : FindExportCandidates(Registry))
		{
			BlueprintIntegration::FExportIdentity Identity;
			if (const auto* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
			    Blueprint && BlueprintIntegration::ReadExportIdentity(*Blueprint, Identity))
			{
				Packages.AddUnique(Asset.PackageName);
			}
		}
	}

	FDelegateHandle CookHandle;
};
} // namespace Mod::ExportCodeGenerator

IMPLEMENT_MODULE(Mod::ExportCodeGenerator::FExportCodeGeneratorModule, ExportCodeGenerator);
