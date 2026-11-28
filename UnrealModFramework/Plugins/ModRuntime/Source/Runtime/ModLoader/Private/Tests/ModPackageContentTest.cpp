// Copyright (c) 2026 Nelaric Contributors
#include "ModPackageExtensions.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "ModLoader.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModPackageContentTest, "Nelaric.ModLoader.Archive.UnmountedContentPath",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FModPackageContentTest::RunTest(const FString&)
{
	using namespace Mod::Loader;
	const FString Root(TEXT("/NelaricArchiveFixture/"));
	TestFalse(TEXT("The fixture's package root is unmounted"), FPackageName::MountPointExists(Root));
	const auto ReadContent = [&](const TCHAR* Path)
	{
		const auto Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("slot"), TEXT("test.asset"));
		Entry->SetStringField(TEXT("id"), TEXT("test.default"));
		Entry->SetStringField(TEXT("action"), TEXT("replace"));
		Entry->SetStringField(TEXT("asset"), Path);
		const auto Extensions = MakeShared<FJsonObject>();
		Extensions->SetNumberField(TEXT("schemaVersion"), 2);
		Extensions->SetArrayField(TEXT("content"), {MakeShared<FJsonValueObject>(Entry)});
		FJsonObject Manifest;
		Manifest.SetObjectField(TEXT("extensions"), Extensions);
		FLoadResult Result;
		Result.Package.PackageRoot = Root;
		return Private::ReadExtensions(Manifest, Result) && Result.Package.Extensions.Content.Num() == 1 &&
		       Result.Package.Extensions.Content[0].Asset.ToString() == Path;
	};
	TestTrue(TEXT("An asset in the unmounted root is accepted"),
	         ReadContent(TEXT("/NelaricArchiveFixture/Meshes/Fixture.Fixture")));
	TestFalse(TEXT("A foreign root is rejected"), ReadContent(TEXT("/Game/Meshes/Fixture.Fixture")));
	TestFalse(TEXT("An invalid object name is rejected"), ReadContent(TEXT("/NelaricArchiveFixture/Meshes/Fixture.")));
	return true;
}
#endif
