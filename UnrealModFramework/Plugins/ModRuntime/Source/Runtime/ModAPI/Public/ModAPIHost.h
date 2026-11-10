// Copyright (c) 2026 Nelaric Contributors
/** @file ModAPIHost.h */
#pragma once

#include "ModAPIRegistry.h"

namespace Mod::API
{
/// Publishes a generated module's registration entry point until unload.
class MODAPI_API FGeneratedHostRegistration
{
public:
	/// Signature of a generated registration entry point.
	using FRegister = void (*)(FRegistry&, TModResult<TArray<FRegistration>>&);

	/** @brief Publishes an entry point for future game instances.
	 * @param InRegister The generated entry point in the owning module.
	 */
	explicit FGeneratedHostRegistration(FRegister InRegister);
	/// Withdraws the entry point and invalidates its live type registrations.
	~FGeneratedHostRegistration();

private:
	FGeneratedHostRegistration(const FGeneratedHostRegistration&) = delete;
	FGeneratedHostRegistration& operator=(const FGeneratedHostRegistration&) = delete;

	friend class ::UModAPIHostSubsystem;
	static TModResult<TArray<FRegistration>> RegisterAll(FRegistry& Registry);

	static FGeneratedHostRegistration* First;
	FGeneratedHostRegistration* Next;
	FRegister Register;
	TArray<TWeakPtr<FRegistrationState>> ActiveTypes;
};

/// Host-only operations used by concrete generated calling bridges.
struct MODAPI_API FHostAccess
{
	/** @brief Enters a synchronous generated call on the game thread.
	 * @param Binding The previously bound target.
	 * @param TypeId Expected source type.
	 * @param Scope Required scope kind.
	 * @param Permissions Required exact permission identifiers.
	 * @param bMutable Whether the source member may mutate its target.
	 * @return The host object or a recoverable validation failure.
	 */
	static TModResult<UObject*> Enter(const FObjectBinding& Binding, FName TypeId, EScope Scope,
	                                  TConstArrayView<FName> Permissions, bool bMutable);
	/** @brief Checks a borrowed UE object argument on the game thread.
	 * @param Binding The target of the current invocation.
	 * @param Object The supplied object, which may be null.
	 * @return Success or a thread, lifetime, or world mismatch error.
	 */
	static TModResult<void> ValidateObjectInput(const FObjectBinding& Binding, const UObject* Object);
	/** @brief Resolves an already loaded argument by its exact object path.
	 * @param Binding The target of the current invocation.
	 * @param Class The expected native class of the argument.
	 * @param Path Borrowed UTF-8 bytes, or null for a null object.
	 * @param Size The byte count, excluding any terminator.
	 * @return The validated object for this synchronous call, or an error.
	 */
	static TModResult<UObject*> ResolveObjectInput(const FObjectBinding& Binding, UClass* Class, const ANSICHAR* Path,
	                                               uint32 Size);
	/** @brief Projects a returned object into an authorized non-owning binding.
	 * @param Source The caller and world of the current invocation.
	 * @param Object The actual returned object.
	 * @param TypeId Expected projected type.
	 * @param Signature Expected generated table signature.
	 * @param bReadOnly Whether mutation through the result is prohibited.
	 * @return An object binding, or a null/type/scope failure.
	 */
	static TModResult<FObjectBinding> ProjectObject(const FObjectBinding& Source, UObject* Object, FName TypeId,
	                                                const TCHAR* Signature, bool bReadOnly);
};
} // namespace Mod::API
