// Copyright (c) 2026 Nelaric Contributors
/** @file APIType.h */
#pragma once

#include <string>

namespace Mod::HeaderTool
{
struct APIType
{
	enum class EKind
	{
		Void,
		Scalar,
		Object,
		ObjectInput
	};

	EKind Kind = EKind::Void;
	std::string Cpp;
	std::string Public;
	std::string Object;
	std::string HostInclude;
	std::string EngineModule;
	std::string EngineInclude;
	bool bConst = false;
};
} // namespace Mod::HeaderTool
