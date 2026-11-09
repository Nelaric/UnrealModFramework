// Copyright (c) 2026 Nelaric Contributors

#include "ModBuildTool/Application.h"

#ifdef _WIN32
int wmain(int Count, wchar_t* Arguments[])
{
	return Mod::BuildTool::RunApplication(Count, Arguments);
}
#else
int main(int Count, char* Arguments[])
{
	return Mod::BuildTool::RunApplication(Count, Arguments);
}
#endif
