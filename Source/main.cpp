// main.cpp - entry point.
#include "Application.h"
#include "Core/Log.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <exception>

#include <math.h>

int main()
{
#ifdef _WIN32
    // The C runtime picks faster maths routines on processors that have FMA3, and they round
    // the last bit differently. In a lockstep party two machines with different processors
    // would then compute different worlds from the same orders; with this they compute the
    // same one.
    _set_FMA3_enable(0);
    // The interface is authored in Russian; the console needs to be told.
    SetConsoleOutputCP(CP_UTF8);
#endif

    try
    {
        woc::Application application;
        return application.Run();
    }
    catch (const std::exception& error)
    {
        WOC_LOG_ERROR("Fatal error: ", error.what());
#ifdef _WIN32
        MessageBoxA(nullptr, error.what(), "World of Clans", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
