#pragma once
/*---------------------------------------------------------------------------
    windows.h -- RXDK stub
    sqlite3 and other ported PC code detect _WIN32 and include <windows.h>.
    RXDK does not ship windows.h -- xtl.h is the equivalent.
    Place this file in the project root so the compiler finds it before
    any SDK include paths.
---------------------------------------------------------------------------*/
#ifndef _WINDOWS_
#define _WINDOWS_
#include <xtl.h>
#endif