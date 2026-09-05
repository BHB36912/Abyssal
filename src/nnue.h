#pragma once
#include <cstdint>
#include <cstring>
#include "types.h"
namespace Chess {
constexpr int KING_ZONES = 10;
constexpr int KING_ZONE_STRIDE = 768;
constexpr int OUT_BUCKETS = 8;
constexpr int IN_DIM = KING_ZONES * KING_ZONE_STRIDE;
constexpr int HID = 512;
constexpr int EVAL_SCALE = 400;
constexpr int QA = 256;
constexpr int QB = 256;
using NetW = int16_t;
constexpr int16_t ACT_MIN = 0;
constexpr int16_t ACT_MAX = QA;
constexpr int KING_ZONE[64] = {
    0, 1, 2, 3, 3, 2, 1, 0,
    4, 5, 6, 7, 7, 6, 5, 4,
    8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8,
    9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9
};
inline int featureIndex(PieceType pt, Color col, Square sq, Color persp, Square ksq) {
    return (col == persp ? 0 : 384)
           + 64 * pt
           + (persp == WHITE ? sq : Square(sq ^ 56))
           + KING_ZONE[persp == WHITE ? ksq : Square(ksq ^ 56)] * KING_ZONE_STRIDE;
}
inline int outputBucket(int pieceCount) { return (pieceCount - 2) / 4; }
struct NnueState {
    alignas(32) int16_t accum[HID * 2];
    void add(PieceType pt, Square sq, Color col, Square wking, Square bking);
    void remove(PieceType pt, Square sq, Color col, Square wking, Square bking);
    void update(PieceType pt, Square from, Square to, Color col, Square wking, Square bking);
    void rebuild(const Piece* board, Square wking, Square bking);
    int evaluate(Color col, int pieceCount) const;
    void zero() { std::memset(accum, 0, sizeof(accum)); }
};
void initNet();
}
