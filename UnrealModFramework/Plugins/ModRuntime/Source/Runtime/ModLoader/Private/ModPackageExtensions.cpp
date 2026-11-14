// Copyright (c) 2026 Nelaric Contributors
#include "ModPackageExtensions.h"

#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "ModPackageArchive.h"

namespace Mod::Loader
{
namespace
{
bool ParseVersion(const FString& Text, TArray<uint32, TInlineAllocator<3>>& Out)
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT("."), false);
	if (Parts.Num() != 3)
	{
		return false;
	}
	for (const FString& Part : Parts)
	{
		if (Part.IsEmpty() || Part.Len() > 9 || (Part.Len() > 1 && Part[0] == TEXT('0')))
		{
			return false;
		}
		uint32 Number = 0;
		for (TCHAR Character : Part)
		{
			if (Character < TEXT('0') || Character > TEXT('9'))
			{
				return false;
			}
			Number = Number * 10 + (Character - TEXT('0'));
		}
		Out.Add(Number);
	}
	return true;
}

TOptional<int32> CompareVersions(const FString& Left, const FString& Right)
{
	TArray<uint32, TInlineAllocator<3>> A, B;
	if (!ParseVersion(Left, A) || !ParseVersion(Right, B))
	{
		return {};
	}
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		if (A[Index] != B[Index])
		{
			return A[Index] < B[Index] ? -1 : 1;
		}
	}
	return 0;
}

bool IsId(const FString& Text, bool bModId = false)
{
	if (Text.IsEmpty() || Text.Len() > 128 || Text.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		return false;
	}
	for (TCHAR Character : Text)
	{
		if ((Character >= TEXT('a') && Character <= TEXT('z')) || (Character >= TEXT('0') && Character <= TEXT('9')) ||
		    Character == TEXT('_') ||
		    (!bModId &&
		     ((Character >= TEXT('A') && Character <= TEXT('Z')) || Character == TEXT('.') || Character == TEXT('-'))))
		{
			continue;
		}
		return false;
	}
	return true;
}

bool Signature(const FJsonObject& Json, FString& Out)
{
	if (!Json.TryGetStringField(TEXT("signature"), Out) || Out.Len() != 64)
	{
		return false;
	}
	for (const TCHAR Character : Out)
	{
		if (!(Character >= TEXT('0') && Character <= TEXT('9')) && !(Character >= TEXT('a') && Character <= TEXT('f')))
		{
			return false;
		}
	}
	return true;
}

bool Name(const FJsonObject& Json, const TCHAR* Field, FName& Out)
{
	FString Value;
	if (!Json.TryGetStringField(Field, Value) || !IsId(Value))
	{
		return false;
	}
	Out = FName(Value);
	return true;
}

bool ReadRange(const FJsonObject& Json, FVersionRange& Range)
{
	if ((Json.HasField(TEXT("minimum")) && !Json.TryGetStringField(TEXT("minimum"), Range.Minimum)) ||
	    (Json.HasField(TEXT("before")) && !Json.TryGetStringField(TEXT("before"), Range.Before)))
	{
		return false;
	}
	return (Range.Minimum.IsEmpty() || CompareVersions(Range.Minimum, Range.Minimum).IsSet()) &&
	       (Range.Before.IsEmpty() || CompareVersions(Range.Before, Range.Before).IsSet()) &&
	       (Range.Minimum.IsEmpty() || Range.Before.IsEmpty() ||
	        CompareVersions(Range.Minimum, Range.Before).Get(0) < 0);
}

bool Required(const FJsonObject& Json, bool& Value)
{
	return !Json.HasField(TEXT("required")) || Json.TryGetBoolField(TEXT("required"), Value);
}

bool Asset(const FJsonObject& Json, const TCHAR* Field, const FString& Root, FString& Value)
{
	return Json.TryGetStringField(Field, Value) && Value.Len() <= 512 &&
	       Value.StartsWith(Root, ESearchCase::CaseSensitive) && FPackageName::IsValidObjectPath(Value) &&
	       !Value.Contains(TEXT(":"));
}

template <class FRead> bool ReadArray(const FJsonObject& Json, const TCHAR* Field, FRead Read)
{
	if (!Json.HasField(Field))
	{
		return true;
	}
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Json.TryGetArrayField(Field, Values) || Values->Num() > 256)
	{
		return false;
	}
	for (const auto& Value : *Values)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value->TryGetObject(Object) || !Object->IsValid() || !Read(**Object))
		{
			return false;
		}
	}
	return true;
}

bool ReadNames(const FJsonObject& Json, const TCHAR* Field, TArray<FName>& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Json.HasField(Field))
	{
		return true;
	}
	if (!Json.TryGetArrayField(Field, Values) || Values->Num() > 32)
	{
		return false;
	}
	for (const auto& Value : *Values)
	{
		FString Id;
		if (!Value->TryGetString(Id) || !IsId(Id))
		{
			return false;
		}
		const FName Key(Id);
		if (Out.Contains(Key))
		{
			return false;
		}
		Out.Add(Key);
	}
	return true;
}
} // namespace

bool FVersionRange::Contains(const FString& Version) const
{
	return CompareVersions(Version, Version).IsSet() &&
	       (Minimum.IsEmpty() || CompareVersions(Version, Minimum).Get(-1) >= 0) &&
	       (Before.IsEmpty() || CompareVersions(Version, Before).Get(0) < 0);
}

bool Private::ReadExtensions(const FJsonObject& Manifest, FLoadResult& Result)
{
	FMountedPackage& Package = Result.Package;
	const TSharedPtr<FJsonObject>* Extensions = nullptr;
	double Version = 0;
	if (!Manifest.TryGetObjectField(TEXT("extensions"), Extensions) || !Extensions->IsValid() ||
	    !(*Extensions)->TryGetNumberField(TEXT("schemaVersion"), Version) || (Version != 1 && Version != 2))
	{
		return Fail(Result, ELoadError::InvalidArchive, TEXT("Unsupported package extensions schema."));
	}
	const bool bOffers = Version == 2;
	for (const auto& Field : (*Extensions)->Values)
	{
		if (Field.Key != TEXT("schemaVersion") && Field.Key != TEXT("dependencies") && Field.Key != TEXT("services") &&
		    Field.Key != TEXT("content") && Field.Key != TEXT("patches") && Field.Key != TEXT("contracts") &&
		    !(bOffers && (Field.Key == TEXT("provides") || Field.Key == TEXT("incompatible") ||
		                  (Field.Key == TEXT("requiredGrants") || Field.Key == TEXT("nativeExtension")))))
		{
			return Fail(Result, ELoadError::InvalidArchive, TEXT("Unknown package extension field: ") + Field.Key);
		}
	}
	auto& Out = Package.Extensions;
	Out.bDeclared = true;
	Out.SchemaVersion = static_cast<int32>(Version);
	if (bOffers)
	{
		if ((*Extensions)->HasField(TEXT("nativeExtension")) &&
		    (!(*Extensions)->TryGetStringField(TEXT("nativeExtension"), Out.NativeExtensionFile) ||
		     !IsId(Out.NativeExtensionFile) || !Out.NativeExtensionFile.EndsWith(TEXT(".dll")) ||
		     Out.NativeExtensionFile.Contains(TEXT("/")) || Out.NativeExtensionFile.Contains(TEXT(".."))))
		{
			return Fail(Result, ELoadError::InvalidArchive, TEXT("Invalid independent extension DLL name."));
		}
		if (!ReadArray(
		        **Extensions, TEXT("provides"),
		        [&Out](const FJsonObject& Json)
		        {
			        FServiceOffer Entry;
			        if (!Name(Json, TEXT("id"), Entry.Id) || !Name(Json, TEXT("typeId"), Entry.TypeId) ||
			            !Signature(Json, Entry.Signature) || !Json.TryGetStringField(TEXT("version"), Entry.Version) ||
			            !FVersionRange{}.Contains(Entry.Version) ||
			            (Json.HasField(TEXT("permission")) && !Name(Json, TEXT("permission"), Entry.Permission)) ||
			            (Json.HasField(TEXT("patch")) && !Name(Json, TEXT("patch"), Entry.Patch)) ||
			            Out.Provides.ContainsByPredicate([&Entry](const auto& Offer) { return Offer.Id == Entry.Id; }))
			        {
				        return false;
			        }
			        Out.Provides.Add(MoveTemp(Entry));
			        return true;
		        }) ||
		    !ReadArray(**Extensions, TEXT("incompatible"),
		               [&](const FJsonObject& Json)
		               {
			               FString Id;
			               if (!Json.TryGetStringField(TEXT("modId"), Id) || !IsId(Id, true) || Id == Package.ModId ||
			                   Out.IncompatibleMods.Contains(Id))
			               {
				               return false;
			               }
			               Out.IncompatibleMods.Add(MoveTemp(Id));
			               return true;
		               }) ||
		    !ReadArray(**Extensions, TEXT("requiredGrants"),
		               [&](const FJsonObject& Json)
		               {
			               FName Grant;
			               if (!Name(Json, TEXT("id"), Grant) || Out.RequiredGrants.Contains(Grant))
			               {
				               return false;
			               }
			               Out.RequiredGrants.Add(Grant);
			               return true;
		               }))
		{
			return Fail(Result, ELoadError::InvalidArchive, TEXT("Invalid extension service offer or admission rule."));
		}
	}
	TSet<FString> Mods;
	TSet<FName> Services, Patches, Contracts;
	TSet<TPair<FName, FName>> Content;
	if (!ReadArray(**Extensions, TEXT("dependencies"),
	               [&](const FJsonObject& Json)
	               {
		               auto& Entry = Out.Mods.AddDefaulted_GetRef();
		               if (!Json.TryGetStringField(TEXT("modId"), Entry.ModId) || !IsId(Entry.ModId, true) ||
		                   Entry.ModId == Package.ModId || Mods.Contains(Entry.ModId) ||
		                   !ReadRange(Json, Entry.Versions) || !Required(Json, Entry.bRequired))
		               {
			               return false;
		               }
		               Mods.Add(Entry.ModId);
		               return true;
	               }) ||
	    !ReadArray(**Extensions, TEXT("services"),
	               [&](const FJsonObject& Json)
	               {
		               auto& Entry = Out.Services.AddDefaulted_GetRef();
		               if (!Name(Json, TEXT("id"), Entry.Id) || Services.Contains(Entry.Id) ||
		                   !Json.TryGetStringField(TEXT("provider"), Entry.Provider) || !IsId(Entry.Provider, true) ||
		                   Entry.Provider == Package.ModId || !Name(Json, TEXT("typeId"), Entry.TypeId) ||
		                   !Signature(Json, Entry.Signature) || !ReadRange(Json, Entry.Versions) ||
		                   !Required(Json, Entry.bRequired))
		               {
			               return false;
		               }
		               Services.Add(Entry.Id);
		               return true;
	               }) ||
	    !ReadArray(**Extensions, TEXT("contracts"),
	               [&](const FJsonObject& Json)
	               {
		               auto& Entry = Out.Contracts.AddDefaulted_GetRef();
		               FString Id;
		               if (!Json.TryGetStringField(TEXT("id"), Id) || Id.Len() > 128 ||
		                   (!Id.StartsWith(TEXT("hook/"), ESearchCase::CaseSensitive) &&
		                    !Id.StartsWith(TEXT("event/"), ESearchCase::CaseSensitive)) ||
		                   !IsId(Id.RightChop(Id.Find(TEXT("/")) + 1).Replace(TEXT("/"), TEXT("."))) ||
		                   Id != Id.ToLower() || !Signature(Json, Entry.Signature))
		               {
			               return false;
		               }
		               Entry.Id = FName(Id);
		               if (Contracts.Contains(Entry.Id))
		               {
			               return false;
		               }
		               Contracts.Add(Entry.Id);
		               return true;
	               }) ||
	    !ReadArray(**Extensions, TEXT("content"),
	               [&](const FJsonObject& Json)
	               {
		               auto& Entry = Out.Content.AddDefaulted_GetRef();
		               FString Action, Path;
		               if (!Name(Json, TEXT("slot"), Entry.Slot) || !Name(Json, TEXT("id"), Entry.Id) ||
		                   Content.Contains({Entry.Slot, Entry.Id}) || !Json.TryGetStringField(TEXT("action"), Action))
		               {
			               return false;
		               }
		               Content.Add({Entry.Slot, Entry.Id});
		               double Priority = 0;
		               if (Json.HasField(TEXT("priority")) &&
		                   (!Json.TryGetNumberField(TEXT("priority"), Priority) || !FMath::IsFinite(Priority) ||
		                    Priority < MIN_int32 || Priority > MAX_int32 || Priority != FMath::TruncToDouble(Priority)))
		               {
			               return false;
		               }
		               Entry.Priority = static_cast<int32>(Priority);
		               if (Action == TEXT("disable"))
		               {
			               Entry.Action = EContentAction::Disable;
			               return !Json.HasField(TEXT("asset"));
		               }
		               if ((Action != TEXT("provide") && Action != TEXT("replace")) ||
		                   !Asset(Json, TEXT("asset"), Package.PackageRoot, Path))
		               {
			               return false;
		               }
		               Entry.Action = Action == TEXT("provide") ? EContentAction::Provide : EContentAction::Replace;
		               Entry.Asset = FSoftObjectPath(Path);
		               return true;
	               }) ||
	    !ReadArray(
	        **Extensions, TEXT("patches"),
	        [&](const FJsonObject& Json)
	        {
		        auto& Entry = Out.Patches.AddDefaulted_GetRef();
		        double WaitSeconds = 0;
		        const bool bActor = Json.HasField(TEXT("actorClass"));
		        const bool bComponent = Json.HasField(TEXT("componentClass"));
		        const bool bData = Json.HasField(TEXT("dataAsset"));
		        if (!Name(Json, TEXT("id"), Entry.Id) || Patches.Contains(Entry.Id) ||
		            !Name(Json, TEXT("anchor"), Entry.Anchor) ||
		            (Json.HasField(TEXT("conflictKey")) && !Name(Json, TEXT("conflictKey"), Entry.ConflictKey)) ||
		            static_cast<int>(bActor) + static_cast<int>(bComponent) + static_cast<int>(bData) != 1 ||
		            (!bOffers && !bActor) || (Json.HasField(TEXT("dataId")) != bData) ||
		            !Required(Json, Entry.bRequired) || !ReadNames(Json, TEXT("after"), Entry.After) ||
		            Entry.After.Contains(Entry.Id) ||
		            (bOffers && !ReadNames(Json, TEXT("requiresServices"), Entry.RequiresServices)) ||
		            (Json.HasField(TEXT("waitSeconds")) &&
		             (!bOffers || Entry.bRequired || !Json.TryGetNumberField(TEXT("waitSeconds"), WaitSeconds) ||
		              !FMath::IsFinite(WaitSeconds) || WaitSeconds <= 0 || WaitSeconds > 3600)))
		        {
			        return false;
		        }
		        Patches.Add(Entry.Id);
		        FString Path;
		        if (bData)
		        {
			        if (!Name(Json, TEXT("dataId"), Entry.DataId) ||
			            !Asset(Json, TEXT("dataAsset"), Package.PackageRoot, Path))
			        {
				        return false;
			        }
			        Entry.Operation = EWorldPatchOperation::RegisterData;
			        Entry.DataAsset = FSoftObjectPath(Path);
		        }
		        else
		        {
			        if (!Asset(Json, bActor ? TEXT("actorClass") : TEXT("componentClass"), Package.PackageRoot, Path) ||
			            !Path.EndsWith(TEXT("_C")))
			        {
				        return false;
			        }
			        if (bActor)
			        {
				        Entry.ActorClass = FSoftClassPath(Path);
			        }
			        else
			        {
				        Entry.Operation = EWorldPatchOperation::AttachComponent;
				        Entry.ComponentClass = FSoftClassPath(Path);
			        }
		        }
		        Entry.WaitSeconds = static_cast<float>(WaitSeconds);
		        return true;
	        }))
	{
		return Fail(Result, ELoadError::InvalidArchive, TEXT("Invalid dependency, content, service or World Patch."));
	}
	for (const FServiceOffer& Offer : Out.Provides)
	{
		if (Offer.Patch.IsNone())
		{
			continue;
		}
		const auto* Patch =
		    Out.Patches.FindByPredicate([&Offer](const FWorldPatch& Entry) { return Entry.Id == Offer.Patch; });
		if (!Patch || Patch->Operation != EWorldPatchOperation::SpawnActor)
		{
			return Fail(Result, ELoadError::InvalidArchive, TEXT("A service offer requires an Actor-spawning Patch."));
		}
	}
	return true;
}
} // namespace Mod::Loader
