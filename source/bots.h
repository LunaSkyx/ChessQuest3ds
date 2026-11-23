// Offline bot ladder: named opponents of rising strength; beating one unlocks the next
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "engine.h"

typedef struct {
    const char *name;
    const char *tag;            // one-line personality
    EngLimits lim;              // how the built-in engine plays for this bot
    int stars;                  // 1..5 difficulty
    uint32_t color;             // portrait colour 0xRRGGBB
} Bot;

#define BOT_COUNT 12
extern const Bot BOTS[BOT_COUNT];

static inline bool bot_is_beaten(uint32_t mask, int i) { return i >= 0 && i < BOT_COUNT && (mask >> i & 1u); }
static inline bool bot_is_unlocked(uint32_t mask, int i) { return i == 0 || (i > 0 && i < BOT_COUNT && bot_is_beaten(mask, i - 1)); }
static inline int bot_beaten_count(uint32_t mask) { int n = 0; for (int i = 0; i < BOT_COUNT; i++) n += (int)(mask >> i & 1u); return n; }
static inline int bot_next_target(uint32_t mask) { for (int i = 0; i < BOT_COUNT; i++) if (!bot_is_beaten(mask, i)) return i; return BOT_COUNT - 1; }
