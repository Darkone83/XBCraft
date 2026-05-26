#ifndef CRAFTXB_AUDIO_H
#define CRAFTXB_AUDIO_H

/*---------------------------------------------------------------------------
    CraftXB - audio.h
    DirectSound SFX pool + MP3 music streaming.

    Two looping music tracks, both on D: (XBE root):
        D:\track0.mp3   title / menu theme
        D:\track1.mp3   overworld theme

    Streaming design: 256KB ring buffer split into two 128KB halves.
    Background thread fills the dead half; both tracks loop indefinitely.
---------------------------------------------------------------------------*/

#define AUDIO_SFX_MAX   8

void  Audio_Init(void);
void  Audio_Update(void);
void  Audio_Shutdown(void);

/* --- music -------------------------------------------------------------- */
void  Audio_MusicPlayTitle(void);       /* track0 -- title / menu, loops    */
void  Audio_MusicPlayOverworld(void);   /* track1 -- overworld theme, loops */
void  Audio_MusicStop(void);
void  Audio_MusicVolume(int nVol);      /* 0..100                           */
int   Audio_MusicIsPlaying(void);

/* --- sfx ---------------------------------------------------------------- */
int   Audio_SfxLoad(const char* pszPath);    /* returns slot index or -1   */
void  Audio_SfxPlay(int iSlot);
void  Audio_SfxPlayLooping(int iSlot);
void  Audio_SfxStop(int iSlot);
void  Audio_SfxVolume(int iSlot, int nVol);  /* 0..100                     */

#endif /* CRAFTXB_AUDIO_H */