#include "pch.h"

static bool bStickyAim = true;
static float sens = 0.45f;
static signed int clamp_min = -150;
static signed int clamp_max = 150;
static float fov = 200.f;

bool can_read( void *addr, size_t size )
{
    MEMORY_BASIC_INFORMATION mbi;
    if ( VirtualQuery( addr, &mbi, sizeof( mbi ) ) == 0 )
        return false;

    if ( !( mbi.Protect & ( PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE ) ) )
        return false;

    if ( mbi.State != MEM_COMMIT )
        return false;

    return ( (char *)addr + size ) <= ( (char *)mbi.BaseAddress + mbi.RegionSize );
}

bool _can_read( void *addr, size_t size )
{
    return (uint64_t)addr > base && (uint64_t) addr < base + 0xffffffff;
}

template <typename T>
bool safe_read( uint64_t addr, T &val )
{
    if ( !_can_read( (void *)addr, sizeof( T ) ) ) return false;
    val = *(T *)addr;
    return true;
}

template <typename T>
bool safe_read_slow ( uint64_t addr, T &val )
{
    if ( !can_read( (void *)addr, sizeof( T ) ) ) return false;
    val = *(T *)addr;
    return true;
}

typedef struct {
    char button;
    char x;
    char y;
    char wheel;
    char unk1;
} _MOUSE;


namespace bloodstrike
{
    namespace renderer
    {
        constexpr uint64_t hwnd = 0x6DE9430; // updated 9.26

        static ID3D11Device *deviceInstance = nullptr;
        ID3D11DeviceContext *contextInstance = nullptr;
        static ID3D11RenderTargetView *rtv = nullptr;
        static HWND hWindow;
        static bool hooked = false;

        static uint64_t camera = 0x0;
        static std::vector<uint64_t> all_cameras = {};

    }

    namespace funcs
    {
        constexpr uint64_t Messiah__IObject__Initalizer = 0x2CF160;       // updated 9.26
        constexpr uint64_t Messiah__IObject__Deconstructor = 0x2CF890;    // updated 9.26
        constexpr uint64_t Messiah_WorldToScreen = 0x940F60;              // updated 9.26
        constexpr uint64_t GetRawInputData = 0x3BE8FF8;
    }

    namespace vftables
    {
        const uint64_t Messiah__IEntity = 0x3D80048;                // updated 9.26
        const uint64_t Messiah__ICamera = 0x3F9B1D0;                // updated 9.26
        const uint64_t Messiah__AnimationCore__Pose = 0x3FA2F18;    // updated 9.26
        const uint64_t Messiah__SkeletonComponent = 0x4103698;      // updated 9.26
        const uint64_t Messiah__Actor = 0x5001C20;                  // updated 9.26
        const uint64_t Messiah__ActorComponent = 0x4138AB0;         // updated 9.28
        const uint64_t Messiah__IArea = 0x3FBAA68;
        const uint64_t Messiah__TachComponent = 0x4101FC8;
        const uint64_t Messiah__FontType = 0x3C189F0;
    }
}

void *hkIObjectInitalizer( __int64 IObject, __int64 a2, int a3 )
{
    if ( IObject && std::find( g_IEntity.begin( ), g_IEntity.end( ), IObject ) == g_IEntity.end( ) && can_read((void *)IObject, sizeof(void *)) )
    {
        g_IEntity.push_back( IObject );
    }

    return oIObjectInitalizer( IObject, a2, a3 );
}

void *hkIObjectDeconstructor( __int64 *Block )
{
    if ( Block )
    {
        if ( *Block == bloodstrike::renderer::camera ) bloodstrike::renderer::camera = 0x0;
        if ( std::find( bloodstrike::renderer::all_cameras.begin( ), bloodstrike::renderer::all_cameras.end( ), *Block ) == bloodstrike::renderer::all_cameras.end( ) )
        {
            bloodstrike::renderer::all_cameras.erase( std::remove( bloodstrike::renderer::all_cameras.begin( ), bloodstrike::renderer::all_cameras.end( ), *Block ), bloodstrike::renderer::all_cameras.end( ) );
        }

        g_IEntity.erase( std::remove( g_IEntity.begin( ), g_IEntity.end( ), *Block ), g_IEntity.end( ) );
    }

    return oIObjectDeconstructor( Block );
}

typedef HRESULT( *tPresent )( IDXGISwapChain *pSwapChain, UINT, UINT );
static tPresent oPresent = nullptr;
static uint64_t aPresent;

typedef double *( *tProject )( __int64 thisptr, double * out, __int64 worldpos );
static tProject oProject = nullptr;
static uint64_t aProject;

typedef void ( *tSetBonePos ) ( __int64 SkeletonComponent, void *NewBonePos );
static tSetBonePos oSetBonePos = nullptr;
static uint64_t aSetBonePos;

typedef UINT ( *tGetRawInputData ) ( HRAWINPUT, UINT, LPVOID, PUINT, UINT );
tGetRawInputData oGetRawInputData;
static uint64_t aGetRawInputData;

typedef HRESULT( *tResizeBuffers )( IDXGISwapChain *swap, UINT bufferCount, UINT width, UINT height, DXGI_FORMAT newFormat, UINT flags );
static tResizeBuffers oResizeBuffers;
static uint64_t aResizeBuffers;

static bool should_change_mouse;
static char dx;
static char dy;

UINT WINAPI hkGetRawInputData( HRAWINPUT hRaw, UINT uiCmd, LPVOID pData, PUINT pcbSize, UINT cbHeader ) {
    UINT ret = oGetRawInputData( hRaw, uiCmd, pData, pcbSize, cbHeader );
    if ( !should_change_mouse ) return ret;

    if ( pData ) {
        RAWINPUT *ri = (RAWINPUT *)pData;
        if ( ri->header.dwType == RIM_TYPEMOUSE ) {
            ri->data.mouse.lLastX = dx;
            ri->data.mouse.lLastY = dy;
        }
    }
    return ret;
}

bool w2s( __int64 cam, const glm::vec3 &world, glm::vec2 &out )
{
    float relX = world.x - *(float *)( cam + 124 );
    float relY = world.y - *(float *)( cam + 128 );
    float relZ = world.z - *(float *)( cam + 132 );

    float px = relX * *(float *)( cam + 772 )
        + relY * *(float *)( cam + 784 )
        + relZ * *(float *)( cam + 796 );

    float py = relX * *(float *)( cam + 776 )
        + relY * *(float *)( cam + 788 )
        + relZ * *(float *)( cam + 800 );

    float pzOrig = relX * *(float *)( cam + 780 )
        + relY * *(float *)( cam + 792 )
        + relZ * *(float *)( cam + 804 );

    if ( pzOrig >= -0.01f ) // behind camera, original w2s doesn't have this for some reason
        return false;

    float pz = -pzOrig;

    float fov = *(float *)( cam + 824 );
    float f = 1.0f / tanf( ( fov * 0.017453292f ) * 0.5f );

    float screenW = (float)*(uint16_t *)( cam + 752 );
    float screenH = (float)*(uint16_t *)( cam + 754 );

    float invZ = 1.0f / fmaxf( fabsf( pz ), 0.000001f );

    out.x = roundf( ( ( px * invZ ) * f * screenH + screenW ) * 0.5f * 10.0f ) * 0.1f;
    out.y = roundf( ( ( screenH - ( ( py * invZ ) * f * screenH ) ) * 0.5f ) * 10.0f ) * 0.1f;

    return true;
}

HRESULT hkResizeBuffers( IDXGISwapChain *swap, UINT bufferCount, UINT width, UINT height, DXGI_FORMAT newFormat, UINT flags )
{
    if ( bloodstrike::renderer::rtv ) {
        bloodstrike::renderer::rtv->Release( );
        bloodstrike::renderer::rtv = nullptr;
    }

    HRESULT hr = oResizeBuffers( swap, bufferCount, width, height, newFormat, flags );
    if ( FAILED( hr ) ) return hr;

    ID3D11Texture2D *backBuffer = nullptr;
    swap->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (void **)&backBuffer );
    if ( backBuffer ) {
        bloodstrike::renderer::deviceInstance->CreateRenderTargetView( backBuffer, nullptr, &bloodstrike::renderer::rtv );
        backBuffer->Release( );
    }

    return hr;
}

static int bone_offset = 158;

HRESULT hkPresent( IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags )
{
    if ( !bloodstrike::renderer::hooked )
    {
        if ( FAILED( pSwapChain->GetDevice( __uuidof( ID3D11Device ), (void **)&bloodstrike::renderer::deviceInstance ) ) )
            return oPresent( pSwapChain, SyncInterval, Flags );

        bloodstrike::renderer::deviceInstance->GetImmediateContext( &bloodstrike::renderer::contextInstance );

        ID3D11Texture2D *backBuffer = nullptr;
        pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (void **)&backBuffer );

        if ( !backBuffer ) return oPresent( pSwapChain, SyncInterval, Flags );

        bloodstrike::renderer::deviceInstance->CreateRenderTargetView( backBuffer, nullptr, &bloodstrike::renderer::rtv );
        backBuffer->Release( );

        IMGUI_CHECKVERSION( );
        ImGui::CreateContext( );
        ImGuiIO &io = ImGui::GetIO( ); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

        ImGui::StyleColorsDark( );

        bloodstrike::renderer::hWindow = *(HWND *)( base + bloodstrike::renderer::hwnd );

        ImGui_ImplWin32_Init( bloodstrike::renderer::hWindow );
        ImGui_ImplDX11_Init( bloodstrike::renderer::deviceInstance, bloodstrike::renderer::contextInstance );

        bloodstrike::renderer::hooked = true;
        return oPresent( pSwapChain, SyncInterval, Flags );
    }

    DXGI_SWAP_CHAIN_DESC desc;
    pSwapChain->GetDesc( &desc );

    ImGuiIO &io = ImGui::GetIO( );
    io.MouseDown[0] = ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) != 0;
    io.WantCaptureMouse = true;

    ImGui_ImplDX11_NewFrame( );
    ImGui_ImplWin32_NewFrame( );
    ImGui::NewFrame( );

    if ( bloodstrike::renderer::camera != 0x0 )
    {
        ImVec2 ds = io.DisplaySize;
        ds.x /= 2.f;
        ds.y /= 2.f;

        glm::vec2 sc = { ds.x, ds.y };
        bool canProject = *(int *)( bloodstrike::renderer::camera + 384 ) != 0x7FFFFFFF || *(int *)( bloodstrike::renderer::camera + 388 ) != 0x7FFFFFFF;
        if ( canProject && *(int *)( bloodstrike::renderer::camera + 392 ) != 0x7FFFFFFF )
        {
            float closest_dst = FLT_MAX;
            glm::vec2 target_pos{ -1.f, -1.f };

            glm::mat4x3 local_trans = *(glm::mat4x3 *)( bloodstrike::renderer::camera + 0x58 );
            glm::vec3 local_pos = local_trans[3];
            for ( auto addr : g_IEntity )
            {
                if ( !addr ) continue;

                uint64_t vtable = *(uint64_t *)( addr );

                if ( vtable == base + bloodstrike::vftables::Messiah__IEntity )
                {
                    uint64_t expiredObj = *(uint64_t *)( addr + 0x18 );
                    if ( !expiredObj )
                    {
                        g_IEntity.erase( std::remove( g_IEntity.begin( ), g_IEntity.end( ), addr ), g_IEntity.end( ) );
                        continue;
                    }

                    uint64_t isLocalEntity = *(uint64_t *)( addr + 0xF8 );
                    if ( isLocalEntity )
                    {
                        continue;
                    }

                    uint64_t SkelCache = 0;
                    uint64_t BoneData = 0;


                    glm::mat4x3 trans = *(glm::mat4x3 *)( addr + 0x58 );
                    glm::vec3 coords = trans[3];

                    float d = ( glm::distance( local_pos, coords ) );
                    float d2 = (int)d;
                    if ( d < 1.f ) continue;

                    if ( coords.x - (float)(int)coords.x > 0.000 ) continue;

                    uint64_t CachedObjects = *(uint64_t *)( addr + 0x40 );
                    if ( !CachedObjects )
                    {
                        continue;
                    }

                    uint64_t ActorComponent = *(uint64_t *)( CachedObjects + 0x10 );
                    if ( !ActorComponent )
                    {
                        continue;
                    }

                    uint64_t Area = *(uint64_t *)( addr + 0x88 );
                    if ( !Area ) continue;
                    void **ac_vtable = *(void ***)( Area );
                    if ( (uint64_t)ac_vtable != base + bloodstrike::vftables::Messiah__IArea )
                    {
                        continue;
                    }


                    if ( !can_read( (void *)ActorComponent, sizeof( void * ) ) ) continue;

                    ac_vtable = *(void ***)( ActorComponent );
                    if ( (uint64_t)ac_vtable != base + bloodstrike::vftables::Messiah__ActorComponent )
                    {
                        continue;
                    }


                    uint64_t Actor = *(uint64_t *)( ActorComponent + 0xD0 );
                    if ( !Actor )
                    {
                        continue;
                    }
                    ac_vtable = *(void ***)( Actor );
                    if ( (uint64_t)ac_vtable != base + bloodstrike::vftables::Messiah__Actor )
                    {
                        continue;
                    }

                    uint64_t Pose = *(uint64_t *)( Actor + 0x18 );
                    if ( !Pose )
                    {
                        continue;
                    }
                    ac_vtable = *(void ***)( Pose );
                    if ( (uint64_t)ac_vtable != base + bloodstrike::vftables::Messiah__AnimationCore__Pose )
                    {
                        continue;
                    }

                    SkelCache = *(uint64_t *)( Pose + 0x10 );
                    if ( !SkelCache )
                    {
                        continue;
                    }

                    

                    // bone data starts at 0x88
                    // each bone is 0x98 in length


                    SkelCache += 0x88; // junk data before the array starts
                    if ( !can_read( (void *)( *(uint64_t *)SkelCache ), sizeof( void * ) ) ) continue;

                    uint64_t font_check = *(uint64_t *)( CachedObjects + 0x20 );
                    if ( can_read( (void *)font_check, sizeof( void * ) ) )
                    {
                        uint64_t fc_vtable = *(uint64_t *)( font_check );
                        if ( fc_vtable == base + bloodstrike::vftables::Messiah__FontType ) continue;
                    }


                    glm::vec2 result;
                    if ( w2s( bloodstrike::renderer::camera, coords, result ) )
                    {
                        ImVec2 pos = { (float)result[0], (float)result[1] };
                        if ( pos.x > ImGui::GetIO( ).DisplaySize.x || pos.y > ImGui::GetIO( ).DisplaySize.y ) continue;
                        if ( pos.x < 1.0 || pos.y < 1.0 ) continue;
                        int dst_m = (int)( d2 / 5 );

                        if ( dst_m < 1000 )
                        {
                            std::string txt = std::format( "[enemy {:d}m]", dst_m );
                            float text_size_x = ImGui::CalcTextSize( txt.c_str( ) ).x; text_size_x /= 2.f;

                            bool visible = *(bool *)( addr + 0x128 );
                            ImColor color = visible ? ImColor( 100, 25, 25, 255 ) : ImColor( 175, 175, 175, 255 );

                            ImGui::GetForegroundDrawList( )->AddText( ImVec2( pos.x-text_size_x, pos.y ), color, txt.c_str( ) );

                            glm::vec3 head_pos = coords + glm::vec3( 0.0f, bone_offset / 100.f, 0.0f );

                            glm::vec2 hp;
                            if ( w2s( bloodstrike::renderer::camera, head_pos, hp ) )
                            {
                                ImGui::GetForegroundDrawList( )->AddLine( ImVec2( ds.x, 0 ), ImVec2( hp.x, hp.y ), color );

                                float dst = glm::distance( hp, sc );
                                if ( dst < closest_dst )
                                {
                                    if ( !bStickyAim )
                                    {
                                        closest_dst = dst;
                                        target_pos = hp;
                                    }
                                    else
                                    {
                                        if ( !should_change_mouse || closest_dst == FLT_MAX )
                                        {
                                            closest_dst = dst;
                                            target_pos = hp;
                                        }
                                    }
                                }
                            }

                        }
                    }
                }
                else
                {
                    g_IEntity.erase( std::remove( g_IEntity.begin( ), g_IEntity.end( ), addr ), g_IEntity.end( ) );
                }
            }

            if ( closest_dst < fov && target_pos.x != -1.f && GetAsyncKeyState(VK_RBUTTON ) )
            {
                POINT target = { (LONG)target_pos.x, (LONG)target_pos.y };
                ClientToScreen( bloodstrike::renderer::hWindow, &target );

                POINT cur;
                GetCursorPos( &cur );

                dx = target.x - cur.x;
                dy = target.y - cur.y;

                dx *= sens;
                dy *= sens;

                // dx = (signed char)std::clamp( (signed int)dx, clamp_min, clamp_max );
                // dy = (signed char)std::clamp( (signed int)dy, clamp_min, clamp_max );

                SetCursorPos( (int)target_pos.x, (int)target_pos.y );

                should_change_mouse = true;
            }
            else
            {
                should_change_mouse = false;
                closest_dst = FLT_MAX;
            }
        }
        ImGui::GetForegroundDrawList( )->AddCircle( ds, fov, ImColor( 255, 255, 255, 255 ), 250.f );
    }
    else // no ICamera object
    {
        ImGui::GetForegroundDrawList( )->AddText( ImVec2( 100, 100 ), ImColor( 255, 0, 0, 255 ), "looking for camera..." );
        for ( auto addr : g_IEntity )
        {
            if ( !addr ) continue;

            void **vtable = *(void ***)addr;
            if ( vtable && (uint64_t)vtable == base + bloodstrike::vftables::Messiah__ICamera )
            {
                printf( "[camera] concidering %llX\n", addr );
                bool canProject = *(int *)( addr + 384 ) != 0x7FFFFFFF || *(int *)( addr + 388 ) != 0x7FFFFFFF;
                if ( canProject )
                {
                    canProject = *(int *)( addr + 392 ) != 0x7FFFFFFF;
                }

                if ( canProject )
                {
                    float fv = *(float *)( addr + 0x18 );
                    canProject = fv == 0.00f;
                }

                if ( canProject )
                {
                    bloodstrike::renderer::camera = addr;
                    printf( "[camera] set camera to %llX\n", addr );
                    break;
                }
                else
                {
                    printf( "[invalid camera] %llX\n", addr );
                }
            }
        }
    }
    

    ImGui::Begin( "undetekted p2c (vanguard bypass) (2018 undetected new) (imgui bypass injector version)" );
    ImGui::Text( "Total objects: %d", g_IEntity.size( ) );
    ImGui::SliderInt( "BoneOffset", &bone_offset, 150, 500 );
    ImGui::SliderInt( "Clamp Min", &clamp_min, -255, -1 );
    ImGui::SliderInt( "Clamp Max", &clamp_max, 1, 255 );
    ImGui::SliderFloat( "dx/dy sens", &sens, 0.001, 1.000, "%.5f" );
    ImGui::SliderFloat( "fov", &fov, 35.f, 500.f );
    ImGui::Checkbox( "Sticky Aim", &bStickyAim );
    ImGui::End( );

    ImGui::Render( );
    const float clear_color_with_alpha[4] = { 0.f, 0.f, 0.f, 255.f };
    bloodstrike::renderer::contextInstance->OMSetRenderTargets( 1, &bloodstrike::renderer::rtv, nullptr );
    ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData( ) );

    return oPresent( pSwapChain, SyncInterval, Flags );
}

bool findPresent( )
{
    HWND hwnd = CreateWindowA( "STATIC", "dummy", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, 0, 0, 0, 0 );

    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = 1;
    scDesc.BufferDesc.Width = 1;
    scDesc.BufferDesc.Height = 1;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = hwnd;
    scDesc.SampleDesc.Count = 1;
    scDesc.Windowed = TRUE;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device *pDevice = nullptr;
    ID3D11DeviceContext *pContext = nullptr;
    IDXGISwapChain *pSwap = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &scDesc,
        &pSwap,
        &pDevice,
        nullptr,
        &pContext
    );

    if ( !SUCCEEDED( hr ) ) return false;
    if ( !pSwap ) return false;

    void **vtable = *(void ***)pSwap;

    aPresent = (uint64_t)vtable[8];
    aResizeBuffers = (uint64_t)vtable[13];

    // cleanup
    pSwap->Release( );
    pContext->Release( );
    pDevice->Release( );
    DestroyWindow( hwnd );

    return true;
}

void cleanup( HMODULE hModule )
{
    if ( f ) fclose( f );
    printf( "[-] unhooking\n" );
    MH_DisableHook( reinterpret_cast<void *>( aIObjectInitalizer ) );
    MH_DisableHook( reinterpret_cast<void *>( aIObjectDeconstructor ) );
    MH_DisableHook( reinterpret_cast<void *>( aPresent ) );
    MH_Uninitialize( );
    printf( "[-] finished\n" );
    FreeConsole( );
    FreeLibraryAndExitThread( hModule, 0 );
}

void Thread( HMODULE hModule )
{
    if ( !findPresent( ) )
    {
        printf( "[-] failed to hook d3d11..." );
    }

    printf( "[+] present %llX\n", aPresent );

    MH_STATUS status = MH_CreateHook(
        (LPVOID)aPresent,
        &hkPresent,
        (void **)&oPresent
    );
    status = MH_EnableHook( (LPVOID)aPresent );

    status = MH_CreateHook(
        (LPVOID)aResizeBuffers,
        &hkResizeBuffers,
        (void **)&oResizeBuffers
    );
    status = MH_EnableHook( (LPVOID)aResizeBuffers );

    HMODULE hUser32 = GetModuleHandleW( L"user32.dll" );
    if ( !hUser32 ) hUser32 = LoadLibraryW( L"user32.dll" );
    if ( !hUser32 )
    {
        cleanup( hModule );
        return;
    }

    aGetRawInputData = (uint64_t)GetProcAddress( hUser32, "GetRawInputData" );

    status = MH_CreateHook(
        (LPVOID)aGetRawInputData,
        &hkGetRawInputData,
        (void **)&oGetRawInputData
    );

    printf( "[ntdll] created GetRawInputData hook %d\n", status );
    status = MH_EnableHook( (LPVOID)aGetRawInputData );
    printf( "[ntdll] created GetRawInputData hook %d\n", status );

    while ( !GetAsyncKeyState(VK_F6) )
    {
        
    }

    cleanup( hModule );
}


BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        AllocConsole( );
        freopen_s( &f, "CONOUT$", "w", stdout );
        freopen_s( &f, "CONIN$", "r", stdin );
        base = (uint64_t)GetModuleHandleA( NULL );
        printf( "[+] EngineBase %llX\n", base );

        printf( "[+] hooks\n" );


        if ( MH_Initialize( ) == MH_OK )
        {
            printf( "[+] minhook init success, game @ %llX\n", base );
            aIObjectInitalizer = bloodstrike::funcs::Messiah__IObject__Initalizer + base;
            aIObjectDeconstructor = bloodstrike::funcs::Messiah__IObject__Deconstructor + base;
            aProject = bloodstrike::funcs::Messiah_WorldToScreen + base;

            MH_STATUS status = MH_CreateHook(
                reinterpret_cast<void *>( aIObjectInitalizer ),
                &hkIObjectInitalizer,
                reinterpret_cast<void **>( &oIObjectInitalizer )
            );
            // printf( "Messiah__IEntity__Initalizer CreateHook %d\n", status );
            status = MH_EnableHook( reinterpret_cast<void *>( aIObjectInitalizer ) );
            // printf( "Messiah__IEntity__Initalizer EnableHook %d\n", status );
            
            status = MH_CreateHook(
                reinterpret_cast<void *>( aIObjectDeconstructor ),
                &hkIObjectDeconstructor,
                reinterpret_cast<void **>( &oIObjectDeconstructor )
            );
            status = MH_EnableHook( reinterpret_cast<void *>( aIObjectDeconstructor ) );
        }
        else
        {
            printf( "[-] failed to init minhook\n" );
        }

        CreateThread( NULL, NULL, (LPTHREAD_START_ROUTINE)Thread, hModule, NULL, NULL );

        break;

    case DLL_THREAD_ATTACH:

    case DLL_THREAD_DETACH:

    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

