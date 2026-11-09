// Copyright (c) 2026 Nelaric Contributors
/** @file Metadata.h */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
/// Literal categories accepted by the metadata parser.
enum class EMetadataValueKind
{
	/// A decoded C++ string literal.
	String,
	/// An identifier or qualified type name.
	Identifier,
	/// A numeric, character, or other non-string literal.
	Literal,
	/// A parenthesized sequence of scalar values.
	List,
};

/// A parsed value with its original byte position for diagnostics.
struct FMetadataValue
{
	/// Literal category before schema validation.
	EMetadataValueKind Kind = EMetadataValueKind::String;
	/// Decoded string or identifier spelling.
	std::string Text;
	/// Elements for a list value.
	std::vector<FMetadataValue> Items;
	/// Byte position of the value in the original file.
	std::size_t Offset = 0;
};

/// A named metadata value.
struct FMetadataField
{
	/// Name of the declaration option.
	std::string Name;
	/// Byte position of the field name.
	std::size_t Offset = 0;
	/// Parsed field value.
	FMetadataValue Value;
};
} // namespace Mod::HeaderTool
