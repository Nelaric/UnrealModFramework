/* Copyright (c) 2026 Nelaric Contributors */
/** @file ModExtensionABI.h */
#ifndef UMF_MOD_EXTENSION_ABI_H
#define UMF_MOD_EXTENSION_ABI_H

#include <stdint.h>

#if defined(_WIN32)
#define UMF_EXTENSION_CALL __cdecl
#define UMF_EXTENSION_EXPORT __declspec(dllexport)
#else
#define UMF_EXTENSION_CALL
#define UMF_EXTENSION_EXPORT __attribute__((visibility("default")))
#endif

#define UMF_EXTENSION_ABI_VERSION 1u
#define UMF_EXTENSION_ENTRY "UMF_RegisterExtensionsV1"

#ifdef __cplusplus
extern "C"
{
#endif

	/// Native extension status values.
	typedef enum UMF_ExtensionStatus
	{
		/// Call succeeded.
		UMF_ExtensionOk = 0,
		/// The entry point requires the Game Thread.
		UMF_ExtensionWrongThread = 1,
		/// The session, binding, or continuation has expired.
		UMF_ExtensionExpired = 2,
		/// The extension or object type is unavailable.
		UMF_ExtensionUnknown = 3,
		/// The descriptor does not match the generated contract.
		UMF_ExtensionSignature = 4,
		/// The requested object cannot be used.
		UMF_ExtensionUnavailable = 5,
		/// The object or invocation belongs to another scope.
		UMF_ExtensionScope = 6,
		/// The host has not granted the requested capability.
		UMF_ExtensionDenied = 7,
		/// The projected object grants read access.
		UMF_ExtensionReadOnly = 8,
		/// The input or callback reply is invalid.
		UMF_ExtensionInvalid = 9,
		/// The operation or continuation was already entered.
		UMF_ExtensionReentrant = 10,
		/// The World has not opened extension dispatch.
		UMF_ExtensionNotReady = 11,
		/// The requested subscription conflicts with the plan.
		UMF_ExtensionConflict = 12,
		/// The World event queue reached its bound.
		UMF_ExtensionQueueFull = 13,
		/// The callback returned a failure.
		UMF_ExtensionCallbackFailed = 14
	} UMF_ExtensionStatus;

	/// Stage numbers and bit flags used by registration.
	typedef enum UMF_ExtensionStage
	{
		/// Runs before the original operation.
		UMF_ExtensionPre = 0,
		/// Runs after successful execution.
		UMF_ExtensionPost = 1,
		/// Observes an event payload.
		UMF_ExtensionEvent = 2,
		/// Executes or wraps the original operation.
		UMF_ExtensionOn = 3
	} UMF_ExtensionStage;

	/// Capabilities requested by a registered handler.
	typedef enum UMF_ExtensionOperation
	{
		/// Observes the current value.
		UMF_ExtensionObserve = 0,
		/// Offers a complete replacement input.
		UMF_ExtensionModifyInput = 1,
		/// Ends execution and supplies a cancellation result.
		UMF_ExtensionCancel = 2,
		/// Offers a complete replacement result.
		UMF_ExtensionModifyResult = 4,
		/// Runs the continuation once.
		UMF_ExtensionWrap = 8,
		/// Provides the operation result.
		UMF_ExtensionReplace = 16
	} UMF_ExtensionOperation;

	/// Decisions a callback may write into its reply.
	typedef enum UMF_ExtensionAction
	{
		/// Keeps the current value.
		UMF_ExtensionContinue = 0,
		/// Commits the supplied output value.
		UMF_ExtensionWrite = 1,
		/// Cancels the operation and returns the supplied result.
		UMF_ExtensionStop = 2
	} UMF_ExtensionAction;

#pragma pack(push, 8)

	/// A callback-local value and its authorized object context.
	typedef struct UMF_ExtensionCallV1
	{
		uint32_t StructSize;
		uint32_t ValueSize;
		uint64_t ObjectContext;
		uint64_t Next;
		const void* Value;
	} UMF_ExtensionCallV1;

	/// Host-owned output storage, zeroed before callback entry.
	typedef struct UMF_ExtensionReplyV1
	{
		uint32_t StructSize;
		uint32_t Action;
		uint32_t Committed;
		uint32_t Reserved;
		uint32_t ValueSize;
		void* Value;
	} UMF_ExtensionReplyV1;

	/// Executes one Game Thread callback.
	typedef uint32_t(UMF_EXTENSION_CALL* UMF_ExtensionCallbackV1)(uint64_t User, const UMF_ExtensionCallV1* Call,
	                                                              UMF_ExtensionReplyV1* Reply);

	/// An exact SDK contract and subscription owned by this DLL session.
	typedef struct UMF_ExtensionRegistrationV1
	{
		uint32_t StructSize;
		uint32_t Stage;
		uint32_t Operations;
		int32_t Priority;
		const char* Id;
		const char* Signature;
		const char* Key;
		uint64_t User;
		UMF_ExtensionCallbackV1 Callback;
	} UMF_ExtensionRegistrationV1;

	/// Host functions for a native extension session.
	typedef struct UMF_ExtensionHostV1
	{
		uint32_t StructSize;
		uint32_t Version;
		uint32_t(UMF_EXTENSION_CALL* Register)(uint64_t Session, const UMF_ExtensionRegistrationV1*, uint64_t* Token);
		uint32_t(UMF_EXTENSION_CALL* Unregister)(uint64_t Session, uint64_t Token);
		uint32_t(UMF_EXTENSION_CALL* Next)(uint64_t Next, const void* Input, uint32_t Size, void* Output,
		                                   uint32_t* Committed);
		void*(UMF_EXTENSION_CALL* FindHostQuery)(const char* Module, const char* Symbol);
	} UMF_ExtensionHostV1;

	/// Called after all callback frames for this session have drained.
	typedef void(UMF_EXTENSION_CALL* UMF_ExtensionCloseV1)(uint64_t User);

	/// Optional per-World teardown returned by the registration export.
	typedef struct UMF_ExtensionSessionV1
	{
		uint32_t StructSize;
		uint32_t Reserved;
		uint64_t User;
		UMF_ExtensionCloseV1 Close;
	} UMF_ExtensionSessionV1;

	/// Registers the DLL's World callbacks and returns its session teardown.
	typedef uint32_t(UMF_EXTENSION_CALL* UMF_ExtensionEntryV1)(const UMF_ExtensionHostV1* Host, uint64_t Session,
	                                                           UMF_ExtensionSessionV1* Out);

#pragma pack(pop)

#ifdef __cplusplus
}
#endif
#endif
