#include "movegen.h"
#include "magic.h"
#include <iostream>
namespace Chess {
template<Color Us>
static inline void generatePieceMoves(const Board& b, MoveList& list, PieceType pt, Bitboard target) {
    Bitboard pieces = b.pieces(Us, pt);
    while (pieces) {
        Square from = pop_lsb(pieces);
        Bitboard attacks = 0;
        if (pt == KNIGHT) attacks = KnightAttacks[from] & target;
        else if (pt == BISHOP) attacks = attacksBishop(from, b.occupied) & target;
        else if (pt == ROOK) attacks = attacksRook(from, b.occupied) & target;
        else if (pt == QUEEN)
            attacks = (attacksBishop(from, b.occupied) | attacksRook(from, b.occupied)) & target;
        else if (pt == KING) attacks = KingAttacks[from] & target;
        while (attacks) {
            Square to = pop_lsb(attacks);
            list.add(makeMove(from, to));
        }
    }
}
static inline void addPromotions(MoveList& list, Square from, Square to) {
    list.add(makePromo(from, to, PieceType(0)));
    list.add(makePromo(from, to, PieceType(1)));
    list.add(makePromo(from, to, PieceType(2)));
    list.add(makePromo(from, to, PieceType(3)));
}
template<Color Us>
static inline void generatePawnMoves(const Board& b, MoveList& list, Bitboard target, bool capturesOnly) {
    constexpr Color Them = (Us == WHITE) ? BLACK : WHITE;
    constexpr Direction Up = (Us == WHITE) ? NORTH : SOUTH;
    constexpr Direction UpRight = (Us == WHITE) ? NORTH_EAST : SOUTH_EAST;
    constexpr Direction UpLeft = (Us == WHITE) ? NORTH_WEST : SOUTH_WEST;
    constexpr Bitboard RANK_PROMO = (Us == WHITE) ? RANK_8_BB : RANK_1_BB;
    constexpr Bitboard START_RANK = (Us == WHITE) ? RANK_2_BB : RANK_7_BB;
    Bitboard pawns = b.pieces(Us, PAWN);
    Bitboard empty = ~b.occupied;
    Bitboard enemies = b.pieces(Them);
    if (!capturesOnly) {
        Bitboard singlePush = shift<Up>(pawns) & empty & target;
        Bitboard promoPush = singlePush & RANK_PROMO;
        Bitboard normalPush = singlePush & ~RANK_PROMO;
        while (normalPush) {
            Square to = pop_lsb(normalPush);
            list.add(makeMove(Square(to - Up), to));
        }
        while (promoPush) {
            Square to = pop_lsb(promoPush);
            Square from = Square(to - Up);
            addPromotions(list, from, to);
        }
        Bitboard startPawns = pawns & START_RANK;
        Bitboard pushOne = shift<Up>(startPawns) & empty;
        Bitboard doublePush = shift<Up>(pushOne) & empty & target;
        while (doublePush) {
            Square to = pop_lsb(doublePush);
            list.add(makeMove(Square(to - Up - Up), to));
        }
    }
    Bitboard captureTarget = enemies & target;
    Bitboard leftCaps = shift<UpLeft>(pawns) & captureTarget;
    Bitboard rightCaps = shift<UpRight>(pawns) & captureTarget;
    while (leftCaps) {
        Square to = pop_lsb(leftCaps);
        Square from = Square(to - UpLeft);
        if ((RANK_PROMO & squareBB(to))) {
            addPromotions(list, from, to);
        } else {
            list.add(makeMove(from, to));
        }
    }
    while (rightCaps) {
        Square to = pop_lsb(rightCaps);
        Square from = Square(to - UpRight);
        if ((RANK_PROMO & squareBB(to))) {
            addPromotions(list, from, to);
        } else {
            list.add(makeMove(from, to));
        }
    }
    if (b.epSquare != SQ_NONE) {
        Bitboard epAttackers = PawnAttacks[Them][b.epSquare] & pawns;
        while (epAttackers) {
            Square from = pop_lsb(epAttackers);
            list.add(makeEnPassant(from, b.epSquare));
        }
    }
}
template<Color Us>
static inline void generateCastling(const Board& b, MoveList& list) {
    if (b.inCheck(Us)) return;
    Color them = Color(Us ^ 1);
    if constexpr (Us == WHITE) {
        if ((b.castling & WHITE_OO) && !(b.occupied & (squareBB(SQ_F1) | squareBB(SQ_G1)))) {
            if (!b.isSquareAttacked(SQ_E1, them) && !b.isSquareAttacked(SQ_F1, them) && !b.isSquareAttacked(SQ_G1, them))
                list.add(makeCastling(SQ_E1, SQ_G1));
        }
        if ((b.castling & WHITE_OOO) && !(b.occupied & (squareBB(SQ_B1) | squareBB(SQ_C1) | squareBB(SQ_D1)))) {
            if (!b.isSquareAttacked(SQ_E1, them) && !b.isSquareAttacked(SQ_D1, them) && !b.isSquareAttacked(SQ_C1, them))
                list.add(makeCastling(SQ_E1, SQ_C1));
        }
    } else {
        if ((b.castling & BLACK_OO) && !(b.occupied & (squareBB(SQ_F8) | squareBB(SQ_G8)))) {
            if (!b.isSquareAttacked(SQ_E8, them) && !b.isSquareAttacked(SQ_F8, them) && !b.isSquareAttacked(SQ_G8, them))
                list.add(makeCastling(SQ_E8, SQ_G8));
        }
        if ((b.castling & BLACK_OOO) && !(b.occupied & (squareBB(SQ_B8) | squareBB(SQ_C8) | squareBB(SQ_D8)))) {
            if (!b.isSquareAttacked(SQ_E8, them) && !b.isSquareAttacked(SQ_D8, them) && !b.isSquareAttacked(SQ_C8, them))
                list.add(makeCastling(SQ_E8, SQ_C8));
        }
    }
}
void generateEvasions(const Board& b, MoveList& list) {
    Color us = b.sideToMove;
    Color them = Color(us ^ 1);
    Square ks = b.kingSquare[us];
    Bitboard checkers = b.checkers;
    Bitboard kingMoves = KingAttacks[ks] & ~b.pieces(us);
    while (kingMoves) {
        Square to = pop_lsb(kingMoves);
        Bitboard occ = b.occupied ^ squareBB(ks);
        Bitboard attackers = b.attackersTo(to, them, occ);
        if (squareBB(to) & checkers) {
            occ ^= squareBB(to);
            attackers = b.attackersTo(to, them, occ);
        }
        if (attackers == 0) {
            list.add(makeMove(ks, to));
        }
    }
    if (popcount(checkers) == 1) {
        Square cs = lsb(checkers);
        Bitboard blockSquares = b.blockBB;
        if (us == WHITE) {
            Bitboard pawns = b.pieces(WHITE, PAWN);
            Bitboard empty = ~b.occupied;
            Bitboard singlePush = shift<NORTH>(pawns) & empty & blockSquares;
            while (singlePush) {
                Square to = pop_lsb(singlePush);
                if (rankOf(to) == 7) addPromotions(list, Square(to - NORTH), to);
                else list.add(makeMove(Square(to - NORTH), to));
            }
            Bitboard startPawns = pawns & RANK_2_BB;
            Bitboard pushOne = shift<NORTH>(startPawns) & empty;
            Bitboard doublePush = shift<NORTH>(pushOne) & empty & blockSquares;
            while (doublePush) {
                Square to = pop_lsb(doublePush);
                list.add(makeMove(Square(to - NORTH - NORTH), to));
            }
            Bitboard enemies = b.pieces(BLACK);
            Bitboard capTarget = blockSquares & (enemies | (b.epSquare != SQ_NONE ? squareBB(b.epSquare) : 0ULL));
            Bitboard leftCaps = shift<NORTH_WEST>(pawns) & capTarget;
            while (leftCaps) {
                Square to = pop_lsb(leftCaps);
                Square from = Square(to - NORTH_WEST);
                if (rankOf(to) == 7) addPromotions(list, from, to);
                else if (to == b.epSquare) list.add(makeEnPassant(from, to));
                else list.add(makeMove(from, to));
            }
            Bitboard rightCaps = shift<NORTH_EAST>(pawns) & capTarget;
            while (rightCaps) {
                Square to = pop_lsb(rightCaps);
                Square from = Square(to - NORTH_EAST);
                if (rankOf(to) == 7) addPromotions(list, from, to);
                else if (to == b.epSquare) list.add(makeEnPassant(from, to));
                else list.add(makeMove(from, to));
            }
            if (b.epSquare != SQ_NONE) {
                Square capPawnSq = Square(b.epSquare - NORTH);
                if (squareBB(capPawnSq) & checkers) {
                    Bitboard epAttackers = PawnAttacks[BLACK][b.epSquare] & pawns;
                    while (epAttackers) {
                        Square from = pop_lsb(epAttackers);
                        list.add(makeEnPassant(from, b.epSquare));
                    }
                }
            }
            generatePieceMoves<WHITE>(b, list, KNIGHT, blockSquares);
            generatePieceMoves<WHITE>(b, list, BISHOP, blockSquares);
            generatePieceMoves<WHITE>(b, list, ROOK, blockSquares);
            generatePieceMoves<WHITE>(b, list, QUEEN, blockSquares);
        } else {
            Bitboard pawns = b.pieces(BLACK, PAWN);
            Bitboard empty = ~b.occupied;
            Bitboard singlePush = shift<SOUTH>(pawns) & empty & blockSquares;
            while (singlePush) {
                Square to = pop_lsb(singlePush);
                if (rankOf(to) == 0) addPromotions(list, Square(to - SOUTH), to);
                else list.add(makeMove(Square(to - SOUTH), to));
            }
            Bitboard startPawns = pawns & RANK_7_BB;
            Bitboard pushOne = shift<SOUTH>(startPawns) & empty;
            Bitboard doublePush = shift<SOUTH>(pushOne) & empty & blockSquares;
            while (doublePush) {
                Square to = pop_lsb(doublePush);
                list.add(makeMove(Square(to - SOUTH - SOUTH), to));
            }
            Bitboard enemies = b.pieces(WHITE);
            Bitboard capTarget = blockSquares & (enemies | (b.epSquare != SQ_NONE ? squareBB(b.epSquare) : 0ULL));
            Bitboard leftCaps = shift<SOUTH_WEST>(pawns) & capTarget;
            while (leftCaps) {
                Square to = pop_lsb(leftCaps);
                Square from = Square(to - SOUTH_WEST);
                if (rankOf(to) == 0) addPromotions(list, from, to);
                else if (to == b.epSquare) list.add(makeEnPassant(from, to));
                else list.add(makeMove(from, to));
            }
            Bitboard rightCaps = shift<SOUTH_EAST>(pawns) & capTarget;
            while (rightCaps) {
                Square to = pop_lsb(rightCaps);
                Square from = Square(to - SOUTH_EAST);
                if (rankOf(to) == 0) addPromotions(list, from, to);
                else if (to == b.epSquare) list.add(makeEnPassant(from, to));
                else list.add(makeMove(from, to));
            }
            if (b.epSquare != SQ_NONE) {
                Square capPawnSq = Square(b.epSquare - SOUTH);
                if (squareBB(capPawnSq) & checkers) {
                    Bitboard epAttackers = PawnAttacks[WHITE][b.epSquare] & pawns;
                    while (epAttackers) {
                        Square from = pop_lsb(epAttackers);
                        list.add(makeEnPassant(from, b.epSquare));
                    }
                }
            }
            generatePieceMoves<BLACK>(b, list, KNIGHT, blockSquares);
            generatePieceMoves<BLACK>(b, list, BISHOP, blockSquares);
            generatePieceMoves<BLACK>(b, list, ROOK, blockSquares);
            generatePieceMoves<BLACK>(b, list, QUEEN, blockSquares);
        }
    }
}
void generateMoves(const Board& b, MoveList& list) {
    list.clear();
    if (b.inCheck()) {
        generateEvasions(b, list);
        return;
    }
    Color us = b.sideToMove;
    Bitboard target = ~b.pieces(us);
    if (us == WHITE) {
        generatePawnMoves<WHITE>(b, list, target, false);
        generatePieceMoves<WHITE>(b, list, KNIGHT, target);
        generatePieceMoves<WHITE>(b, list, BISHOP, target);
        generatePieceMoves<WHITE>(b, list, ROOK, target);
        generatePieceMoves<WHITE>(b, list, QUEEN, target);
        generatePieceMoves<WHITE>(b, list, KING, target);
        generateCastling<WHITE>(b, list);
    } else {
        generatePawnMoves<BLACK>(b, list, target, false);
        generatePieceMoves<BLACK>(b, list, KNIGHT, target);
        generatePieceMoves<BLACK>(b, list, BISHOP, target);
        generatePieceMoves<BLACK>(b, list, ROOK, target);
        generatePieceMoves<BLACK>(b, list, QUEEN, target);
        generatePieceMoves<BLACK>(b, list, KING, target);
        generateCastling<BLACK>(b, list);
    }
}
void generateCaptures(const Board& b, MoveList& list) {
    list.clear();
    Color us = b.sideToMove;
    if (b.inCheck()) {
        generateEvasions(b, list);
        int newCount = 0;
        for (int i = 0; i < list.count; i++) {
            Move m = list.moves[i];
            Square to = toSq(m);
            Square from = fromSq(m);
            if (b.pieceOn[to] != NO_PIECE || isPromo(m) || isEnPassant(m)) {
                list.moves[newCount++] = m;
            }
        }
        list.count = newCount;
        return;
    }
    Bitboard target = b.pieces(Color(us ^ 1));
    if (us == WHITE) {
        generatePawnMoves<WHITE>(b, list, target, true);
        Bitboard pawns = b.pieces(WHITE, PAWN);
        Bitboard empty = ~b.occupied;
        Bitboard singlePush = shift<NORTH>(pawns) & empty & RANK_8_BB;
        while (singlePush) {
            Square to = pop_lsb(singlePush);
            Square from = Square(to - NORTH);
            addPromotions(list, from, to);
        }
        generatePieceMoves<WHITE>(b, list, KNIGHT, target);
        generatePieceMoves<WHITE>(b, list, BISHOP, target);
        generatePieceMoves<WHITE>(b, list, ROOK, target);
        generatePieceMoves<WHITE>(b, list, QUEEN, target);
        generatePieceMoves<WHITE>(b, list, KING, target);
    } else {
        generatePawnMoves<BLACK>(b, list, target, true);
        Bitboard pawns = b.pieces(BLACK, PAWN);
        Bitboard empty = ~b.occupied;
        Bitboard singlePush = shift<SOUTH>(pawns) & empty & RANK_1_BB;
        while (singlePush) {
            Square to = pop_lsb(singlePush);
            Square from = Square(to - SOUTH);
            addPromotions(list, from, to);
        }
        generatePieceMoves<BLACK>(b, list, KNIGHT, target);
        generatePieceMoves<BLACK>(b, list, BISHOP, target);
        generatePieceMoves<BLACK>(b, list, ROOK, target);
        generatePieceMoves<BLACK>(b, list, QUEEN, target);
        generatePieceMoves<BLACK>(b, list, KING, target);
    }
}
void generateQuiet(const Board& b, MoveList& list) {
    list.clear();
    if (b.inCheck()) return;
    Color us = b.sideToMove;
    Bitboard target = ~b.occupied;
    if (us == WHITE) {
        Bitboard pawns = b.pieces(WHITE, PAWN);
        Bitboard empty = ~b.occupied;
        Bitboard singlePush = shift<NORTH>(pawns) & empty & ~RANK_8_BB;
        while (singlePush) {
            Square to = pop_lsb(singlePush);
            list.add(makeMove(Square(to - NORTH), to));
        }
        Bitboard startPawns = pawns & RANK_2_BB;
        Bitboard pushOne = shift<NORTH>(startPawns) & empty;
        Bitboard doublePush = shift<NORTH>(pushOne) & empty;
        while (doublePush) {
            Square to = pop_lsb(doublePush);
            list.add(makeMove(Square(to - NORTH - NORTH), to));
        }
        generatePieceMoves<WHITE>(b, list, KNIGHT, target);
        generatePieceMoves<WHITE>(b, list, BISHOP, target);
        generatePieceMoves<WHITE>(b, list, ROOK, target);
        generatePieceMoves<WHITE>(b, list, QUEEN, target);
        generatePieceMoves<WHITE>(b, list, KING, target);
        generateCastling<WHITE>(b, list);
    } else {
        Bitboard pawns = b.pieces(BLACK, PAWN);
        Bitboard empty = ~b.occupied;
        Bitboard singlePush = shift<SOUTH>(pawns) & empty & ~RANK_1_BB;
        while (singlePush) {
            Square to = pop_lsb(singlePush);
            list.add(makeMove(Square(to - SOUTH), to));
        }
        Bitboard startPawns = pawns & RANK_7_BB;
        Bitboard pushOne = shift<SOUTH>(startPawns) & empty;
        Bitboard doublePush = shift<SOUTH>(pushOne) & empty;
        while (doublePush) {
            Square to = pop_lsb(doublePush);
            list.add(makeMove(Square(to - SOUTH - SOUTH), to));
        }
        generatePieceMoves<BLACK>(b, list, KNIGHT, target);
        generatePieceMoves<BLACK>(b, list, BISHOP, target);
        generatePieceMoves<BLACK>(b, list, ROOK, target);
        generatePieceMoves<BLACK>(b, list, QUEEN, target);
        generatePieceMoves<BLACK>(b, list, KING, target);
        generateCastling<BLACK>(b, list);
    }
}
uint64_t perft(Board& b, int depth) {
    if (depth == 0) return 1;
    MoveList list;
    generateMoves(b, list);
    uint64_t nodes = 0;
    StateInfo st;
    Color us = b.sideToMove;
    for (int i = 0; i < list.count; i++) {
        Move m = list.moves[i];
        b.makeMove(m, st);
        if (!b.inCheck(us)) {
            nodes += perft(b, depth - 1);
        }
        b.unmakeMove(m, st);
    }
    return nodes;
}
void perftDivide(Board& b, int depth) {
    MoveList list;
    generateMoves(b, list);
    uint64_t total = 0;
    StateInfo st;
    Color us = b.sideToMove;
    for (int i = 0; i < list.count; i++) {
        Move m = list.moves[i];
        b.makeMove(m, st);
        if (!b.inCheck(us)) {
            uint64_t n = (depth <= 1) ? 1 : perft(b, depth - 1);
            std::cout << moveToUci(m) << ": " << n << std::endl;
            total += n;
        }
        b.unmakeMove(m, st);
    }
    std::cout << "Total: " << total << std::endl;
}
std::string moveToUci(Move m) {
    if (m == MOVE_NONE) return "0000";
    if (m == MOVE_NULL) return "0000";
    Square from = fromSq(m);
    Square to = toSq(m);
    std::string s;
    s += char('a' + fileOf(from));
    s += char('1' + rankOf(from));
    s += char('a' + fileOf(to));
    s += char('1' + rankOf(to));
    if (isPromo(m)) {
        PieceType promo = promoType(m);
        char c = (promo == 0) ? 'n' : (promo == 1) ? 'b' : (promo == 2) ? 'r' : 'q';
        s += c;
    }
    return s;
}
Move uciToMove(const Board& b, const std::string& s) {
    if (s.length() < 4 || s.length() > 5) return MOVE_NONE;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        bool ok = (i % 2 == 0) ? (c >= 'a' && c <= 'h') : (c >= '1' && c <= '8');
        if (!ok) return MOVE_NONE;
    }
    if (s.length() == 5 && s[4] != 'n' && s[4] != 'b' && s[4] != 'r' && s[4] != 'q')
        return MOVE_NONE;
    Square from = makeSquare(File(s[0] - 'a'), Rank(s[1] - '1'));
    Piece p = b.pieceOn[from];
    if (p == NO_PIECE || colorOf(p) != b.sideToMove) return MOVE_NONE;
    MoveList list;
    generateMoves(b, list);
    for (int i = 0; i < list.count; i++) {
        if (moveToUci(list.moves[i]) != s) continue;
        Board tmp = b;
        StateInfo st;
        tmp.makeMove(list.moves[i], st);
        if (!tmp.inCheck(b.sideToMove)) return list.moves[i];
    }
    if (s.length() == 4) {
        for (int i = 0; i < list.count; i++) {
            Move m = list.moves[i];
            if (!isPromo(m) || promoType(m) != 3) continue;
            Square f = fromSq(m), t = toSq(m);
            if (char('a' + fileOf(f)) != s[0] || char('1' + rankOf(f)) != s[1] ||
                char('a' + fileOf(t)) != s[2] || char('1' + rankOf(t)) != s[3]) continue;
            Board tmp = b;
            StateInfo st;
            tmp.makeMove(m, st);
            if (!tmp.inCheck(b.sideToMove)) return m;
        }
    }
    return MOVE_NONE;
}
}
