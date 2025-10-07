#ifndef PCH_H
#define PCH_H

#define GLM_ENABLE_EXPERIMENTAL

#include "framework.h"
#include <stdio.h>
#include "MinHook.h"

typedef unsigned long long uint64_t;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "ntdll.lib")

#include <algorithm>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <d3d11.h>
#include <dxgi.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <string>
#include <emmintrin.h> // SSE2 intrinsics (__m128d, __m128i, _mm_cvtpd_ps, _mm_cvtsi128_si32)
#include <cmath>       // roundf, tanf, fmaxf
#include <cstdint>     // fixed width types

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>

#include <winternl.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );
static FILE *f;
static uint64_t base;

typedef void *( *tIObjectInitalizer )( __int64 IObject, __int64 a2, int  a3 );
static tIObjectInitalizer oIObjectInitalizer = nullptr;
static uint64_t aIObjectInitalizer;

typedef void *( *tIObjectDeconstructor )( __int64 *Block );
static tIObjectDeconstructor oIObjectDeconstructor = nullptr;
static uint64_t aIObjectDeconstructor;

static std::vector<uint64_t> g_IEntity = {};
#endif //PCH_H