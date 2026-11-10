// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "GameFramework/DefaultPawn.h"
#include "GameFramework/GameModeBase.h"

#include "TestModPreview.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UTestModInstance;

/// The local player pawn for the standalone mesh replacement preview.
UCLASS(MinimalAPI)
class ATestModPreviewPawn : public ADefaultPawn
{
	GENERATED_BODY()

public:
	ATestModPreviewPawn();

	/// Replaces the preview body's visible mesh with a skeletal mesh.
	bool SetMesh(USkeletalMesh* NewMesh);

protected:
	void PawnClientRestart() override;
	void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> PreviewMesh;

	UPROPERTY()
	TSubclassOf<UTestModInstance> CallbackClass;

	UPROPERTY(Transient)
	TObjectPtr<UTestModInstance> CallbackInstance;
};

/// Selects the preview pawn when playing TestMod.
UCLASS(MinimalAPI)
class ATestModGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATestModGameMode();
};
