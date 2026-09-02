#include "stdafx.h"

#ifdef DEBUG
ECORE_API BOOL bDebug = FALSE;
#endif

#include <windows.h>
u32 GetScreenWidth()
{
    HDC hdc = GetDC(NULL);
    int width = GetDeviceCaps(hdc, HORZRES);
    ReleaseDC(NULL, hdc);
    return width > 0 ? static_cast<u32>(width) : 1024;
}

u32 GetScreenHeight()
{
    HDC hdc = GetDC(NULL);
    int height = GetDeviceCaps(hdc, VERTRES);
    ReleaseDC(NULL, hdc);
    return height > 0 ? static_cast<u32>(height) : 768;
}

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
    int* count = reinterpret_cast<int*>(dwData);
    (*count)++;
    return TRUE; // continue enumeration
}

int GetMonitorCount()
{
    int count = 0;
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(&count));
    return count;
}

int monitorCount = GetMonitorCount();

u32 psCurrentVidMode[2] = {
    (monitorCount != 1) ? 1024 : GetScreenWidth(),
    (monitorCount != 1) ? 768  : GetScreenHeight()
};


u32 psCurrentBPP = 32;
// release version always has "mt_*" enabled
Flags32 psDeviceFlags = {rsDetails | mtPhysics | mtSound | mtNetwork | rsDrawStatic | rsDrawDynamic};

// textures
int psTextureLOD = 0;
