#ifndef SCORCHEDXB_VIDEO_H
#define SCORCHEDXB_VIDEO_H

/*---------------------------------------------------------------------------
    ScorchedXB - video.h
    XMV intro playback.

    Video_PlayBlocking() runs XMVDecoder_Play on the CALLING (main) thread.
    This is required because:
      - XMVDecoder_Play uses the D3D overlay which is thread-affine to where
        D3D was initialized.
      - Xbox ADPCM audio decode needs full hardware priority; a competing
        main thread causes choppy audio.

    Skip detection runs on a tiny side thread using XInputGetState directly
    (not PumpInput) so there is no input-system conflict.  TerminatePlayback
    is documented as thread-safe.

    If the file is missing or the decoder fails, the function returns
    immediately with no video played — caller should advance state normally.
---------------------------------------------------------------------------*/

void Video_PlayBlocking(const char* pszPath);
BOOL Video_LastPlaySucceeded(void);  /* diagnostic: FALSE = overlay likely unsupported */

#endif /* SCORCHEDXB_VIDEO_H */