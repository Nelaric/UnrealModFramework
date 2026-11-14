// Copyright (c) 2026 Nelaric Contributors
#include "GameCharacter.h"

#if MOD_API_BOOTSTRAP
float AGameCharacter::ChangeHealth(float Delta)
{
	return ApplyHealth(Delta);
}

void AGameCharacter::PublishHealthChanged(float)
{
}

void AGameCharacter::PublishHealthChangeDetails(const FGameHealthChange&)
{
}

void AGameCharacter::PublishHealthChangeSubject(const AGameCharacter*)
{
}
#else
#include "ExportTest.modextensions.h"

float AGameCharacter::ChangeHealth(float Delta)
{
	const float Previous = Health;
	if (!FMath::IsFinite(Delta))
	{
		return Health;
	}
	const auto Result = Mod::Generated::ExportTest::AGameCharacter_ChangeHealth::Invoke(
	    *this, HealthHook, Delta,
	    [this, Previous](float Input)
	    {
		    const float Value = ApplyHealth(Input);
		    return Mod::Extension::FCoreResult{Value, Mod::API::EError::None, Value != Previous};
	    },
	    [](float Input) { return FMath::IsFinite(Input); },
	    [this](float Value) { return FMath::IsFinite(Value) && Value >= 0 && Value <= FMath::Max(MaxHealth, 0.0f); });
	if (Result.Error != Mod::API::EError::None)
	{
		UE_LOG(LogTemp, Warning, TEXT("ChangeHealth Hook failed (%d), committed=%d."), static_cast<int32>(Result.Error),
		       Result.bCommitted);
	}
	if (Result.bCommitted)
	{
		UE_LOG(LogTemp, Verbose, TEXT("Character health changed from %g to %g."), Previous, Health);
		PublishHealthChanged(Health);
		FGameHealthChange Change;
		Change.PreviousHealth = Previous;
		Change.CurrentHealth = Health;
		PublishHealthChangeDetails(Change);
		PublishHealthChangeSubject(this);
	}
	return Result.Error == Mod::API::EError::None ? Result.Value : Health;
}

void AGameCharacter::PublishHealthChanged(float CurrentHealth)
{
	Mod::Generated::ExportTest::AGameCharacter_PublishHealthChanged::Publish(*this, HealthEvent, CurrentHealth);
}

void AGameCharacter::PublishHealthChangeDetails(const FGameHealthChange& Change)
{
	Mod::Generated::ExportTest::AGameCharacter_PublishHealthChangeDetails::Publish(*this, HealthDetailsEvent, Change);
}

void AGameCharacter::PublishHealthChangeSubject(const AGameCharacter* Subject)
{
	Mod::Generated::ExportTest::AGameCharacter_PublishHealthChangeSubject::Publish(*this, HealthSubjectEvent, Subject);
}

#endif
