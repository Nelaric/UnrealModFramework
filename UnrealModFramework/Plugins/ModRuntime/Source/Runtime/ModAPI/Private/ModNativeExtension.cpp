// Copyright (c) 2026 Nelaric Contributors
#include "ModNativeExtension.h"

#include "HAL/PlatformProcess.h"
#include "Misc/ScopeExit.h"
#include "ModAPIHostSubsystem.h"
#include "ModExtensionSubsystem.h"
#include "ModExtensionWorld.h"
#include "UObject/UnrealType.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace Mod::Extension
{
namespace
{
using API::EError;
using API::FNativeAccess;

uint64 LastToken = 0;
TMap<uint64, TWeakPtr<FNativeExtension>> Sessions;
using FNativeNext = TFunction<uint32(const void*, uint32, void*, uint32&)>;
TMap<uint64, TSharedRef<FNativeNext>> Continuations;

uint64 NewToken()
{
	return LastToken == MAX_uint64 ? 0 : ++LastToken;
}

uint32 Status(EError Error)
{
	static_assert(static_cast<uint32>(EError::QueueFull) == UMF_ExtensionQueueFull);
	return static_cast<uint32>(Error);
}

EError DecodeStatus(uint32 Value)
{
	return Value <= UMF_ExtensionQueueFull ? static_cast<EError>(Value) : EError::CallbackFailed;
}

bool ReadString(const char* Value, int32 Limit, FString& Out)
{
	Out.Reset();
	if (!Value)
	{
		return false;
	}
	for (int32 Index = 0; Index <= Limit; ++Index)
	{
		const char Ch = Value[Index];
		if (Ch == '\0')
		{
			return Index > 0;
		}
		if (Index == Limit || Ch < 33 || Ch > 126)
		{
			return false;
		}
		Out.AppendChar(static_cast<TCHAR>(Ch));
	}
	return false;
}

struct FWireField
{
	const FProperty* Property;
	int32 SourceOffset;
	uint32 WireOffset;
	uint32 Size;
};

bool MakeFields(const UScriptStruct& Type, int32 Base, TArray<FWireField>& Fields, uint32& Size)
{
	for (TFieldIterator<FProperty> It(&Type, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		const FProperty* Property = *It;
		const int32 Offset = Base + Property->GetOffset_ForInternal();
		if (const auto* Nested = CastField<FStructProperty>(Property))
		{
			if (!MakeFields(*Nested->Struct, Offset, Fields, Size))
			{
				return false;
			}
			continue;
		}
		const uint32 Width = Property->IsA<FBoolProperty>() ? 1 : Property->GetElementSize();
		Fields.Add({Property, Offset, Size, Width});
		Size += Width;
	}
	return Size <= 1024;
}

template <class T> uint32 WireSize()
{
	if constexpr (std::is_same_v<T, bool>)
	{
		return 1;
	}
	else
	{
		return sizeof(T);
	}
}

template <class T> void WriteScalar(uint8* Out, T Value)
{
	if constexpr (std::is_same_v<T, bool>)
	{
		*Out = Value ? 1 : 0;
	}
	else
	{
		FMemory::Memcpy(Out, &Value, sizeof(T));
	}
}

template <class T> bool ReadScalar(const uint8* In, T& Value)
{
	if constexpr (std::is_same_v<T, bool>)
	{
		Value = *In != 0;
		return *In <= 1;
	}
	else
	{
		FMemory::Memcpy(&Value, In, sizeof(T));
		if constexpr (std::is_floating_point_v<T>)
		{
			return FMath::IsFinite(Value);
		}
		else
		{
			return true;
		}
	}
}
} // namespace

struct FNativeExtension::FCallback
{
	UMF_ExtensionCallbackV1 Function = nullptr;
	uint64 User = 0;
	FPayloadType Type;
	TWeakPtr<FModExtensionWorld> World;
	FObjectKey OwnerKey;
	TWeakObjectPtr<UScriptStruct> Struct;
	TArray<FWireField> Fields;
	uint32 Size = 0;

	template <class T>
	bool Pack(const T& Value, uint64 ObjectContext, const TSharedRef<const uint8>& Lifetime, uint8* Out) const
	{
		if constexpr (!std::is_same_v<T, FPayload>)
		{
			WriteScalar(Out, Value);
		}
		else if (Type.IsObject())
		{
			uint64 Token = 0;
			if (const UObject* Object = Value.GetObject())
			{
				const auto State = World.Pin();
				const auto* Caller = State ? State->Callers.Find(OwnerKey) : nullptr;
				if (!Caller || !State->APIHostOwner.IsValid() ||
				    State->APIHostOwner->GetRegistry() != State->APIRegistry || !State->APIRegistry)
				{
					return false;
				}
				auto Binding = State->APIRegistry->BindPayload(*Caller, *const_cast<UObject*>(Object), Type.TypeId,
				                                               *Type.Signature);
				if (!Binding.IsSuccess())
				{
					return false;
				}
				auto Borrow = Binding.GetValue().Borrow(Lifetime);
				if (!Borrow.IsSuccess())
				{
					return false;
				}
				auto Published = FNativeAccess::Publish(ObjectContext, MoveTemp(Borrow.GetValue()));
				if (!Published.IsSuccess())
				{
					return false;
				}
				Token = Published.GetValue();
			}
			else if (!Value.IsNullObject())
			{
				return false;
			}
			WriteScalar(Out, Token);
		}
		else
		{
			if (!Struct.IsValid() || Value.GetStruct().GetScriptStruct() != Struct.Get())
			{
				return false;
			}
			for (const auto& Field : Fields)
			{
				const auto* Data = Value.GetStruct().GetMemory() + Field.SourceOffset;
				if (const auto* Boolean = CastField<FBoolProperty>(Field.Property))
				{
					WriteScalar(Out + Field.WireOffset, Boolean->GetPropertyValue(Data));
				}
				else
				{
					FMemory::Memcpy(Out + Field.WireOffset, Data, Field.Size);
				}
			}
		}
		return true;
	}

	template <class T> bool Unpack(const uint8* In, uint64 ObjectContext, T& Out) const
	{
		if constexpr (!std::is_same_v<T, FPayload>)
		{
			return ReadScalar(In, Out);
		}
		else if (Type.IsObject())
		{
			uint64 Token = 0;
			ReadScalar(In, Token);
			if (Token == 0)
			{
				Out = FPayload::FromObject(nullptr);
				return true;
			}
			auto Binding = FNativeAccess::ResolveAnyObject(ObjectContext, Token);
			if (!Binding.IsSuccess())
			{
				return false;
			}
			auto Object = FNativeAccess::ReadPayloadObject(Binding.GetValue());
			if (!Object.IsSuccess())
			{
				return false;
			}
			Out = FPayload::FromObject(Object.GetValue());
			return true;
		}
		else
		{
			auto* StructType = Struct.Get();
			if (!StructType)
			{
				return false;
			}
			FInstancedStruct Value(StructType);
			for (const auto& Field : Fields)
			{
				uint8* Data = Value.GetMutableMemory() + Field.SourceOffset;
				const uint8* Wire = In + Field.WireOffset;
				if (const auto* Boolean = CastField<FBoolProperty>(Field.Property))
				{
					bool Parsed = false;
					if (!ReadScalar(Wire, Parsed))
					{
						return false;
					}
					Boolean->SetPropertyValue(Data, Parsed);
				}
				else if (Field.Property->IsA<FFloatProperty>())
				{
					float Parsed = 0;
					if (!ReadScalar(Wire, Parsed))
					{
						return false;
					}
					CastFieldChecked<FFloatProperty>(Field.Property)->SetPropertyValue(Data, Parsed);
				}
				else if (Field.Property->IsA<FDoubleProperty>())
				{
					double Parsed = 0;
					if (!ReadScalar(Wire, Parsed))
					{
						return false;
					}
					CastFieldChecked<FDoubleProperty>(Field.Property)->SetPropertyValue(Data, Parsed);
				}
				else
				{
					FMemory::Memcpy(Data, Wire, Field.Size);
				}
			}
			Out = FPayload::FromStruct(*StructType, Value.GetMemory());
			return true;
		}
	}

	template <class T>
	EError Invoke(const T& Value, UMF_ExtensionReplyV1& Reply, T& Output, const TNext<T>* NextCall = nullptr) const
	{
		alignas(8) uint8 Input[1024];
		alignas(8) uint8 OutputBytes[1024];
		FMemory::Memzero(OutputBytes, Size);
		TSharedRef<const uint8> Lifetime = MakeShared<uint8>(0);
		uint64 ObjectContext = 0;
		if constexpr (std::is_same_v<T, FPayload>)
		{
			if (Type.IsObject())
			{
				const auto State = World.Pin();
				const auto* Caller = State ? State->Callers.Find(OwnerKey) : nullptr;
				if (!Caller || !State->APIHostOwner.IsValid() ||
				    State->APIHostOwner->GetRegistry() != State->APIRegistry || !State->APIRegistry)
				{
					return EError::BindingExpired;
				}
				const auto Open = FNativeAccess::Open(*State->APIRegistry, *Caller, false);
				if (!Open.IsSuccess())
				{
					return Open.GetError();
				}
				ObjectContext = Open.GetValue().Context;
			}
		}
		const uint64 NextToken = NextCall ? NewToken() : 0;
		ON_SCOPE_EXIT
		{
			if (NextToken)
			{
				Continuations.Remove(NextToken);
			}
			if (ObjectContext)
			{
				FNativeAccess::Close(ObjectContext);
			}
		};
		if (!Pack(Value, ObjectContext, Lifetime, Input) || (NextCall && !NextToken))
		{
			return EError::InvalidArgument;
		}
		if (NextCall)
		{
			Continuations.Add(NextToken, MakeShared<FNativeNext>(
			                                 [this, NextCall, ObjectContext, Lifetime](const void* Arg, uint32 Bytes,
			                                                                           void* Out, uint32& Committed)
			                                 {
				                                 T NextInput{};
				                                 if (!Arg || !Out || Bytes != Size ||
				                                     !Unpack(static_cast<const uint8*>(Arg), ObjectContext, NextInput))
				                                 {
					                                 return Status(EError::InvalidArgument);
				                                 }
				                                 const auto Result = (*NextCall)(MoveTemp(NextInput));
				                                 Committed = Result.bCommitted ? 1 : 0;
				                                 return Result.Error != EError::None ||
				                                                Pack(Result.Value, ObjectContext, Lifetime,
				                                                     static_cast<uint8*>(Out))
				                                            ? Status(Result.Error)
				                                            : Status(EError::ObjectUnavailable);
			                                 }));
		}
		const UMF_ExtensionCallV1 Call{sizeof(UMF_ExtensionCallV1), Size, ObjectContext, NextToken, Input};
		Reply = {sizeof(UMF_ExtensionReplyV1), UMF_ExtensionContinue, 0, 0, Size, OutputBytes};
		uint32 Code = UMF_ExtensionCallbackFailed;
#if !PLATFORM_EXCEPTIONS_DISABLED
		try
		{
#endif
			Code = Function(User, &Call, &Reply);
#if !PLATFORM_EXCEPTIONS_DISABLED
		}
		catch (...)
		{
			return EError::CallbackFailed;
		}
#endif
		if (Code != UMF_ExtensionOk)
		{
			return DecodeStatus(Code);
		}
		if (Reply.StructSize != sizeof(Reply) || Reply.Value != OutputBytes || Reply.ValueSize != Size ||
		    Reply.Action > UMF_ExtensionStop || Reply.Committed > 1 || Reply.Reserved)
		{
			return EError::InvalidArgument;
		}
		if (Reply.Action != UMF_ExtensionContinue && !Unpack(OutputBytes, ObjectContext, Output))
		{
			return EError::InvalidArgument;
		}
		return EError::None;
	}
};

FNativeExtension::~FNativeExtension()
{
	Close();
}

bool FNativeExtension::Open(const Mod::Loader::FMountedPackage& Package, UModNativeExtensionInstance& Instance,
                            TSharedRef<FModExtensionWorld> State, FString& Error)
{
	static const UMF_ExtensionHostV1 Host{
	    sizeof(UMF_ExtensionHostV1), UMF_EXTENSION_ABI_VERSION, &Register, &Unregister, &Next, &FindHostQuery};
	if (!Mod::Loader::VerifyNativeExtension(Package))
	{
		Error = Package.ModId + TEXT(": the extracted native extension no longer matches its package.");
		return false;
	}
#if PLATFORM_WINDOWS
	Library = ::LoadLibraryExW(*Package.NativeExtensionPath, nullptr,
	                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
#endif
	const auto Entry = Library ? reinterpret_cast<UMF_ExtensionEntryV1>(
	                                 FPlatformProcess::GetDllExport(Library, TEXT("UMF_RegisterExtensionsV1")))
	                           : nullptr;
	SessionId = Entry ? NewToken() : 0;
	if (!SessionId)
	{
		Error = Package.ModId + TEXT(": could not open UMF_RegisterExtensionsV1.");
		return false;
	}
	World = State;
	Owner = &Instance;
	Sessions.Add(SessionId, AsShared());
	UMF_ExtensionSessionV1 Prepared{sizeof(UMF_ExtensionSessionV1), 0, 0, nullptr};
	uint32 Result = UMF_ExtensionCallbackFailed;
#if !PLATFORM_EXCEPTIONS_DISABLED
	try
	{
#endif
		Result = Entry(&Host, SessionId, &Prepared);
#if !PLATFORM_EXCEPTIONS_DISABLED
	}
	catch (...)
	{
	}
#endif
	if (Prepared.StructSize == sizeof(Prepared) && Prepared.Reserved == 0)
	{
		Shutdown = Prepared;
	}
	else
	{
		Result = UMF_ExtensionInvalid;
	}
	if (Result == UMF_ExtensionOk && !State->RegistrationError.IsEmpty())
	{
		Error = State->RegistrationError;
		return false;
	}
	if (Result != UMF_ExtensionOk)
	{
		Error = FString::Printf(TEXT("%s: native extension registration failed (%u)."), *Package.ModId, Result);
		return false;
	}
	return true;
}

void FNativeExtension::Close()
{
	if (!Library)
	{
		return;
	}
	Sessions.Remove(SessionId);
	SessionId = 0;
	Tokens.Reset();
	if (Shutdown.Close)
	{
#if !PLATFORM_EXCEPTIONS_DISABLED
		try
		{
#endif
			Shutdown.Close(Shutdown.User);
#if !PLATFORM_EXCEPTIONS_DISABLED
		}
		catch (...)
		{
			UE_LOG(LogTemp, Error, TEXT("Native extension shutdown raised an exception."));
		}
#endif
		Shutdown = {};
	}
	FPlatformProcess::FreeDllHandle(Library);
	Library = nullptr;
}

uint32 FNativeExtension::Add(const UMF_ExtensionRegistrationV1& Registration, uint64& Token)
{
	const auto State = World.Pin();
	auto* Instance = Owner.Get();
	auto* Current = State ? State->World.Get() : nullptr;
	auto* Extensions = Current ? Current->GetSubsystem<UModExtensionSubsystem>() : nullptr;
	if (!Extensions || !Instance || Tokens.Num() >= 256)
	{
		return UMF_ExtensionExpired;
	}
	FString Id, Signature, Key;
	if (Registration.StructSize != sizeof(Registration) || Registration.Stage > UMF_ExtensionOn ||
	    Registration.Operations > 31 || !Registration.Callback || !ReadString(Registration.Id, 128, Id) ||
	    !ReadString(Registration.Signature, 64, Signature) || !ReadString(Registration.Key, 96, Key))
	{
		return UMF_ExtensionInvalid;
	}
	const FName ChannelId(Id);
	const auto Channel = State->Channels.FindRef(ChannelId);
	if (!Channel)
	{
		return UMF_ExtensionUnknown;
	}
	const auto& Definition = Channel->GetDefinition();
	const auto& Requirements =
	    State->Mods[State->Instances.FindChecked(FObjectKey(Instance))].Package->Extensions.Contracts;
	if (!Requirements.ContainsByPredicate(
	        [&](const auto& Item)
	        { return Item.Id == ChannelId && Item.Signature == Signature && Signature == Definition.Signature; }))
	{
		return UMF_ExtensionSignature;
	}
	FHandler Handler;
	Handler.Key = FName(Key);
	Handler.Target = Instance;
	Handler.Stage = static_cast<EStage>(Registration.Stage);
	Handler.Operations = static_cast<EOperation>(Registration.Operations);
	Handler.Order.Priority = Registration.Priority;
	const auto Callback = MakeShared<FCallback>();
	Callback->Function = Registration.Callback;
	Callback->User = Registration.User;
	Callback->World = State;
	Callback->OwnerKey = FObjectKey(Instance);
	Callback->Type = Definition.Payload;
	if (Definition.Payload.IsStruct())
	{
		auto* Type = Cast<UScriptStruct>(Definition.Payload.Struct.ResolveObject());
		if (!Type || !MakeFields(*Type, 0, Callback->Fields, Callback->Size))
		{
			return UMF_ExtensionSignature;
		}
		Callback->Struct = Type;
	}
	const auto Install = [&]<class T>()
	{
		if constexpr (!std::is_same_v<T, FPayload>)
		{
			Callback->Size = WireSize<T>();
		}
		else if (Definition.Payload.IsObject())
		{
			Callback->Size = sizeof(uint64);
		}
		THandlerCalls<T> Calls;
		if (Handler.Stage == EStage::Event)
		{
			Calls.Event = [Callback](T Value)
			{
				UMF_ExtensionReplyV1 Reply{};
				T Unused{};
				const auto Error = Callback->Invoke(Value, Reply, Unused);
				return Error == EError::None && (Reply.Action != UMF_ExtensionContinue || Reply.Committed)
				           ? EError::InvalidArgument
				           : Error;
			};
		}
		else if (Handler.Operations == EOperation::Wrap || Handler.Operations == EOperation::Replace)
		{
			const auto Invoke = [Callback](T Value, const TNext<T>* NextCall)
			{
				UMF_ExtensionReplyV1 Reply{};
				T Output{};
				auto Error = Callback->Invoke(Value, Reply, Output, NextCall);
				if (Error == EError::None && Reply.Action != UMF_ExtensionWrite)
				{
					Error = EError::InvalidArgument;
				}
				return TCoreResult<T>{MoveTemp(Output), Error, Reply.Committed == 1};
			};
			if (Handler.Operations == EOperation::Wrap)
			{
				Calls.Wrap = [Invoke](T Value, const TNext<T>& NextCall) { return Invoke(MoveTemp(Value), &NextCall); };
			}
			else
			{
				Calls.Replace = [Invoke](T Value) { return Invoke(MoveTemp(Value), nullptr); };
			}
		}
		else
		{
			Calls.Hook = [Callback](T Value)
			{
				UMF_ExtensionReplyV1 Reply{};
				T Output{};
				THookDecision<T> Decision;
				Decision.Error = Callback->Invoke(Value, Reply, Output);
				if (Decision.Error == EError::None && Reply.Committed)
				{
					Decision.Error = EError::InvalidArgument;
				}
				if (Decision.Error == EError::None && Reply.Action != UMF_ExtensionContinue)
				{
					Decision.Value = MoveTemp(Output);
					Decision.bCancel = Reply.Action == UMF_ExtensionStop;
				}
				return Decision;
			};
		}
		Handler.SetCalls<T>(MoveTemp(Calls));
	};
	switch (Definition.Scalar)
	{
	case EScalar::Float:
		Install.template operator()<float>();
		break;
	case EScalar::Double:
		Install.template operator()<double>();
		break;
	case EScalar::Int32:
		Install.template operator()<int32>();
		break;
	case EScalar::Int64:
		Install.template operator()<int64>();
		break;
	case EScalar::Bool:
		Install.template operator()<bool>();
		break;
	case EScalar::Byte:
		Install.template operator()<uint8>();
		break;
	case EScalar::Projected:
		Install.template operator()<FPayload>();
		break;
	}
	const uint64 IdToken = NewToken();
	if (!IdToken)
	{
		return UMF_ExtensionInvalid;
	}
	const auto Result = Extensions->Register(*Instance, ChannelId, Signature, MoveTemp(Handler));
	if (!Result.IsSuccess())
	{
		return Status(Result.GetError());
	}
	Tokens.Add(IdToken, Result.GetValue());
	Token = IdToken;
	return UMF_ExtensionOk;
}

uint32 FNativeExtension::Remove(uint64 Token)
{
	const auto State = World.Pin();
	auto* Instance = Owner.Get();
	auto* Current = State ? State->World.Get() : nullptr;
	auto* Extensions = Current ? Current->GetSubsystem<UModExtensionSubsystem>() : nullptr;
	FSubscription Subscription;
	if (!Extensions || !Instance || !Tokens.RemoveAndCopyValue(Token, Subscription))
	{
		return UMF_ExtensionExpired;
	}
	Extensions->Unregister(*Instance, Subscription);
	return UMF_ExtensionOk;
}

uint32 FNativeExtension::Register(uint64 Session, const UMF_ExtensionRegistrationV1* Registration, uint64* Token)
{
	if (!IsInGameThread())
	{
		return UMF_ExtensionWrongThread;
	}
	if (!Registration || !Token)
	{
		return UMF_ExtensionInvalid;
	}
	*Token = 0;
	const auto State = Sessions.FindRef(Session).Pin();
	return State ? State->Add(*Registration, *Token) : UMF_ExtensionExpired;
}

uint32 FNativeExtension::Unregister(uint64 Session, uint64 Token)
{
	if (!IsInGameThread())
	{
		return UMF_ExtensionWrongThread;
	}
	const auto State = Sessions.FindRef(Session).Pin();
	return State ? State->Remove(Token) : UMF_ExtensionExpired;
}

uint32 FNativeExtension::Next(uint64 Token, const void* Input, uint32 Size, void* Output, uint32* Committed)
{
	if (!IsInGameThread())
	{
		return UMF_ExtensionWrongThread;
	}
	if (!Committed)
	{
		return UMF_ExtensionInvalid;
	}
	*Committed = 0;
	const auto* Entry = Continuations.Find(Token);
	const TSharedPtr<FNativeNext> Call = Entry ? TSharedPtr<FNativeNext>(*Entry) : nullptr;
	return Call ? (*Call)(Input, Size, Output, *Committed) : UMF_ExtensionExpired;
}

void* FNativeExtension::FindHostQuery(const char* Module, const char* Symbol)
{
	FString ModuleName, SymbolName;
	return IsInGameThread() && ReadString(Module, 128, ModuleName) && ReadString(Symbol, 256, SymbolName)
	           ? FNativeAccess::FindHostQuery(*ModuleName, *SymbolName)
	           : nullptr;
}
} // namespace Mod::Extension
