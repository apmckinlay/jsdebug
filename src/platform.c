/* Copyright 2014 (c) Suneido Software Corp. All rights reserved.
 * Licensed under GPLv2.
 */

//==============================================================================
// file: platform.c
// auth: Victor Schappert
// date: 20140608
// desc: Contains platform-specific/OS-specific shared library entry-point code.
//==============================================================================

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4100) // unreferenced formal parameter
#endif // _MSC_VER

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        // A process is loading the DLL.
        break;
    case DLL_THREAD_ATTACH:
        // A process is creating a new thread.
        break;
    case DLL_THREAD_DETACH:
        // A thread exits normally.
        break;
    case DLL_PROCESS_DETACH:
        // A process unloads the DLL.
        break;
    }
    return TRUE;
}

#ifdef _MSC_VER
#pragma warning (pop)
#endif // _MSC_VER

#endif // _WIN32
