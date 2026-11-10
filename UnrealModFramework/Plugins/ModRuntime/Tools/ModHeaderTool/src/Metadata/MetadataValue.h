// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataValue.h */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
/// A parsed value with its original byte position for diagnostics.
struct MetadataValue
{
	/// Literal categories accepted by the metadata parser.
	enum class EKind
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

	/// Literal category before schema validation.
	EKind Kind = EKind::String;
	/// Decoded string or identifier spelling.
	std::string Text;
	/// Elements for a list value.
	std::vector<MetadataValue> Items;
	/// Byte position of the value in the original file.
	std::size_t Offset = 0;
};
} // namespace Mod::HeaderTool
