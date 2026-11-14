// Copyright (c) 2026 Nelaric Contributors
/** @file ModExtensionLibrary.h */
#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "ModExtensionLibrary.generated.h"

class UModInstance;

/// A game-thread callback stage in the World extension bus.
UENUM(BlueprintType)
enum class EModExtensionStage : uint8
{
	/// Before original execution.
	Pre,
	/// After successful execution.
	Post,
	/// A read-only notification.
	Event,
};

/// A requested modification of one generated Hook value.
UENUM(BlueprintType)
enum class EModExtensionOperation : uint8
{
	/// Observation only.
	Observe,
	/// Replaces the next input.
	ModifyInputs,
	/// Returns a cancellation result.
	Cancel,
	/// Replaces the returned value.
	ModifyResult,
};

/// Identifies a registration owned by one mod and World.
USTRUCT(BlueprintType)
struct FModExtensionToken
{
	GENERATED_BODY()

	UPROPERTY()
	FName Id;
	UPROPERTY()
	uint64 Generation = 0;
	UPROPERTY()
	uint64 Serial = 0;
};

/// Registers generated extension callbacks for cooked Mod Blueprints.
UCLASS(MinimalAPI)
class UModExtensionLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** @brief Registers a synchronous function on the game thread.
	 * @param Owner The host-created Mod instance.
	 * @param Id Stable generated Hook or Event ID.
	 * @param Key Stable identifier within the owner and stage.
	 * @param Function Name of the matching Blueprint function.
	 * @param Stage Pre, Post, or Event.
	 * @param Operation The requested change or observation.
	 * @param Priority Lower values run first when unconstrained.
	 * @param Before Mod IDs this callback precedes.
	 * @param After Mod IDs that precede this callback.
	 * @param CancelValue The complete result on cancellation.
	 * @param Token Receives the subscription token on success.
	 * @return Zero on success or a runtime extension error code.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mod|Extension")
	static MODLOADER_API int32 RegisterModExtension(UModInstance* Owner, FName Id, FName Key, FName Function,
	                                                EModExtensionStage Stage, EModExtensionOperation Operation,
	                                                int32 Priority, const TArray<FString>& Before,
	                                                const TArray<FString>& After, float CancelValue,
	                                                FModExtensionToken& Token);

	/** @brief Requests unsubscription on the game thread.
	 * @param Owner The Mod that owns the subscription.
	 * @param Token The token returned by registration.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mod|Extension")
	static MODLOADER_API void UnregisterModExtension(UModInstance* Owner, const FModExtensionToken& Token);

	/** @brief Provides a named World service during On Mod Register.
	 * @param Owner The host-created instance that owns the provider.
	 * @param Id The service ID declared by consuming packages.
	 * @param TypeId The generated World interface ID.
	 * @param Signature The interface's generated service signature.
	 * @param Version The provided service version in major.minor.patch form.
	 * @param Permission Host grant for the provider and consumers, or None.
	 * @param Instance The service object implementing the generated interface.
	 * @return Zero on success or a runtime extension error code.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mod|Service")
	static MODLOADER_API int32 ProvideModService(UModInstance* Owner, FName Id, FName TypeId, const FString& Signature,
	                                             const FString& Version, FName Permission, UObject* Instance);

	/// Revokes the named service when it is owned by this Mod.
	UFUNCTION(BlueprintCallable, Category = "Mod|Service")
	static MODLOADER_API void WithdrawModService(UModInstance* Owner, FName Id);

	/** @brief Acquires a declared service for generated SDK calls.
	 * @param Owner The current Mod instance.
	 * @param Id The service ID declared by the consuming package.
	 * @param TypeId The generated World interface ID.
	 * @param Signature The generated calling table signature.
	 * @param Context Receives the owner-bound context token.
	 * @param Object Receives the revocable service object token.
	 * @return Whether the service is available.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mod|Extension")
	static MODLOADER_API bool AcquireModService(UModInstance* Owner, FName Id, FName TypeId, const FString& Signature,
	                                            int64& Context, int64& Object);

public:
	struct FBridge
	{
		int32 (*Register)(UModInstance&, FName, FName, FName, EModExtensionStage, EModExtensionOperation, int32,
		                  const TArray<FString>&, const TArray<FString>&, float, FModExtensionToken&);
		void (*Unregister)(UModInstance&, const FModExtensionToken&);
		int32 (*Provide)(UModInstance&, FName, FName, const FString&, const FString&, FName, UObject*);
		void (*Withdraw)(UModInstance&, FName);
	};
	static MODLOADER_API void SetBridge(FBridge Bridge);
};
