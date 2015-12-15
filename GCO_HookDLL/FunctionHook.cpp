#include "stdafx.h"
#include "FunctionHook.h"
#include "common.h"

bool FunctionHook::hasInitialized = false;

FunctionHook::FunctionHook() {
	isHooked = false;
}

bool FunctionHook::initialize() {
	MH_STATUS status = MH_Initialize();
	if (status == MH_OK)
		hasInitialized = true;
	else
	{
		LogCurrentTime();
		logOutput << "Failed to initialize MinHook, got error: " << status << std::endl;
	}

	return hasInitialized;
}

bool FunctionHook::hook(FARPROC original, FARPROC hook) {
	if (!hasInitialized)
	{
		if (!initialize())
		{
			LogCurrentTime();
			logOutput << "Failed to hook, failed to initialize MinHook." << std::endl;
			return false;
		}
	}
	if (isHooked) //A FunctionHook object can only manage one hook at a time
	{
		if (!unHook())
		{
			LogCurrentTime();
			logOutput << "Failed to hook, could not remove existing hook." << std::endl;
			return false;
		}
	}

	LPVOID out_original;
	MH_STATUS hookStatus = MH_CreateHook(original, hook, &out_original);
	if (hookStatus != MH_OK)
	{
		LogCurrentTime();
		logOutput << "Failed to create hook, error: " << hookStatus << std::endl;
		return false;
	}
	hookStatus = MH_EnableHook(original);
	if (hookStatus != MH_OK)
	{
		LogCurrentTime();
		logOutput << "Failed to enable hook, error: " << hookStatus << std::endl;
		return false;
	}

	isHooked = true;
	targetAddress = original;
	originalFunction = (FARPROC)out_original;

	return true;
}

bool FunctionHook::unHook() {
	if (!isHooked)
		return true; //Already unhooked is not deemed as an error, same result
	
	MH_STATUS hookStatus = MH_DisableHook(targetAddress);
	if (hookStatus != MH_OK)
	{
		LogCurrentTime();
		logOutput << "Failed to disable hook, error: " << hookStatus << std::endl;
		return false;
	}
	hookStatus = MH_RemoveHook(targetAddress);
	if (hookStatus != MH_OK)
	{
		LogCurrentTime();
		logOutput << "Failed to remove hook, error: " << hookStatus << std::endl;
		return false;
	}

	isHooked = false;

	return true;
}

FARPROC FunctionHook::getOriginalFunction() {
	if (!isHooked)
		return NULL;
	return originalFunction;
}