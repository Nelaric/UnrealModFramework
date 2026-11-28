// Copyright (c) 2026 Nelaric Contributors
#include "SharedCallerAssets.h"

#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

int32 USharedCallerAssetsCommandlet::Main(const FString&)
{
	const TCHAR* PackageName = TEXT("/Game/BP_SharedCaller");
	UClass* Parent = LoadClass<UObject>(nullptr, TEXT("/Script/SharedCaller.SharedCallerInstance"));
	if (!Parent)
	{
		UE_LOG(LogTemp, Error, TEXT("Could not resolve the SharedCaller lifecycle class."));
		return 1;
	}

	UPackage* Package = CreatePackage(PackageName);
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Package, TEXT("BP_SharedCaller"),
	                                                                BPTYPE_Normal, TEXT("SharedCallerAssets"));
	if (Blueprint)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status != BS_Error && Blueprint->GeneratedClass)
		{
			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			const FString File =
			    FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
			if (UPackage::SavePackage(Package, Blueprint, *File, Args))
			{
				return 0;
			}
		}
	}
	UE_LOG(LogTemp, Error, TEXT("Could not compile and save the SharedCaller lifecycle Blueprint."));
	return 1;
}

IMPLEMENT_MODULE(FDefaultModuleImpl, SharedCallerEditor);
