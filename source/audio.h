#pragma once
#include "app.h"

typedef enum {
    SND_MOVE_SELF, SND_MOVE_OPP, SND_CAPTURE, SND_CASTLE, SND_CHECK, SND_PROMOTE, SND_START, SND_WIN, SND_LOSE,
    SND_DRAW, SND_LOW, SND_PREMOVE, SND_ILLEGAL, SND_NOTIFY, SND_CLICK, SND_COUNT
} SndId;

bool audio_init(void);
void audio_exit(void);
void audio_play(SndId id);        // honours g_set.sound / g_set.volume; never blocks
void audio_play_for_move(const Pos *before, Move m, const Pos *after, bool mine);   // picks capture/castle/check...
