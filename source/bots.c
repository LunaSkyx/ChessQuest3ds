#include "bots.h"

const Bot BOTS[BOT_COUNT] = {
    {"Pip",              "Just learned how the pieces move.",   {1,   50, 600}, 1, 0xE8B04A},
    {"Mochi",            "Loves giving pieces away.",           {1,   80, 350}, 1, 0xE58AB0},
    {"Clara",            "Plays fast and thinks later.",        {1,  100, 200}, 1, 0x7CC4E8},
    {"Benny Bishop",     "Likes long diagonals.",               {2,  150,  90}, 2, 0x9AD66B},
    {"Olive",            "Careful, but a little naive.",        {2,  200,  40}, 2, 0x8FB08A},
    {"Rook Rick",        "Knows a few nasty traps.",            {3,  400,  15}, 3, 0xD9805A},
    {"Nova",             "Tactical and tidy.",                  {3,  500,   0}, 3, 0xB08AE8},
    {"Professor Quill",  "Never drops a piece for free.",       {4,  800,   0}, 4, 0x6AA0D8},
    {"Captain Castle",   "Builds a fortress and waits.",        {5, 1500,   0}, 4, 0xC4A25A},
    {"Zara Zwischenzug", "Spots every tactic.",                 {6, 2500,   0}, 5, 0xE0605A},
    {"Gizmo 3000",       "A very patient machine.",             {7, 4000,   0}, 5, 0x8A9098},
    {"Lady Checkmate",   "The final boss.",                     {8, 5000,   0}, 5, 0x4A3A6A},
};
