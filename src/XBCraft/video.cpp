/*---------------------------------------------------------------------------
    ScorchedXB - video.cpp
    XMV intro playback via XMVDecoder_Play (official XDK path).

    Research confirms XMVDecoder_Play is the correct official API — the XDK
    ships with XMV samples that use exactly this call.  GetNextFrame is for
    custom frame-pump scenarios and bugchecked with our surface setup.

    If RXDK does not fully wire up the D3D overlay subsystem that Play()
    depends on, video will silently produce no output even though audio
    works.  In that case s_bVideoWorked stays FALSE and the caller can
    log or skip.  Either way the title screen follows cleanly.

    Flow:
        1. CreateDecoderForFile
        2. EnableAudioStream 0 (ADPCM stereo)
        3. Clear + Present once  — get D3D into a clean presented state
        4. EnableOverlay(TRUE)   — open the overlay layer
        5. Play(NULL)            — blocks; overlay updates internally
        6. EnableOverlay(FALSE)  — close overlay before normal rendering
        7. Clear + Present once  — wipe any overlay residue from display
        8. CloseDecoder
---------------------------------------------------------------------------*/

#include <xtl.h>
#include "xmv.h"
#include "video.h"
#include "render.h"
#include "input.h"

static XMVDecoder* s_pDecoder = NULL;
static BOOL        s_bVideoWorked = FALSE;
static LONG        s_bDSWorkActive = 0;

/* DirectSoundDoWork pump thread */
static DWORD WINAPI DSWorkThread(LPVOID /*pContext*/)
{
    while (InterlockedCompareExchange(&s_bDSWorkActive, 0, 0))
    {
        DirectSoundDoWork();
        Sleep(4);
    }
    return 0;
}

/* Input poll thread -- calls TerminatePlayback on any button press */
static DWORD WINAPI SkipThread(LPVOID /*pContext*/)
{
    while (InterlockedCompareExchange(&s_bDSWorkActive, 0, 0))
    {
        PumpInput();
        if (GetButtons())
        {
            XMVDecoder_TerminatePlayback(s_pDecoder);
            break;
        }
        Sleep(16);
    }
    return 0;
}

BOOL Video_LastPlaySucceeded(void)
{
    return s_bVideoWorked;
}

void Video_PlayBlocking(const char* pszPath)
{
    HRESULT hr;

    s_bVideoWorked = FALSE;

    hr = XMVDecoder_CreateDecoderForFile(XMVFLAG_NONE, pszPath, &s_pDecoder);
    if (FAILED(hr) || !s_pDecoder)
        return;

    /* Enable audio stream 0 — ADPCM stereo, default mix bins.
       DirectSoundCreate called in Render_Init provides the DS device.  */
    XMVDecoder_EnableAudioStream(s_pDecoder, 0, 0, NULL, NULL);

    /* Get D3D into a clean presented state before overlay opens */
    g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);
    g_pd3dDevice->Present(NULL, NULL, NULL, NULL);

    /* Start DSWork pump + skip-input threads before Play() blocks */
    HANDLE hDSWork, hSkip;
    DWORD  dwThreadId;
    InterlockedExchange(&s_bDSWorkActive, 1);

    hDSWork = CreateThread(NULL, 0, DSWorkThread, NULL, 0, &dwThreadId);
    if (hDSWork)
        SetThreadPriority(hDSWork, THREAD_PRIORITY_HIGHEST);

    hSkip = CreateThread(NULL, 0, SkipThread, NULL, 0, &dwThreadId);
    if (hSkip)
        SetThreadPriority(hSkip, THREAD_PRIORITY_ABOVE_NORMAL);

    /* Open overlay layer — required for Play() to show video */
    D3DDevice_EnableOverlay(TRUE);

    /* Blocking play -- SkipThread calls TerminatePlayback on button press */
    hr = XMVDecoder_Play(s_pDecoder, XMVFLAG_NONE, NULL);

    /* Stop both threads */
    InterlockedExchange(&s_bDSWorkActive, 0);
    if (hDSWork) { WaitForSingleObject(hDSWork, 1000); CloseHandle(hDSWork); }
    if (hSkip) { WaitForSingleObject(hSkip, 1000); CloseHandle(hSkip); }

    s_bVideoWorked = SUCCEEDED(hr);

    /* Close overlay before any normal D3D rendering resumes */
    D3DDevice_EnableOverlay(FALSE);

    /* Wipe overlay residue from display */
    g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);
    g_pd3dDevice->Present(NULL, NULL, NULL, NULL);

    XMVDecoder_CloseDecoder(s_pDecoder);
    s_pDecoder = NULL;
}