// Copyright (c) 2026 Nelaric Contributors
#include "ModNativeImports.h"

#if PLATFORM_MAC
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

namespace Mod::Extension
{
namespace
{
bool IsSystemLibrary(const FString& Path)
{
	return (Path.StartsWith(TEXT("/usr/lib/"), ESearchCase::CaseSensitive) ||
	        Path.StartsWith(TEXT("/System/Library/"), ESearchCase::CaseSensitive)) &&
	       !Path.Contains(TEXT("//")) && !Path.Contains(TEXT("/../")) && !Path.Contains(TEXT("/./")) &&
	       !Path.EndsWith(TEXT("/..")) && !Path.EndsWith(TEXT("/.")) && !Path.EndsWith(TEXT("/")) &&
	       !Path.Contains(TEXT("\\"));
}

bool CheckSymbols(const Native::FImageFile& Image, const symtab_command& Symbols, const dysymtab_command& Dynamic,
                  uint32 LibraryCount)
{
	if (Symbols.cmd != LC_SYMTAB || Dynamic.cmd != LC_DYSYMTAB || Dynamic.iundefsym > Symbols.nsyms ||
	    Dynamic.nundefsym > Symbols.nsyms - Dynamic.iundefsym ||
	    !Image.Contains(Symbols.symoff, static_cast<uint64>(Symbols.nsyms) * sizeof(nlist_64)))
	{
		return false;
	}
	const uint64 First = Dynamic.iundefsym;
	const uint64 End = First + Dynamic.nundefsym;
	for (uint64 Index = First; Index < End; ++Index)
	{
		nlist_64 Symbol{};
		if (!Image.Read(Symbols.symoff + Index * sizeof(Symbol), Symbol) || (Symbol.n_type & N_TYPE) != N_UNDF)
		{
			return false;
		}
		const uint32 Ordinal = GET_LIBRARY_ORDINAL(Symbol.n_desc);
		if (Ordinal == 0 || Ordinal > LibraryCount)
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
	Error = TEXT("The Mach-O extension must use two-level imports from macOS system libraries.");
	if (!File)
	{
		return false;
	}
	const Native::FImageFile Image(*File);
	mach_header_64 Header{};
	if (!Image.Read(0, Header) || Header.magic != MH_MAGIC_64 || Header.filetype != MH_DYLIB ||
	    (Header.flags & MH_TWOLEVEL) == 0 || (Header.flags & MH_FORCE_FLAT) != 0 ||
	    !Image.Contains(sizeof(Header), Header.sizeofcmds) || Header.ncmds > Header.sizeofcmds / sizeof(load_command))
	{
		return false;
	}
	uint32 LibraryCount = 0;
	symtab_command Symbols{};
	dysymtab_command Dynamic{};
	uint64 Offset = sizeof(Header);
	const uint64 End = Offset + Header.sizeofcmds;
	for (uint32 Index = 0; Index < Header.ncmds; ++Index)
	{
		load_command Command{};
		if (End - Offset < sizeof(Command) || !Image.Read(Offset, Command) || Command.cmdsize < sizeof(Command) ||
		    Command.cmdsize % 8 != 0 || Command.cmdsize > End - Offset)
		{
			return false;
		}
		switch (Command.cmd)
		{
		case LC_RPATH:
		case LC_DYLD_ENVIRONMENT:
		case LC_PREBOUND_DYLIB:
		case LC_LOAD_DYLINKER:
			return false;
		case LC_LOAD_DYLIB:
		case LC_LOAD_WEAK_DYLIB:
		case LC_REEXPORT_DYLIB:
		case LC_LOAD_UPWARD_DYLIB:
		case LC_LAZY_LOAD_DYLIB:
		{
			dylib_command Dylib{};
			FString Library;
			if (Command.cmdsize < sizeof(Dylib) || !Image.Read(Offset, Dylib) ||
			    Dylib.dylib.name.offset < sizeof(Dylib) || Dylib.dylib.name.offset >= Command.cmdsize ||
			    !ReadNativeImportString(Image, Offset + Dylib.dylib.name.offset,
			                            Command.cmdsize - Dylib.dylib.name.offset, Library) ||
			    !IsSystemLibrary(Library) || ++LibraryCount > MAX_LIBRARY_ORDINAL)
			{
				Error = TEXT("The Mach-O extension imports an unsupported library: ") + Library;
				return false;
			}
			break;
		}
		case LC_SYMTAB:
			if (Symbols.cmd != 0 || Command.cmdsize != sizeof(Symbols) || !Image.Read(Offset, Symbols))
			{
				return false;
			}
			break;
		case LC_DYSYMTAB:
			if (Dynamic.cmd != 0 || Command.cmdsize != sizeof(Dynamic) || !Image.Read(Offset, Dynamic))
			{
				return false;
			}
			break;
		default:
			break;
		}
		Offset += Command.cmdsize;
	}
	if (Offset != End || !CheckSymbols(Image, Symbols, Dynamic, LibraryCount))
	{
		return false;
	}
	Error.Reset();
	return true;
}
} // namespace Mod::Extension
#endif
