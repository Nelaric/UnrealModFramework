// Copyright (c) 2026 Nelaric Contributors
/** @file ModExtensionSession.h */
#pragma once

#include "CoreMinimal.h"

class UModExtensionSubsystem;
class UNetConnection;
struct FURL;

namespace Mod::Extension
{
/** @brief Game Thread handshake on UE's pending connection.
 *
 * @details Login compares the admitted gameplay releases before Welcome.
 * The client checks the map and confirms its ready plan before Join.
 */
class MODAPI_API FModNetworkHandshake
{
public:
	/// Fingerprints the generated host API, Hook, and Event contracts.
	static FString GetContractDigest();
	/// Builds the single URL option submitted in the client's login request.
	static FString MakeLoginOption(const UModExtensionSubsystem* Extensions);
	/// Compares the client's admitted releases with the ready server World.
	static bool VerifyLogin(const UModExtensionSubsystem& Extensions, const FString& Options, FString& Error);
	/// Sends the server's plan before UE sends Welcome.
	static bool SendPlan(const UModExtensionSubsystem& Extensions, UNetConnection& Connection);
	/// Accepts and validates a control message for the current pending join.
	bool ReceivePlan(UNetConnection& Connection, uint8 Message, const FString& Value, FString& Error);
	/// Verifies the pending peer and map before UE loads network gameplay.
	bool GetExpectedPlan(const UNetConnection& Connection, const FURL& URL, FString& Digest, FString& Error) const;
	/// Confirms the ready client plan to the server on the pending connection.
	static bool ConfirmPlan(const UModExtensionSubsystem& Extensions, UNetConnection& Connection,
	                        const FString& Digest);
	/// Checks whether an acknowledgment matches the ready server plan.
	static bool ReceiveAcknowledgment(const UModExtensionSubsystem& Extensions, uint8 Message, const FString& Value,
	                                  bool& bAccepted);
	/// Clears the plan when the pending join ends or fails.
	void Reset();

private:
	TWeakObjectPtr<UNetConnection> Peer;
	FString ExpectedMap;
	FString ExpectedDigest;
};
} // namespace Mod::Extension
