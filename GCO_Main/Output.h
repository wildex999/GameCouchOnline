#ifndef OUTPUT_H_
#define OUTPUT_H_

#include <stdio.h>
#include <string>
#include <comdef.h>

//TEMP
//#define _DEBUG

extern const int strBufferSize;
extern char strBuffer[];

#ifdef _DEBUG
#define Debug(...) { sprintf_s(strBuffer, strBufferSize, __VA_ARGS__); OutputDebugString(strBuffer); }
#else
#define Debug(...) { sprintf_s(strBuffer, strBufferSize, __VA_ARGS__); fprintf_s(stdout, strBuffer); }
#endif
#define Output(...) Debug(__VA_ARGS__)

#endif
