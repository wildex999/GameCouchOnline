#ifndef FUNCTIONHOOK_H_
#define FUNCTIONHOOK_H_

#include <MinHook.h>

class FunctionHook {
private:
	static bool hasInitialized;
	bool isHooked;
	FARPROC originalFunction;
	FARPROC targetAddress;

	static bool initialize();

public:
	FunctionHook();

	//Hook function
	//Returns true if hook successfull
	bool hook(FARPROC original, FARPROC hook);

	//rehook if no longer hooked
	//Returns true if already hooked or rehooked successfully
	bool reHook();

	//Remove the hook
	//Returns true if unhooked successfully
	bool unHook();

	FARPROC getOriginalFunction();
};

#endif