// Copyright (c) 2026 Nelaric Contributors

#include "Application.h"

#ifdef _WIN32
int wmain(int Count, wchar_t* Arguments[])
{
	return Mod::SdkTool::Application(Count, Arguments).Run();
}
#else
int main(int Count, char* Arguments[])
{
	return Mod::SdkTool::Application(Count, Arguments).Run();
}
#endif
