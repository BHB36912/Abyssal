#pragma once
#include "bitboard.h"
#include "types.h"
namespace Chess {
struct Magic {
    Bitboard mask;
    Bitboard* attacks;
    Bitboard magic;
    unsigned shift;
};
extern Magic RookMagics[64];
extern Magic BishopMagics[64];
void initMagic();
inline Bitboard attacksRook(Square sq, Bitboard occupied) {
    const Magic& m = RookMagics[sq];
    unsigned idx = unsigned(((occupied & m.mask) * m.magic) >> m.shift);
    return m.attacks[idx];
}
inline Bitboard attacksBishop(Square sq, Bitboard occupied) {
    const Magic& m = BishopMagics[sq];
    unsigned idx = unsigned(((occupied & m.mask) * m.magic) >> m.shift);
    return m.attacks[idx];
}
inline Bitboard attacksSlider(PieceType pt, Square sq, Bitboard occupied) {
    return pt == ROOK ? attacksRook(sq, occupied) : attacksBishop(sq, occupied);
}
}
