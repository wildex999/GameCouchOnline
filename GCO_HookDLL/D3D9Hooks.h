#ifndef D3D9HOOKS_H_
#define D3D9HOOKS_H_

#include <d3d9.h>

#include "FunctionHook.h"

//Hooks
extern FunctionHook hookEndScene;
extern FunctionHook hookPresent;
extern FunctionHook hookPresentEx;
extern FunctionHook hookSwapPresent;
extern FunctionHook hookReset;
extern FunctionHook hookResetEx;

HRESULT STDMETHODCALLTYPE Hooked_D3D9EndScene(IDirect3DDevice9 *device);
HRESULT STDMETHODCALLTYPE Hooked_D3D9Present(IDirect3DDevice9 *device, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion);
HRESULT STDMETHODCALLTYPE Hooked_D3D9PresentEx(IDirect3DDevice9Ex *device, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags);
HRESULT STDMETHODCALLTYPE Hooked_D3D9SwapPresent(IDirect3DSwapChain9 *swap, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags);
HRESULT STDMETHODCALLTYPE Hooked_D3D9Reset(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *params);
HRESULT STDMETHODCALLTYPE Hooked_D3D9ResetEx(IDirect3DDevice9Ex *device, D3DPRESENT_PARAMETERS *params, D3DDISPLAYMODEEX *fullscreenData);

bool localSetup(IDirect3DDevice9 *device);
bool doCapture(IDirect3DDevice9 *device);

#endif
