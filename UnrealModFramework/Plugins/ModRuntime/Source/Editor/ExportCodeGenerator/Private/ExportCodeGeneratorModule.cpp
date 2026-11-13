// Copyright (c) 2026 Nelaric Contributors
#include "ModExportMacros.h"

#include "Dom/JsonObject.h"
#include "GameDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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
		FString Text;
		TSharedPtr<FJsonObject> Config;
		const TArray<TSharedPtr<FJsonValue>>* Types = nullptr;
		if (!FFileHelper::LoadFileToString(Text,
		                                   *FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("ModHeaderTool.json"))) ||
		    !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Config) || !Config ||
		    !Config->TryGetArrayField(TEXT("blueprintExports"), Types))
		{
			return;
		}
		for (const auto& Entry : *Types)
		{
			const TSharedPtr<FJsonObject>* Type = nullptr;
			FString Asset;
			if (Entry->TryGetObject(Type) && (*Type)->TryGetStringField(TEXT("asset"), Asset) &&
			    FPackageName::IsValidObjectPath(Asset))
			{
				Packages.AddUnique(FName(FPackageName::ObjectPathToPackageName(Asset)));
			}
		}
	}

	FDelegateHandle CookHandle;
};
} // namespace Mod::ExportCodeGenerator

IMPLEMENT_MODULE(Mod::ExportCodeGenerator::FExportCodeGeneratorModule, ExportCodeGenerator);
