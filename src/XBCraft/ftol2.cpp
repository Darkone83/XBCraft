/*---------------------------------------------------------------------------
    ftol2.cpp
    Provides __ftol2_sse for the whole project.

    MSVC generates calls to __ftol2_sse for implicit float->int casts.
    The Xbox Pentium III has no SSE2, so the CRT stub is absent in RXDK.
    This x87 FISTP implementation satisfies the linker for every TU that
    generates the call (stb_truetype, minimp3, any user code with (int)f).

    THIS FILE MUST BE COMPILED WITH WHOLE PROGRAM OPTIMIZATION OFF.
    Right-click ftol2.cpp -> Properties -> C/C++ -> Optimization ->
    Whole Program Optimization -> No.
    All other files can keep /GL.  This file is the one exception.
---------------------------------------------------------------------------*/

#pragma warning( disable : 4731 )

extern "C" __declspec(naked) void __cdecl _ftol2_sse(void)
{
    __asm
    {
        push    ebp
        mov     ebp, esp
        sub     esp, 4
        fistp   dword ptr[ebp - 4]
        mov     eax, dword ptr[ebp - 4]
        mov     esp, ebp
        pop     ebp
        ret
    }
}

#pragma warning( default : 4731 )