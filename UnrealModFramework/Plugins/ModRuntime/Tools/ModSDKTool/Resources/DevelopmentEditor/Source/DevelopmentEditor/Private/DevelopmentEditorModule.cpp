// Copyright (c) 2026 Nelaric Contributors
#include "ModSdkBuildEditor.h"

#include "ModPackageEditor.h"
#include "Modules/ModuleManager.h"

namespace Mod::Editor
{
class FDevelopmentEditorModule final : public IModuleInterface
{
public:
	void StartupModule() override
	{
		BuildEditor = MakeUnique<FModSdkBuildEditor>();
		BuildEditor->Startup();
		PackageEditor = MakeUnique<FModPackageEditor>();
		PackageEditor->Startup();
	}

	void ShutdownModule() override
	{
		PackageEditor.Reset();
		BuildEditor.Reset();
	}

private:
	TUniquePtr<FModSdkBuildEditor> BuildEditor;
	TUniquePtr<FModPackageEditor> PackageEditor;
};
} // namespace Mod::Editor

IMPLEMENT_MODULE(Mod::Editor::FDevelopmentEditorModule, DevelopmentEditor);
