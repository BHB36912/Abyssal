#include "evaluate.h"
#include "magic.h"
#include <algorithm>
namespace Chess {
int evaluate(const Board& b) {
    if (b.kingSquare[WHITE] >= 64 || b.kingSquare[BLACK] >= 64) return 0;
    return b.nnue.evaluate(b.sideToMove, popcount(b.occupied));
}
int see(const Board& b, Move m) {
    Square from = fromSq(m);
    Square to = toSq(m);
    PieceType attacker = typeOf(b.pieceOn[from]);
    if (attacker == NO_PIECE_TYPE) return 0;
    bool ep = isEnPassant(m);
    int victimValue = 0;
    if (ep) victimValue = PieceValue[PAWN];
    else if (b.pieceOn[to] != NO_PIECE)
        victimValue = PieceValue[typeOf(b.pieceOn[to])];
    if (victimValue == 0) return 0;
    Color us = colorOf(b.pieceOn[from]);
    Color them = Color(us ^ 1);
    if (attacker == KING) {
        Bitboard occ2 = b.occupied ^ from ^ to;
        Bitboard defenders = b.attackersTo(to, occ2) & b.pieces(them);
        return defenders ? victimValue - 20000 : victimValue;
    }
    int gain[64];
    int d = 0;
    gain[0] = victimValue;
    Bitboard occupied = b.occupied ^ from;
    if (ep) {
        Square capSq = (us == WHITE) ? Square(to - 8) : Square(to + 8);
        occupied ^= capSq;
    }
    occupied |= to;
    Bitboard attackers = b.attackersTo(to, occupied) & occupied;
    std::swap(us, them);
    while (true) {
        Bitboard att = attackers & b.pieces(us);
        if (att == 0) break;
        PieceType nextAttacker = KING;
        for (int pt = PAWN; pt <= KING; pt++) {
            if (att & b.pieces(us, PieceType(pt))) {
                nextAttacker = PieceType(pt);
                break;
            }
        }
        if (nextAttacker == KING && (attackers & b.pieces(them))) break;
        if (d >= 62) break;
        d++;
        gain[d] = (attacker == KING ? 20000 : PieceValue[attacker]) - gain[d - 1];
        Square attSq = lsb(att & b.pieces(us, nextAttacker));
        occupied ^= attSq;
        attacker = nextAttacker;
        attackers = b.attackersTo(to, occupied) & occupied;
        std::swap(us, them);
    }
    while (d > 0) {
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
        d--;
    }
    return gain[0];
}
}
