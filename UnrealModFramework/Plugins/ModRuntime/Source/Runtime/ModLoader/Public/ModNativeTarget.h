// Copyright (c) 2026 Nelaric Contributors
#pragma once

/**
 * @file ModNativeTarget.h
 * @brief Shared desktop native-extension target and image format rules.
 */

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformFile.h"

namespace Mod::Native
{
enum class EArchitecture : uint8
{
	X64,
	Arm64
};

enum class EImageFormat : uint8
{
	Pe,
	Elf,
	MachO
};

struct FTarget
{
	EArchitecture Architecture;
	EImageFormat Format;

	const TCHAR* GetSuffix() const
	{
		switch (Format)
		{
		case EImageFormat::Pe:
			return TEXT(".dll");
		case EImageFormat::Elf:
			return TEXT(".so");
		case EImageFormat::MachO:
			return TEXT(".dylib");
		}
		return TEXT("");
	}
};

/// Resolves the exact platform and architecture pair recorded in a cook profile.
inline TOptional<FTarget> ResolveTarget(FStringView Platform, FStringView Architecture)
{
	const bool bX64 = Architecture.Equals(TEXT("x64"), ESearchCase::CaseSensitive);
	const bool bArm64 = Architecture.Equals(TEXT("arm64"), ESearchCase::CaseSensitive);
	if (!bX64 && !bArm64)
	{
		return {};
	}
	const EArchitecture Machine = bX64 ? EArchitecture::X64 : EArchitecture::Arm64;
	if (Platform.Equals(TEXT("Win64"), ESearchCase::CaseSensitive))
	{
		return FTarget{Machine, EImageFormat::Pe};
	}
	if (Platform.Equals(TEXT("Mac"), ESearchCase::CaseSensitive))
	{
		return FTarget{Machine, EImageFormat::MachO};
	}
	if ((bX64 && Platform.Equals(TEXT("Linux"), ESearchCase::CaseSensitive)) ||
	    (bArm64 && Platform.Equals(TEXT("LinuxArm64"), ESearchCase::CaseSensitive)))
	{
		return FTarget{Machine, EImageFormat::Elf};
	}
	return {};
}

inline const TCHAR* GetHostArchitecture()
{
#if PLATFORM_64BITS && PLATFORM_CPU_ARM_FAMILY
	return TEXT("arm64");
#elif PLATFORM_64BITS && PLATFORM_CPU_X86_FAMILY
	return TEXT("x64");
#else
	return TEXT("");
#endif
}

inline bool IsLibraryName(const FString& Name, const FTarget& Target)
{
	const TCHAR* Suffix = Target.GetSuffix();
	const ESearchCase::Type SearchCase = Target.Format == EImageFormat::Pe ? ESearchCase::IgnoreCase
	                                                                      : ESearchCase::CaseSensitive;
	if (Name.Len() <= FCString::Strlen(Suffix) || Name.Len() > 128 || Name.Contains(TEXT("..")) ||
	    !Name.EndsWith(Suffix, SearchCase))
	{
		return false;
	}
	for (const TCHAR Char : Name)
	{
		if (!((Char >= 'A' && Char <= 'Z') || (Char >= 'a' && Char <= 'z') || (Char >= '0' && Char <= '9') ||
		      Char == '.' || Char == '_' || Char == '-'))
		{
			return false;
		}
	}
	return true;
}

/// Bounded reads of packed headers on the supported little-endian hosts.
class FImageFile
{
public:
	explicit FImageFile(IFileHandle& InFile) : File(InFile), Size(FMath::Max<int64>(File.Size(), 0))
	{
	}

	bool Contains(uint64 Offset, uint64 Bytes) const
	{
		return Offset <= Size && Bytes <= Size - Offset;
	}

	bool ReadBytes(uint64 Offset, void* Out, uint64 Bytes) const
	{
		return Contains(Offset, Bytes) && File.Seek(static_cast<int64>(Offset)) &&
		       File.Read(static_cast<uint8*>(Out), static_cast<int64>(Bytes));
	}

	template <typename T> bool Read(uint64 Offset, T& Out) const
	{
		return ReadBytes(Offset, &Out, sizeof(T));
	}

private:
	IFileHandle& File;
	uint64 Size;
};

inline bool MatchesMacOSImage(const FImageFile& Image, bool bX64)
{
	// Read the thin Mach-O 64 header and require a macOS deployment command.
	uint32 Header[8]{};
	if (!Image.Read(0, Header) || Header[0] != 0xfeedfacf ||
	    Header[1] != (bX64 ? 0x01000007u : 0x0100000cu) || Header[3] != 6 ||
	    !Image.Contains(sizeof(Header), Header[5]) || Header[4] > Header[5] / 8)
	{
		return false;
	}
	constexpr uint32 MacOSPlatform = 1;
	constexpr uint32 BuildVersion = 0x32;
	constexpr uint32 VersionMinMacOS = 0x24;
	constexpr uint32 VersionMinIPhoneOS = 0x25;
	constexpr uint32 VersionMinTvOS = 0x2f;
	constexpr uint32 VersionMinWatchOS = 0x30;
	bool bMacOS = false;
	uint64 Offset = sizeof(Header);
	const uint64 End = Offset + Header[5];
	for (uint32 Index = 0; Index < Header[4]; ++Index)
	{
		uint32 Command[2]{};
		if (End - Offset < sizeof(Command) || !Image.Read(Offset, Command) || Command[1] < sizeof(Command) ||
		    Command[1] % 8 != 0 || Command[1] > End - Offset)
		{
			return false;
		}
		switch (Command[0])
		{
		case BuildVersion: {
			uint32 Platform = 0;
			if (bMacOS || Command[1] < 24 || !Image.Read(Offset + sizeof(Command), Platform) ||
			    Platform != MacOSPlatform)
			{
				return false;
			}
			bMacOS = true;
			break;
		}
		case VersionMinMacOS:
			if (bMacOS || Command[1] < 16)
			{
				return false;
			}
			bMacOS = true;
			break;
		case VersionMinIPhoneOS:
		case VersionMinTvOS:
		case VersionMinWatchOS:
			return false;
		default:
			break;
		}
		Offset += Command[1];
	}
	return bMacOS && Offset == End;
}

/// Checks the shared library format and CPU of the staged image.
inline bool MatchesImage(IFileHandle& File, const FTarget& Target)
{
	const FImageFile Image(File);
	const bool bX64 = Target.Architecture == EArchitecture::X64;
	switch (Target.Format)
	{
	case EImageFormat::Pe: {
		uint16 DosMagic = 0;
		uint32 PeOffset = 0;
		uint32 Signature = 0;
		uint16 Machine = 0;
		uint16 OptionalSize = 0;
		uint16 Characteristics = 0;
		uint16 OptionalMagic = 0;
		if (!Image.Read(0, DosMagic) || DosMagic != 0x5a4d || !Image.Read(0x3c, PeOffset) || PeOffset < 64)
		{
			return false;
		}
		const uint64 Offset = PeOffset;
		return Image.Read(Offset, Signature) && Signature == 0x00004550 &&
		       Image.Read(Offset + 4, Machine) && Machine == (bX64 ? 0x8664 : 0xaa64) &&
		       Image.Read(Offset + 20, OptionalSize) && OptionalSize >= sizeof(OptionalMagic) &&
		       Image.Read(Offset + 22, Characteristics) && (Characteristics & 0x2000) != 0 &&
		       Image.Contains(Offset + 24, OptionalSize) && Image.Read(Offset + 24, OptionalMagic) &&
		       OptionalMagic == 0x20b;
	}
	case EImageFormat::Elf: {
		uint8 Ident[16]{};
		uint16 Type = 0;
		uint16 Machine = 0;
		return Image.Contains(0, 64) && Image.Read(0, Ident) && Ident[0] == 0x7f && Ident[1] == 'E' &&
		       Ident[2] == 'L' && Ident[3] == 'F' && Ident[4] == 2 && Ident[5] == 1 && Ident[6] == 1 &&
		       (Ident[7] == 0 || Ident[7] == 3) && Image.Read(16, Type) && Type == 3 && Image.Read(18, Machine) &&
		       Machine == (bX64 ? 62 : 183);
	}
	case EImageFormat::MachO:
		return MatchesMacOSImage(Image, bX64);
	}
	return false;
}
} // namespace Mod::Native
