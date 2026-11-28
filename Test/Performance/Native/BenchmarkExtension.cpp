// Copyright (c) 2026 Nelaric Contributors
#include "BenchmarkContract.h"
#include "ModExtensionABI.h"

namespace
{
uint32_t UMF_EXTENSION_CALL Dispatch(uint64_t User, const UMF_ExtensionCallV1* Call, UMF_ExtensionReplyV1* Reply)
{
	if (Call->ValueSize != sizeof(int32_t) || Reply->ValueSize != sizeof(int32_t))
	{
		return UMF_ExtensionInvalid;
	}
	const auto* Host = reinterpret_cast<const UMF_ExtensionHostV1*>(static_cast<uintptr_t>(User));
	const uint32_t Status = Host->Next(Call->Next, Call->Value, Call->ValueSize, Reply->Value, &Reply->Committed);
	if (Status == UMF_ExtensionOk)
	{
		++*static_cast<int32_t*>(Reply->Value);
		Reply->Action = UMF_ExtensionWrite;
	}
	return Status;
}
} // namespace

extern "C" UMF_EXTENSION_EXPORT uint32_t UMF_EXTENSION_CALL UMF_RegisterExtensionsV1(const UMF_ExtensionHostV1* Host,
                                                                                     uint64_t Session,
                                                                                     UMF_ExtensionSessionV1*)
{
	if (Host->Version != UMF_EXTENSION_ABI_VERSION || Host->StructSize < sizeof(*Host))
	{
		return UMF_ExtensionSignature;
	}
	const UMF_ExtensionRegistrationV1 Registration{sizeof(Registration),
	                                               UMF_ExtensionOn,
	                                               UMF_ExtensionWrap,
	                                               0,
	                                               BENCHMARK_HOOK_ID,
	                                               BENCHMARK_HOOK_SIGNATURE,
	                                               "engine-call",
	                                               reinterpret_cast<uintptr_t>(Host),
	                                               &Dispatch};
	uint64_t Token = 0;
	return Host->Register(Session, &Registration, &Token);
}
