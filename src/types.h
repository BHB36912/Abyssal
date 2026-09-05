#pragma once
#include <cstdint>
#include <array>
#include <string>
namespace Chess {
enum Color : int { WHITE = 0, BLACK = 1 };
constexpr Color operator~(Color c) {
    return Color(c ^ 1);
}
enum PieceType : int {
    PAWN = 0, KNIGHT = 1, BISHOP = 2, ROOK = 3, QUEEN = 4, KING = 5,
    NO_PIECE_TYPE = 6
};
enum Piece : int {
    W_PAWN = 0, W_KNIGHT = 1, W_BISHOP = 2, W_ROOK = 3, W_QUEEN = 4, W_KING = 5,
    B_PAWN = 8, B_KNIGHT = 9, B_BISHOP = 10, B_ROOK = 11, B_QUEEN = 12, B_KING = 13,
    NO_PIECE = 14
};
inline Piece makePiece(Color c, PieceType pt) { return Piece((c << 3) | pt); }
inline PieceType typeOf(Piece p) {
    return PieceType(p & 7);
}
inline Color colorOf(Piece p) { return Color((p >> 3) & 1); }
enum Square : int {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE = 64
};
constexpr int operator+(Square s, int i) { return int(s) + i; }
constexpr int operator-(Square s, int i) { return int(s) - i; }
constexpr Square& operator+=(Square& s, int i) { s = Square(int(s) + i); return s; }
constexpr Square& operator-=(Square& s, int i) { s = Square(int(s) - i); return s; }
constexpr Square operator++(Square& s) { s = Square(int(s) + 1); return s; }
constexpr Square operator++(Square& s, int) { Square old = s; s = Square(int(s) + 1); return old; }
enum File : int { FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H };
enum Rank : int { RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8 };
inline File fileOf(Square s) { return File(s & 7); }
inline Rank rankOf(Square s) { return Rank(s >> 3); }
inline Square makeSquare(File f, Rank r) { return Square((r << 3) | f); }
enum Direction : int {
    NORTH = 8, SOUTH = -8, EAST = 1, WEST = -1,
    NORTH_EAST = 9, NORTH_WEST = 7, SOUTH_EAST = -7, SOUTH_WEST = -9
};
typedef uint32_t Move;
constexpr Move MOVE_NONE = 0;
constexpr Move MOVE_NULL = 0xFFFF;
inline Move makeMove(Square from, Square to) {
    return Move((from << 6) | to);
}
inline Move makePromo(Square from, Square to, PieceType promo) {
    return Move((1 << 14) | (promo << 12) | (from << 6) | to);
}
inline Move makeEnPassant(Square from, Square to) {
    return Move((1 << 15) | (from << 6) | to);
}
inline Move makeCastling(Square from, Square to) {
    return Move((1 << 16) | (from << 6) | to);
}
inline Square fromSq(Move m) { return Square((m >> 6) & 63); }
inline Square toSq(Move m) { return Square(m & 63); }
inline PieceType promoType(Move m) { return PieceType((m >> 12) & 3); }
inline bool isPromo(Move m) { return (m & (1 << 14)) != 0; }
inline bool isEnPassant(Move m) { return (m & (1 << 15)) != 0; }
inline bool isCastling(Move m) { return (m & (1 << 16)) != 0; }
enum CastlingRights : int {
    NO_CASTLING = 0,
    WHITE_OO = 1,
    WHITE_OOO = 2,
    BLACK_OO = 4,
    BLACK_OOO = 8,
    ANY_CASTLING = 15
};
constexpr CastlingRights operator|(CastlingRights a, CastlingRights b) {
    return CastlingRights(int(a) | int(b));
}
constexpr CastlingRights& operator|=(CastlingRights& a, CastlingRights b) {
    a = a | b; return a;
}
constexpr int VALUE_INFINITE = 32000;
constexpr int VALUE_MATE = 30000;
constexpr int VALUE_MATE_IN_MAX = VALUE_MATE - 1000;
constexpr int VALUE_DRAW = 0;
constexpr int VALUE_NONE = 32001;
enum Bound : int {
    BOUND_NONE = 0,
    BOUND_UPPER = 1,
    BOUND_LOWER = 2,
    BOUND_EXACT = BOUND_UPPER | BOUND_LOWER
};
}
