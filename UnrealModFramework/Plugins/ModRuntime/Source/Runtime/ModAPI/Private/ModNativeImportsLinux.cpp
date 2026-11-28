// Copyright (c) 2026 Nelaric Contributors
#include "ModNativeImports.h"

#if PLATFORM_LINUX
#include <elf.h>

namespace Mod::Extension
{
namespace
{
constexpr uint16 MaxProgramHeaders = 128;
constexpr uint32 MaxDynamicEntries = 4096;
constexpr uint32 MaxHeaders = 4096;

struct FDynamicTables
{
	TArray<uint64> Needed;
	uint64 Strings = 0;
	uint64 StringBytes = 0;
	uint64 Symbols = 0;
	uint64 SymbolEntryBytes = 0;
	uint64 SymbolVersions = 0;
	uint64 VersionNeeds = 0;
	uint64 VersionNeedCount = 0;
};

TOptional<uint64> FindFileRange(const Native::FImageFile& Image, const TArray<Elf64_Phdr>& Headers, uint64 Address,
                                uint64 Bytes)
{
	for (const Elf64_Phdr& Header : Headers)
	{
		if (Header.p_type != PT_LOAD || Address < Header.p_vaddr || !Image.Contains(Header.p_offset, Header.p_filesz))
		{
			continue;
		}
		const uint64 Relative = Address - Header.p_vaddr;
		if (Relative <= Header.p_filesz && Bytes <= Header.p_filesz - Relative)
		{
			return Header.p_offset + Relative;
		}
	}
	return {};
}

bool ReadDynamicTables(const Native::FImageFile& Image, const Elf64_Phdr& Segment, FDynamicTables& Tables)
{
	if (Segment.p_filesz % sizeof(Elf64_Dyn) != 0 || Segment.p_filesz / sizeof(Elf64_Dyn) > MaxDynamicEntries ||
	    !Image.Contains(Segment.p_offset, Segment.p_filesz))
	{
		return false;
	}
	for (uint64 Offset = 0; Offset < Segment.p_filesz; Offset += sizeof(Elf64_Dyn))
	{
		Elf64_Dyn Entry{};
		if (!Image.Read(Segment.p_offset + Offset, Entry))
		{
			return false;
		}
		uint64* Field = nullptr;
		switch (Entry.d_tag)
		{
		case DT_NULL:
			return Tables.Strings != 0 && Tables.StringBytes != 0 && Tables.Symbols != 0 &&
			       Tables.SymbolEntryBytes == sizeof(Elf64_Sym);
		case DT_NEEDED:
			Tables.Needed.Add(Entry.d_un.d_val);
			break;
		case DT_STRTAB:
			Field = &Tables.Strings;
			break;
		case DT_STRSZ:
			Field = &Tables.StringBytes;
			break;
		case DT_SYMTAB:
			Field = &Tables.Symbols;
			break;
		case DT_SYMENT:
			Field = &Tables.SymbolEntryBytes;
			break;
		case DT_VERSYM:
			Field = &Tables.SymbolVersions;
			break;
		case DT_VERNEED:
			Field = &Tables.VersionNeeds;
			break;
		case DT_VERNEEDNUM:
			Field = &Tables.VersionNeedCount;
			break;
		case DT_RPATH:
		case DT_RUNPATH:
		case DT_FILTER:
		case DT_AUXILIARY:
		case DT_AUDIT:
		case DT_DEPAUDIT:
			return false;
		case DT_FLAGS_1:
			if ((Entry.d_un.d_val & (DF_1_PIE | DF_1_GLOBAL)) != 0)
			{
				return false;
			}
			break;
		default:
			break;
		}
		if (Field)
		{
			if (*Field != 0 || Entry.d_un.d_val == 0)
			{
				return false;
			}
			*Field = Entry.d_un.d_val;
		}
	}
	return false;
}

bool IsSystemLibrary(const FString& Name)
{
	const TCHAR* const SystemLibraries[] = {TEXT("libc.so.6"),
	                                        TEXT("libm.so.6"),
	                                        TEXT("libpthread.so.0"),
	                                        TEXT("libdl.so.2"),
	                                        TEXT("librt.so.1"),
	                                        TEXT("libstdc++.so.6"),
	                                        TEXT("libgcc_s.so.1"),
	                                        TEXT("libatomic.so.1"),
#if PLATFORM_CPU_ARM_FAMILY
	                                        TEXT("ld-linux-aarch64.so.1")
#else
	                                        TEXT("ld-linux-x86-64.so.2")
#endif
	};
	for (const TCHAR* Library : SystemLibraries)
	{
		if (Name.Equals(Library, ESearchCase::CaseSensitive))
		{
			return true;
		}
	}
	return false;
}

bool ReadDynamicString(const Native::FImageFile& Image, uint64 StringOffset, const FDynamicTables& Tables, uint64 Index,
                       FString& Out)
{
	return Index < Tables.StringBytes &&
	       ReadNativeImportString(Image, StringOffset + Index, Tables.StringBytes - Index, Out);
}

bool ReadRequiredVersions(const Native::FImageFile& Image, const TArray<Elf64_Phdr>& Headers,
                          const FDynamicTables& Tables, uint64 StringOffset, const TSet<FString>& Dependencies,
                          TSet<uint16>& OutVersions)
{
	if (Tables.VersionNeedCount == 0)
	{
		return Tables.VersionNeeds == 0;
	}
	if (!Tables.VersionNeeds || !Tables.SymbolVersions ||
	    Tables.VersionNeedCount > static_cast<uint64>(Dependencies.Num()))
	{
		return false;
	}
	uint64 Address = Tables.VersionNeeds;
	for (uint64 Index = 0; Index < Tables.VersionNeedCount; ++Index)
	{
		const TOptional<uint64> Offset = FindFileRange(Image, Headers, Address, sizeof(Elf64_Verneed));
		Elf64_Verneed Need{};
		FString Library;
		if (!Offset.IsSet() || !Image.Read(Offset.GetValue(), Need) || Need.vn_version != VER_NEED_CURRENT ||
		    Need.vn_cnt == 0 || Need.vn_cnt > MaxDynamicEntries || Need.vn_aux < sizeof(Need) ||
		    !ReadDynamicString(Image, StringOffset, Tables, Need.vn_file, Library) || !IsSystemLibrary(Library) ||
		    !Dependencies.Contains(Library) || Address > MAX_uint64 - Need.vn_aux)
		{
			return false;
		}
		uint64 AuxAddress = Address + Need.vn_aux;
		for (uint16 AuxIndex = 0; AuxIndex < Need.vn_cnt; ++AuxIndex)
		{
			const TOptional<uint64> AuxOffset = FindFileRange(Image, Headers, AuxAddress, sizeof(Elf64_Vernaux));
			Elf64_Vernaux Aux{};
			if (!AuxOffset.IsSet() || !Image.Read(AuxOffset.GetValue(), Aux) || (Aux.vna_other & 0x7fff) <= 1)
			{
				return false;
			}
			OutVersions.Add(Aux.vna_other & 0x7fff);
			if (AuxIndex + 1 < Need.vn_cnt && (Aux.vna_next < sizeof(Aux) || AuxAddress > MAX_uint64 - Aux.vna_next))
			{
				return false;
			}
			AuxAddress += Aux.vna_next;
		}
		if (Index + 1 < Tables.VersionNeedCount && (Need.vn_next < sizeof(Need) || Address > MAX_uint64 - Need.vn_next))
		{
			return false;
		}
		Address += Need.vn_next;
	}
	return true;
}

bool CheckSymbols(const Native::FImageFile& Image, const Elf64_Ehdr& Header, const TArray<Elf64_Phdr>& Headers,
                  const FDynamicTables& Tables, uint64 StringOffset, const TSet<uint16>& ImportVersions)
{
	if (Header.e_shnum == 0 || Header.e_shnum > MaxHeaders || Header.e_shentsize != sizeof(Elf64_Shdr) ||
	    !Image.Contains(Header.e_shoff, static_cast<uint64>(Header.e_shnum) * sizeof(Elf64_Shdr)))
	{
		return false;
	}
	Elf64_Shdr SymbolTable{};
	for (uint32 Index = 0; Index < Header.e_shnum; ++Index)
	{
		Elf64_Shdr Section{};
		if (!Image.Read(Header.e_shoff + Index * sizeof(Section), Section))
		{
			return false;
		}
		if (Section.sh_type == SHT_DYNSYM && Section.sh_addr == Tables.Symbols)
		{
			SymbolTable = Section;
			break;
		}
	}
	const TOptional<uint64> SymbolOffset = FindFileRange(Image, Headers, Tables.Symbols, SymbolTable.sh_size);
	if (!SymbolOffset.IsSet() || SymbolOffset.GetValue() != SymbolTable.sh_offset ||
	    SymbolTable.sh_entsize != sizeof(Elf64_Sym) || SymbolTable.sh_size <= sizeof(Elf64_Sym) ||
	    SymbolTable.sh_size % sizeof(Elf64_Sym) != 0)
	{
		return false;
	}
	const uint64 Count = SymbolTable.sh_size / sizeof(Elf64_Sym);
	TOptional<uint64> VersionOffset;
	if (Tables.SymbolVersions)
	{
		VersionOffset = FindFileRange(Image, Headers, Tables.SymbolVersions, Count * sizeof(Elf64_Half));
		if (!VersionOffset.IsSet())
		{
			return false;
		}
	}
	for (uint64 Index = 1; Index < Count; ++Index)
	{
		Elf64_Sym Symbol{};
		if (!Image.Read(SymbolOffset.GetValue() + Index * sizeof(Symbol), Symbol))
		{
			return false;
		}
		if (Symbol.st_shndx != SHN_UNDEF)
		{
			continue;
		}
		Elf64_Half Version = 0;
		if (VersionOffset.IsSet() && !Image.Read(VersionOffset.GetValue() + Index * sizeof(Version), Version))
		{
			return false;
		}
		if (ImportVersions.Contains(Version & 0x7fff))
		{
			continue;
		}
		FString Name;
		// Standard compiler probes can be absent from the process.
		if (ELF64_ST_BIND(Symbol.st_info) != STB_WEAK ||
		    !ReadDynamicString(Image, StringOffset, Tables, Symbol.st_name, Name) ||
		    !(Name == TEXT("__gmon_start__") || Name == TEXT("_ITM_registerTMCloneTable") ||
		      Name == TEXT("_ITM_deregisterTMCloneTable") || Name == TEXT("__cxa_finalize")))
		{
			return false;
		}
	}
	return true;
}
} // namespace

bool ValidateNativeImports(const FString& Path, FString& Error)
{
	IPlatformFile& Files = IPlatformFile::GetPlatformPhysical();
	TUniquePtr<IFileHandle> File(Files.OpenRead(*Path));
	Error = TEXT("The ELF extension must be a shared object with valid dynamic tables and system library imports.");
	if (!File)
	{
		return false;
	}
	const Native::FImageFile Image(*File);
	Elf64_Ehdr Header{};
	if (!Image.Read(0, Header) || Header.e_type != ET_DYN || Header.e_ehsize != sizeof(Header) ||
	    Header.e_phentsize != sizeof(Elf64_Phdr) || Header.e_phnum == 0 || Header.e_phnum > MaxProgramHeaders)
	{
		return false;
	}
	TArray<Elf64_Phdr> ProgramHeaders;
	ProgramHeaders.SetNumUninitialized(Header.e_phnum);
	if (!Image.ReadBytes(Header.e_phoff, ProgramHeaders.GetData(), Header.e_phnum * sizeof(Elf64_Phdr)))
	{
		return false;
	}
	const Elf64_Phdr* Dynamic = nullptr;
	for (const Elf64_Phdr& Segment : ProgramHeaders)
	{
		if (Segment.p_type == PT_INTERP)
		{
			return false;
		}
		if (Segment.p_type == PT_DYNAMIC)
		{
			if (Dynamic)
			{
				return false;
			}
			Dynamic = &Segment;
		}
	}
	if (!Dynamic)
	{
		return false;
	}
	const TOptional<uint64> DynamicOffset = FindFileRange(Image, ProgramHeaders, Dynamic->p_vaddr, Dynamic->p_filesz);
	FDynamicTables Tables;
	if (!DynamicOffset.IsSet() || DynamicOffset.GetValue() != Dynamic->p_offset ||
	    !ReadDynamicTables(Image, *Dynamic, Tables))
	{
		return false;
	}
	const TOptional<uint64> StringOffset = FindFileRange(Image, ProgramHeaders, Tables.Strings, Tables.StringBytes);
	if (!StringOffset.IsSet())
	{
		return false;
	}
	TSet<FString> Dependencies;
	for (const uint64 NameOffset : Tables.Needed)
	{
		FString Library;
		if (!ReadDynamicString(Image, StringOffset.GetValue(), Tables, NameOffset, Library) ||
		    !IsSystemLibrary(Library))
		{
			Error = TEXT("The ELF extension imports an unsupported library: ") + Library;
			return false;
		}
		Dependencies.Add(MoveTemp(Library));
	}
	TSet<uint16> ImportVersions;
	if (!ReadRequiredVersions(Image, ProgramHeaders, Tables, StringOffset.GetValue(), Dependencies, ImportVersions) ||
	    !CheckSymbols(Image, Header, ProgramHeaders, Tables, StringOffset.GetValue(), ImportVersions))
	{
		Error = TEXT("The ELF extension has invalid symbol metadata or an import outside its system library contract.");
		return false;
	}
	Error.Reset();
	return true;
}
} // namespace Mod::Extension
#endif
