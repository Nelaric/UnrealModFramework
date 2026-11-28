// Copyright (c) 2026 Nelaric Contributors
#include "UnrealModFrameworkDev.modabi.h"
#include "UnrealModFrameworkDev.modextensionsabi.h"

namespace
{
using FValue = UMF_Ext_hook_game_character_change_health_Value;

uint32_t UMF_EXTENSION_CALL ChangeHealth(uint64_t, const UMF_ExtensionCallV1* Call, UMF_ExtensionReplyV1* Reply)
{
	if (Call->ValueSize != sizeof(FValue) || Reply->ValueSize != sizeof(FValue))
	{
		return UMF_ExtensionInvalid;
	}
	*static_cast<FValue*>(Reply->Value) = *static_cast<const FValue*>(Call->Value) + HEALTH_DELTA;
	Reply->Action = UMF_ExtensionWrite;
	return UMF_ExtensionOk;
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
	const auto Query = reinterpret_cast<decltype(&UMF_UnrealModFrameworkDev_ExportTest_GetHostAPIV1)>(
	    Host->FindHostQuery("ExportTest", "UMF_UnrealModFrameworkDev_ExportTest_GetHostAPIV1"));
	UMF_UnrealModFrameworkDev_ExportTest_FHostAPIV1 Table{};
	if (!Query || Query(UE_UMF_UNREALMODFRAMEWORKDEV_ABI_VERSION, sizeof(Table), UE_UMF_UNREALMODFRAMEWORKDEV_ABI_HASH,
	                    &Table) != 0)
	{
		return UMF_ExtensionSignature;
	}
	const UMF_ExtensionRegistrationV1 Registration{sizeof(Registration),
	                                               UMF_ExtensionPre,
	                                               UMF_ExtensionModifyInput,
	                                               0,
	                                               UMF_Ext_hook_game_character_change_health_ID,
	                                               UMF_Ext_hook_game_character_change_health_SIGNATURE,
	                                               "release-health",
	                                               0,
	                                               &ChangeHealth};
	uint64_t Token = 0;
	return Host->Register(Session, &Registration, &Token);
}
