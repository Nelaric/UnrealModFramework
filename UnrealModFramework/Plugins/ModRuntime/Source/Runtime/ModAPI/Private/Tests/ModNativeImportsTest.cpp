// Copyright (c) 2026 Nelaric Contributors
#include "ModNativeImports.h"

#if PLATFORM_WINDOWS && PLATFORM_64BITS && WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Windows/WindowsHWrapper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModNativeImportsTest, "Nelaric.ModAPI.Native.IndependentDll",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FModNativeImportsTest::RunTest(const FString&)
{
	constexpr uint32 SectionOffset = 512;
	constexpr uint32 SectionRva = 4096;
	constexpr uint32 NameOffset = 128;
	const FString Path = FPaths::CreateTempFilename(*FPaths::ProjectIntermediateDir(), TEXT("ModImport"), TEXT(".dll"));
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*Path);
	};

	const auto WriteImage = [&](const ANSICHAR* Name, bool bDelayed, uint16 Machine)
	{
		TArray<uint8> Bytes;
		Bytes.SetNumZeroed(SectionOffset + 512);
		IMAGE_DOS_HEADER Dos{};
		Dos.e_magic = IMAGE_DOS_SIGNATURE;
		Dos.e_lfanew = sizeof(Dos);
		FMemory::Memcpy(Bytes.GetData(), &Dos, sizeof(Dos));
		IMAGE_NT_HEADERS64 Nt{};
		Nt.Signature = IMAGE_NT_SIGNATURE;
		Nt.FileHeader.Machine = Machine;
		Nt.FileHeader.NumberOfSections = 1;
		Nt.FileHeader.SizeOfOptionalHeader = sizeof(Nt.OptionalHeader);
		Nt.FileHeader.Characteristics = IMAGE_FILE_DLL;
		Nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
		Nt.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
		if (bDelayed)
		{
			IMAGE_DELAYLOAD_DESCRIPTOR Entry{};
			Entry.Attributes.AllAttributes = 1;
			Entry.DllNameRVA = SectionRva + NameOffset;
			FMemory::Memcpy(Bytes.GetData() + SectionOffset, &Entry, sizeof(Entry));
			Nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT] = {SectionRva, 2 * sizeof(Entry)};
		}
		else
		{
			IMAGE_IMPORT_DESCRIPTOR Entry{};
			Entry.Name = SectionRva + NameOffset;
			FMemory::Memcpy(Bytes.GetData() + SectionOffset, &Entry, sizeof(Entry));
			Nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = {SectionRva, 2 * sizeof(Entry)};
		}
		FMemory::Memcpy(Bytes.GetData() + Dos.e_lfanew, &Nt, sizeof(Nt));
		IMAGE_SECTION_HEADER Section{};
		Section.VirtualAddress = SectionRva;
		Section.PointerToRawData = SectionOffset;
		Section.SizeOfRawData = 512;
		FMemory::Memcpy(Bytes.GetData() + Dos.e_lfanew + sizeof(Nt), &Section, sizeof(Section));
		FMemory::Memcpy(Bytes.GetData() + SectionOffset + NameOffset, Name, FCStringAnsi::Strlen(Name) + 1);
		return TestTrue(TEXT("Write PE import table fixture"), FFileHelper::SaveArrayToFile(Bytes, *Path));
	};
	constexpr uint16 HostMachine = PLATFORM_CPU_ARM_FAMILY ? IMAGE_FILE_MACHINE_ARM64 : IMAGE_FILE_MACHINE_AMD64;
	constexpr uint16 OtherMachine = PLATFORM_CPU_ARM_FAMILY ? IMAGE_FILE_MACHINE_AMD64 : IMAGE_FILE_MACHINE_ARM64;
	FString Error;
	for (const bool bDelayed : {false, true})
	{
		if (WriteImage("kernel32.dll", bDelayed, HostMachine))
		{
			TestTrue(TEXT("A Windows system import is accepted"), Mod::Extension::ValidateNativeImports(Path, Error));
		}
		if (WriteImage("UnrealEditor-Core.dll", bDelayed, HostMachine))
		{
			TestFalse(TEXT("A direct or delayed UE import is rejected"),
			          Mod::Extension::ValidateNativeImports(Path, Error));
			TestTrue(TEXT("The validation names the unsupported import"),
			         Error.Contains(TEXT("UnrealEditor-Core.dll")));
		}
	}
	if (WriteImage("kernel32.dll", false, OtherMachine))
	{
		TestFalse(TEXT("A different architecture is rejected"), Mod::Extension::ValidateNativeImports(Path, Error));
	}
	return true;
}
#endif
