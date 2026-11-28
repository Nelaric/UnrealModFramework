// Copyright (c) 2026 Nelaric Contributors
#include "ModNativeImports.h"

#if PLATFORM_WINDOWS
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"
#include "Windows/WindowsHWrapper.h"

namespace Mod::Extension
{
namespace
{
class FImportImage
{
public:
	explicit FImportImage(FArchive& InFile) : File(InFile), Size(InFile.TotalSize())
	{
	}

	bool ReadHeaders()
	{
		IMAGE_DOS_HEADER Dos{};
		if (!Read(0, Dos) || Dos.e_magic != IMAGE_DOS_SIGNATURE || Dos.e_lfanew < static_cast<int32>(sizeof(Dos)) ||
		    !Read(Dos.e_lfanew, Nt) || Nt.Signature != IMAGE_NT_SIGNATURE ||
		    Nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || !(Nt.FileHeader.Characteristics & IMAGE_FILE_DLL) ||
		    Nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
		    Nt.OptionalHeader.NumberOfRvaAndSizes > IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
		    Nt.FileHeader.SizeOfOptionalHeader <
		        STRUCT_OFFSET(IMAGE_OPTIONAL_HEADER64, DataDirectory) +
		            Nt.OptionalHeader.NumberOfRvaAndSizes * sizeof(IMAGE_DATA_DIRECTORY) ||
		    Nt.FileHeader.NumberOfSections == 0 || Nt.FileHeader.NumberOfSections > 96)
		{
			return false;
		}
		uint64 Offset = uint64(Dos.e_lfanew) + STRUCT_OFFSET(IMAGE_NT_HEADERS64, OptionalHeader) +
		                Nt.FileHeader.SizeOfOptionalHeader;
		Sections.SetNum(Nt.FileHeader.NumberOfSections);
		for (IMAGE_SECTION_HEADER& Section : Sections)
		{
			if (!Read(Offset, Section) || uint64(Section.PointerToRawData) + Section.SizeOfRawData > Size)
			{
				return false;
			}
			Offset += sizeof(Section);
		}
		return true;
	}

	template <class T, class FNameRva>
	bool ReadImports(int32 DirectoryIndex, FNameRva NameOf, TArray<FString>& OutNames)
	{
		if (static_cast<uint32>(DirectoryIndex) >= Nt.OptionalHeader.NumberOfRvaAndSizes)
		{
			return true;
		}
		const auto& Directory = Nt.OptionalHeader.DataDirectory[DirectoryIndex];
		if (Directory.VirtualAddress == 0)
		{
			return Directory.Size == 0;
		}
		const auto Range = FindSection(Directory.VirtualAddress);
		if (Directory.Size > Range.Value)
		{
			return false;
		}
		for (uint64 Offset = 0; Offset + sizeof(T) <= Directory.Size; Offset += sizeof(T))
		{
			T Entry{};
			if (!Read(Range.Key + Offset, Entry))
			{
				return false;
			}
			const uint32 Rva = NameOf(Entry);
			if (Rva == 0)
			{
				const T Terminator{};
				return FMemory::Memcmp(&Entry, &Terminator, sizeof(T)) == 0;
			}
			FString Name;
			if (!ReadName(Rva, Name))
			{
				return false;
			}
			OutNames.AddUnique(MoveTemp(Name));
		}
		return false;
	}

private:
	template <class T> bool Read(uint64 Offset, T& Out)
	{
		if (Offset > Size || sizeof(T) > Size - Offset)
		{
			return false;
		}
		File.Seek(Offset);
		File.Serialize(&Out, sizeof(T));
		return !File.IsError();
	}

	TPair<uint64, uint64> FindSection(uint32 Rva) const
	{
		for (const auto& Section : Sections)
		{
			if (Rva >= Section.VirtualAddress && Rva - Section.VirtualAddress < Section.SizeOfRawData)
			{
				const uint32 Offset = Rva - Section.VirtualAddress;
				return {uint64(Section.PointerToRawData) + Offset, Section.SizeOfRawData - Offset};
			}
		}
		return {};
	}

	bool ReadName(uint32 Rva, FString& Name)
	{
		const auto Range = FindSection(Rva);
		ANSICHAR Buffer[MAX_PATH];
		const int32 Count = FMath::Min<uint64>(Range.Value, UE_ARRAY_COUNT(Buffer));
		if (Count == 0)
		{
			return false;
		}
		File.Seek(Range.Key);
		File.Serialize(Buffer, Count);
		for (int32 Index = 0; !File.IsError() && Index < Count; ++Index)
		{
			const ANSICHAR Ch = Buffer[Index];
			if (!Ch)
			{
				Name = ANSI_TO_TCHAR(Buffer);
				return Name.EndsWith(TEXT(".dll")) && !Name.Contains(TEXT(".."));
			}
			if (!(Ch >= 'a' && Ch <= 'z') && !(Ch >= 'A' && Ch <= 'Z') && !(Ch >= '0' && Ch <= '9') && Ch != '.' &&
			    Ch != '-' && Ch != '_')
			{
				return false;
			}
		}
		return false;
	}

	FArchive& File;
	const uint64 Size;
	IMAGE_NT_HEADERS64 Nt{};
	TArray<IMAGE_SECTION_HEADER> Sections;
};
} // namespace

bool ValidateNativeImports(const FString& Path, FString& Error)
{
	const TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
	if (!File || File->TotalSize() <= 0)
	{
		Error = TEXT("could not read the native extension DLL.");
		return false;
	}
	TArray<FString> Imports;
	FImportImage Image(*File);
	if (!Image.ReadHeaders() ||
	    !Image.ReadImports<IMAGE_IMPORT_DESCRIPTOR>(
	        IMAGE_DIRECTORY_ENTRY_IMPORT, [](const IMAGE_IMPORT_DESCRIPTOR& Entry) { return Entry.Name; }, Imports) ||
	    !Image.ReadImports<IMAGE_DELAYLOAD_DESCRIPTOR>(
	        IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT, [](const IMAGE_DELAYLOAD_DESCRIPTOR& Entry)
	        { return Entry.Attributes.AllAttributes == 1 ? Entry.DllNameRVA : 0; }, Imports))
	{
		Error = TEXT("invalid Win64 x64 DLL or PE import directory.");
		return false;
	}
	TCHAR SystemDirectory[MAX_PATH];
	const uint32 Count = ::GetSystemDirectoryW(SystemDirectory, UE_ARRAY_COUNT(SystemDirectory));
	if (!Count || Count >= UE_ARRAY_COUNT(SystemDirectory))
	{
		Error = TEXT("could not locate Windows system libraries.");
		return false;
	}
	for (const FString& Name : Imports)
	{
		if (!Name.StartsWith(TEXT("api-ms-win-")) && !Name.StartsWith(TEXT("ext-ms-win-")) &&
		    !IFileManager::Get().FileExists(*FPaths::Combine(SystemDirectory, Name)))
		{
			Error = TEXT("the independent extension imports ") + Name +
			        TEXT(". Link private libraries statically and use the SDK function tables for host APIs.");
			return false;
		}
	}
	return true;
}
} // namespace Mod::Extension
#endif
