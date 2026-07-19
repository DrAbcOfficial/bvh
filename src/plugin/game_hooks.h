#pragma once

#include <extdll.h>

int GetEntityAPI2(DLL_FUNCTIONS* functionTable, int* interfaceVersion);
int GetNewDLLFunctions(NEW_DLL_FUNCTIONS* functionTable, int* interfaceVersion);

void ShutdownBvhPlugin();
