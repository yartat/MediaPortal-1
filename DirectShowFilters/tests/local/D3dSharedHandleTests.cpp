// MadPresenter::ReinitOSDDevice (SECURITY_AUDIT.md: CD-09) used to call CloseHandle on the shared handles of its textures.
// The handle that CreateTexture returns for a shared Direct3D 9Ex texture is not a handle of the process, so it is not closed
// with CloseHandle: the texture owns it. Needs a graphics device: local only.
#include <gtest/gtest.h>
#include <d3d9.h>
#include "LocalSupport.h"

TEST(D3dSharedHandle, ASharedTextureHandleIsNotAHandleOfTheProcess)
{
  IDirect3D9Ex* d3d = nullptr;
  if (FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d)) || !d3d)
    GTEST_SKIP() << "Direct3D 9Ex is not available";
  HWND window = CreateWindowExW(0, L"STATIC", L"mp_local_tests", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  D3DPRESENT_PARAMETERS pp = {};
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.BackBufferFormat = D3DFMT_UNKNOWN;
  pp.hDeviceWindow = window;
  pp.BackBufferWidth = 64;
  pp.BackBufferHeight = 64;
  IDirect3DDevice9Ex* device = nullptr;
  HRESULT hr = d3d->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp, nullptr, &device);
  if (FAILED(hr))
  {
    d3d->Release();
    DestroyWindow(window);
    GTEST_SKIP() << "cannot create a Direct3D 9Ex device";
  }

  IDirect3DTexture9* texture = nullptr;
  HANDLE shared = nullptr;
  ASSERT_EQ(device->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, &shared), D3D_OK);
  ASSERT_NE(shared, nullptr);

  DWORD flags = 0;
  EXPECT_FALSE(GetHandleInformation(shared, &flags)) << "the shared handle is a handle of the process: CloseHandle would be right";

  texture->Release();
  device->Release();
  d3d->Release();
  DestroyWindow(window);
}
