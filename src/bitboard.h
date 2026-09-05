#pragma once
#include "types.h"
#include <bit>
namespace Chess {
typedef uint64_t Bitboard;
constexpr Bitboard EMPTY = 0ULL;
constexpr Bitboard ALL_SQUARES = ~0ULL;
constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
constexpr Bitboard FILE_B_BB = FILE_A_BB << 1;
constexpr Bitboard FILE_C_BB = FILE_A_BB << 2;
constexpr Bitboard FILE_D_BB = FILE_A_BB << 3;
constexpr Bitboard FILE_E_BB = FILE_A_BB << 4;
constexpr Bitboard FILE_F_BB = FILE_A_BB << 5;
constexpr Bitboard FILE_G_BB = FILE_A_BB << 6;
constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;
constexpr Bitboard RANK_1_BB = 0x00000000000000FFULL;
constexpr Bitboard RANK_2_BB = RANK_1_BB << 8;
constexpr Bitboard RANK_3_BB = RANK_1_BB << 16;
constexpr Bitboard RANK_4_BB = RANK_1_BB << 24;
constexpr Bitboard RANK_5_BB = RANK_1_BB << 32;
constexpr Bitboard RANK_6_BB = RANK_1_BB << 40;
constexpr Bitboard RANK_7_BB = RANK_1_BB << 48;
constexpr Bitboard RANK_8_BB = RANK_1_BB << 56;
constexpr Bitboard LIGHT_SQUARES = 0x55AA55AA55AA55AAULL;
constexpr Bitboard DARK_SQUARES  = ~LIGHT_SQUARES;
inline Bitboard squareBB(Square s) { return 1ULL << s; }
inline Bitboard fileBB(Square s) { return FILE_A_BB << fileOf(s); }
inline Bitboard rankBB(Square s) { return RANK_1_BB << (8 * rankOf(s)); }
inline Bitboard fileBB(File f) { return FILE_A_BB << f; }
inline Bitboard rankBB(Rank r) { return RANK_1_BB << (8 * r); }
inline int popcount(Bitboard b) { return std::popcount(b); }
inline Square lsb(Bitboard b) { return Square(std::countr_zero(b)); }
inline Square msb(Bitboard b) { return Square(63 - std::countl_zero(b)); }
inline Square pop_lsb(Bitboard& b) {
    Square s = lsb(b);
    b &= b - 1;
    return s;
}
inline Bitboard operator&(Bitboard b, Square s) { return b & squareBB(s); }
inline Bitboard operator|(Bitboard b, Square s) { return b | squareBB(s); }
inline Bitboard operator^(Bitboard b, Square s) { return b ^ squareBB(s); }
inline Bitboard& operator|=(Bitboard& b, Square s) { b |= squareBB(s); return b; }
inline Bitboard& operator^=(Bitboard& b, Square s) { b ^= squareBB(s); return b; }
template<Direction D>
constexpr Bitboard shift(Bitboard b) {
    return D == NORTH       ? b << 8
         : D == SOUTH       ? b >> 8
         : D == NORTH_EAST  ? (b & ~FILE_H_BB) << 9
         : D == SOUTH_EAST  ? (b & ~FILE_H_BB) >> 7
         : D == NORTH_WEST  ? (b & ~FILE_A_BB) << 7
         : D == SOUTH_WEST  ? (b & ~FILE_A_BB) >> 9
         : 0;
}
extern std::array<Bitboard, 64> PawnAttacks[2];
extern std::array<Bitboard, 64> KnightAttacks;
extern std::array<Bitboard, 64> KingAttacks;
void initBitboards();
extern int SquareDistance[64][64];
extern int fileDistance(Square s1, Square s2);
extern int rankDistance(Square s1, Square s2);
extern Bitboard BetweenBB[64][64];
extern Bitboard LineBB[64][64];
}
