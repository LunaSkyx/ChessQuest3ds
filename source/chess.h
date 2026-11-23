// Portable chess rules for all Lichess variants. No 3DS dependencies (host-testable).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    VAR_STANDARD, VAR_CHESS960, VAR_KOTH, VAR_3CHECK, VAR_ANTICHESS, VAR_ATOMIC, VAR_HORDE, VAR_RACING,
    VAR_CRAZYHOUSE, VAR_COUNT
} Variant;

enum { WHITE = 0, BLACK = 1 };
enum { NONE = 0, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
#define MKPC(c, t) ((uint8_t)((t) | ((c) << 3)))
#define PTYPE(p) ((p) & 7)
#define PCOLOR(p) ((p) >> 3)
#define SQ(f, r) ((r) * 8 + (f))
#define FILE_OF(s) ((s) & 7)
#define RANK_OF(s) ((s) >> 3)
#define NO_SQ 64

// move flags
enum { MF_CAPTURE = 1, MF_EP = 2, MF_CASTLE = 4, MF_DOUBLE = 8, MF_DROP = 16 };

typedef struct {
    uint8_t from;   // origin square; for drops: the piece type being dropped
    uint8_t to;     // destination square; for castling: the KING's destination (g/c file)
    uint8_t promo;  // promotion piece type or 0
    uint8_t flags;
} Move;

typedef struct {
    uint8_t b[64];          // a1 = 0 ... h8 = 63; 0 = empty
    uint8_t turn;
    uint8_t ep;             // en-passant target square or NO_SQ
    uint8_t castle;         // 1 = white O-O, 2 = white O-O-O, 4 = black O-O, 8 = black O-O-O
    uint8_t rook_file[4];   // castling rook files [WK, WQ, BK, BQ]
    uint8_t king_file[2];   // castling king start files
    uint8_t variant;
    uint8_t checks[2];      // checks given so far by [white, black] (three-check)
    uint8_t pocket[2][6];   // crazyhouse pockets, index = piece type - 1
    uint64_t promoted;      // crazyhouse: squares holding promoted pieces
    uint16_t halfmove, fullmove;
} Pos;

#define MAX_MOVES 512

typedef enum { OUT_ONGOING, OUT_CHECKMATE, OUT_STALEMATE, OUT_VARIANT_WIN, OUT_DRAW_MATERIAL } OutKind;

// --- setup / serialisation
void pos_init(Pos *p, Variant v);                                   // standard start position of the variant
bool pos_from_fen(Pos *p, const char *fen, Variant v);
void pos_to_fen(const Pos *p, char *out, size_t n);
const char *variant_key(Variant v);                                 // Lichess API key ("kingOfTheHill" ...)
const char *variant_name(Variant v);                                // display name
bool variant_from_key(const char *key, Variant *out);

// --- rules
int pos_legal_moves(const Pos *p, Move *out);                       // all legal moves, returns count
int pos_moves_from(const Pos *p, int sq, Move *out);                // legal moves of the piece on sq
int pos_drops(const Pos *p, int type, Move *out);                   // legal drops of a pocket piece
void pos_do(Pos *p, Move m);                                        // play a (legal) move
bool pos_in_check(const Pos *p, int color);                         // variant-aware
OutKind pos_outcome(const Pos *p, int *winner);                     // winner: WHITE/BLACK or -1 for draw/none
bool sq_attacked(const Pos *p, int sq, int by);                     // plain attack test

// --- notation
void move_to_uci(const Pos *p, Move m, char *out);                  // out >= 8 bytes
bool pos_parse_uci(const Pos *p, const char *uci, Move *out);       // accepts both castling notations
void move_to_san(const Pos *p, Move m, char *out, size_t n);
bool move_eq(Move a, Move b);
bool pos_parse_san(const Pos *p, const char *san, Move *out);        // SAN ("Nf3", "exd5", "O-O", "e8=Q+") for a legal move
int  sq_from_name(const char *s);                                   // "e4" -> 28, or -1
void sq_name(int sq, char *out);                                    // 3 bytes
int  castle_rook_sq(const Pos *p, Move m);                          // rook origin for a castling move

// --- helpers
int  pos_king_sq(const Pos *p, int color);
int  material_value(int type);
