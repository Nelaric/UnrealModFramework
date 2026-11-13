// Copyright (c) 2026 Nelaric Contributors
#include "ModAPIHostSubsystem.h"
#include "ModInstance.h"
#include "Modules/ModuleManager.h"

namespace Mod::API
{
namespace
{
UModAPIHostSubsystem* FindHost(const UModInstance& Mod)
{
	auto* Game = Mod.GetTypedOuter<UGameInstance>();
	return Game ? Game->GetSubsystem<UModAPIHostSubsystem>() : nullptr;
}
} // namespace

class FModAPIModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UModInstance::SetHostAPIBridge(
		    [](UModInstance& Mod, FName TypeId, const TCHAR* Signature, uint64& Context, uint64& Object)
		    {
			    auto* Host = FindHost(Mod);
			    if (!Host)
			    {
				    return false;
			    }
			    const auto Result = Host->AuthorizeWorldCaller(Mod, TypeId, Signature);
			    if (!Result.IsSuccess())
			    {
				    return false;
			    }
			    Context = Result.GetValue().Native.Context;
			    Object = Result.GetValue().Object;
			    return true;
		    },
		    &UModAPIHostSubsystem::ResolveHostQuery,
		    [](UModInstance& Mod)
		    {
			    if (auto* Host = FindHost(Mod))
			    {
				    Host->RevokeModCallers(Mod);
			    }
		    });
	}

	virtual void ShutdownModule() override
	{
		UModInstance::SetHostAPIBridge(nullptr, nullptr, nullptr);
	}
};
} // namespace Mod::API

IMPLEMENT_MODULE(Mod::API::FModAPIModule, ModAPI);
