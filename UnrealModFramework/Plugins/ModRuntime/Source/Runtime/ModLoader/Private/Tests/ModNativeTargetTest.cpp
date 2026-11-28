// Copyright (c) 2026 Nelaric Contributors
#include "ModNativeTarget.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

namespace
{
template <typename T> void WriteHeaderValue(TArray<uint8>& Bytes, int32 Offset, T Value)
{
	FMemory::Memcpy(Bytes.GetData() + Offset, &Value, sizeof(Value));
}

TArray<uint8> MakeNativeHeader(const Mod::Native::FTarget& Target, bool bSharedLibrary)
{
	using namespace Mod::Native;
	TArray<uint8> Bytes;
	Bytes.SetNumZeroed(512);
	const bool bX64 = Target.Architecture == EArchitecture::X64;
	switch (Target.Format)
	{
	case EImageFormat::Pe:
		WriteHeaderValue<uint16>(Bytes, 0, 0x5a4d);
		WriteHeaderValue<uint32>(Bytes, 0x3c, 64);
		WriteHeaderValue<uint32>(Bytes, 64, 0x4550);
		WriteHeaderValue<uint16>(Bytes, 68, bX64 ? 0x8664 : 0xaa64);
		WriteHeaderValue<uint16>(Bytes, 84, 240);
		WriteHeaderValue<uint16>(Bytes, 86, bSharedLibrary ? 0x2000 : 0x0002);
		WriteHeaderValue<uint16>(Bytes, 88, 0x20b);
		break;
	case EImageFormat::Elf:
		WriteHeaderValue<uint32>(Bytes, 0, 0x464c457f);
		Bytes[4] = 2;
		Bytes[5] = 1;
		Bytes[6] = 1;
		WriteHeaderValue<uint16>(Bytes, 16, bSharedLibrary ? 3 : 2);
		WriteHeaderValue<uint16>(Bytes, 18, bX64 ? 62 : 183);
		break;
	case EImageFormat::MachO:
		WriteHeaderValue<uint32>(Bytes, 0, 0xfeedfacf);
		WriteHeaderValue<uint32>(Bytes, 4, bX64 ? 0x01000007 : 0x0100000c);
		WriteHeaderValue<uint32>(Bytes, 12, bSharedLibrary ? 6 : 2);
		WriteHeaderValue<uint32>(Bytes, 16, 1);
		WriteHeaderValue<uint32>(Bytes, 20, 24);
		WriteHeaderValue<uint32>(Bytes, 32, 0x32);
		WriteHeaderValue<uint32>(Bytes, 36, 24);
		WriteHeaderValue<uint32>(Bytes, 40, 1);
		break;
	}
	return Bytes;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModNativeTargetTest, "Nelaric.ModLoader.Native.DesktopTargets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FModNativeTargetTest::RunTest(const FString&)
{
	using namespace Mod::Native;
	const struct
	{
		const TCHAR* Platform;
		const TCHAR* Architecture;
		const TCHAR* FileName;
	} Targets[] = {
	    {TEXT("Win64"), TEXT("x64"), TEXT("Extension.dll")},
	    {TEXT("Win64"), TEXT("arm64"), TEXT("Extension.dll")},
	    {TEXT("Linux"), TEXT("x64"), TEXT("Extension.so")},
	    {TEXT("LinuxArm64"), TEXT("arm64"), TEXT("Extension.so")},
	    {TEXT("Mac"), TEXT("x64"), TEXT("Extension.dylib")},
	    {TEXT("Mac"), TEXT("arm64"), TEXT("Extension.dylib")},
	};
	const FString Path = FPaths::CreateTempFilename(*FPaths::ProjectIntermediateDir(), TEXT("ModNativeImage"));
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*Path);
	};

	for (const auto& Item : Targets)
	{
		const FString Label = FString::Printf(TEXT("%s/%s"), Item.Platform, Item.Architecture);
		const TOptional<FTarget> Target = ResolveTarget(Item.Platform, Item.Architecture);
		if (!TestTrue(Label + TEXT(" resolves"), Target.IsSet()))
		{
			continue;
		}
		const FTarget Expected = Target.GetValue();
		TestTrue(Label + TEXT(" accepts the shared library suffix"), IsLibraryName(Item.FileName, Expected));
		TestFalse(Label + TEXT(" rejects a path instead of a filename"),
		          IsLibraryName(FString(TEXT("../")) + Item.FileName, Expected));
		if (TestTrue(Label + TEXT(" writes a shared image"),
		             FFileHelper::SaveArrayToFile(MakeNativeHeader(Expected, true), *Path)))
		{
			TUniquePtr<IFileHandle> File(IPlatformFile::GetPlatformPhysical().OpenRead(*Path));
			if (TestTrue(Label + TEXT(" opens the image"), File.IsValid()))
			{
				TestTrue(Label + TEXT(" accepts the image"), MatchesImage(*File, Expected));
				FTarget Wrong = Expected;
				Wrong.Architecture =
				    Expected.Architecture == EArchitecture::X64 ? EArchitecture::Arm64 : EArchitecture::X64;
				TestFalse(Label + TEXT(" rejects the other CPU"), MatchesImage(*File, Wrong));
			}
		}
		if (TestTrue(Label + TEXT(" writes an executable image"),
		             FFileHelper::SaveArrayToFile(MakeNativeHeader(Expected, false), *Path)))
		{
			TUniquePtr<IFileHandle> File(IPlatformFile::GetPlatformPhysical().OpenRead(*Path));
			if (TestTrue(Label + TEXT(" opens the executable image"), File.IsValid()))
			{
				TestFalse(Label + TEXT(" rejects an executable"), MatchesImage(*File, Expected));
			}
		}
		if (Expected.Format == EImageFormat::MachO)
		{
			TArray<uint8> MobileImage = MakeNativeHeader(Expected, true);
			WriteHeaderValue<uint32>(MobileImage, 40, 2);
			if (TestTrue(Label + TEXT(" writes an iOS image"), FFileHelper::SaveArrayToFile(MobileImage, *Path)))
			{
				TUniquePtr<IFileHandle> File(IPlatformFile::GetPlatformPhysical().OpenRead(*Path));
				if (TestTrue(Label + TEXT(" opens the iOS image"), File.IsValid()))
				{
					TestFalse(Label + TEXT(" rejects a non-macOS deployment target"), MatchesImage(*File, Expected));
				}
			}
		}
	}
	TestFalse(TEXT("Linux ARM64 uses its own cook platform"), ResolveTarget(TEXT("Linux"), TEXT("arm64")).IsSet());
	TestFalse(TEXT("LinuxArm64 is not an x64 cook platform"), ResolveTarget(TEXT("LinuxArm64"), TEXT("x64")).IsSet());
	TestFalse(TEXT("Unknown architecture is rejected"), ResolveTarget(TEXT("Mac"), TEXT("unknown")).IsSet());
	return true;
}
#endif
