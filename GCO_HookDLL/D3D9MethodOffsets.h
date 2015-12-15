#ifndef D3D9MethodOffsets_H_
#define D3D9MethodOffsets_H_

//The vtable offsets for the functions we hook
//Gotten by counting in d3d9.h

#define D3D9Offset_EndScene 42 //There are 42 method addresses in the vtable before EndScene
#define D3D9Offset_Present 17
#define D3D9Offset_PresentEx 121
#define D3D9Offset_Reset 16
#define D3D9Offset_ResetEx 132

#define D3D9SwapOffset_Present 3

#endif