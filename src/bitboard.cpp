#include "bitboard.h"
namespace Chess {
std::array<Bitboard, 64> PawnAttacks[2];
std::array<Bitboard, 64> KnightAttacks;
std::array<Bitboard, 64> KingAttacks;
int SquareDistance[64][64];
Bitboard BetweenBB[64][64];
Bitboard LineBB[64][64];
int fileDistance(Square s1, Square s2) {
    return std::abs(int(fileOf(s1)) - int(fileOf(s2)));
}
int rankDistance(Square s1, Square s2) {
    return std::abs(int(rankOf(s1)) - int(rankOf(s2)));
}
template<typename... Directions>
static inline Bitboard slidingAttacks(Square sq, Bitboard occupied, Directions... dirs) {
    Bitboard attacks = 0;
    for (int dir : {dirs...}) {
        Square s = sq;
        while (true) {
            int f = fileOf(s);
            int r = rankOf(s);
            int nf = f + (dir == 1 ? 1 : dir == -1 ? -1 : dir == 9 || dir == -7 ? 1 : dir == 7 || dir == -9 ? -1 : 0);
            int nr = r + (dir == 8 ? 1 : dir == -8 ? -1 : dir == 9 || dir == 7 ? 1 : dir == -9 || dir == -7 ? -1 : 0);
            if (nf < 0 || nf > 7 || nr < 0 || nr > 7) break;
            if (std::abs(nf - f) > 1 || std::abs(nr - r) > 1) break;
            s = makeSquare(File(nf), Rank(nr));
            attacks |= squareBB(s);
            if (occupied & s) break;
        }
    }
    return attacks;
}
static inline Bitboard rayAttacks(Square sq, int fileDelta, int rankDelta, Bitboard occupied) {
    Bitboard attacks = 0;
    int f = fileOf(sq), r = rankOf(sq);
    while (true) {
        f += fileDelta;
        r += rankDelta;
        if (f < 0 || f > 7 || r < 0 || r > 7) break;
        Square s = makeSquare(File(f), Rank(r));
        attacks |= squareBB(s);
        if (occupied & s) break;
    }
    return attacks;
}
void initBitboards() {
    for (Square s = SQ_A1; s < 64; s++) {
        Bitboard w = 0, b = 0;
        int f = fileOf(s), r = rankOf(s);
        if (r < 7) {
            if (f > 0) w |= squareBB(makeSquare(File(f - 1), Rank(r + 1)));
            if (f < 7) w |= squareBB(makeSquare(File(f + 1), Rank(r + 1)));
        }
        if (r > 0) {
            if (f > 0) b |= squareBB(makeSquare(File(f - 1), Rank(r - 1)));
            if (f < 7) b |= squareBB(makeSquare(File(f + 1), Rank(r - 1)));
        }
        PawnAttacks[WHITE][s] = w;
        PawnAttacks[BLACK][s] = b;
        Bitboard n = 0;
        int df[8] = {1, 2, 2, 1, -1, -2, -2, -1};
        int dr[8] = {2, 1, -1, -2, -2, -1, 1, 2};
        for (int i = 0; i < 8; i++) {
            int nf = f + df[i], nr = r + dr[i];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8)
                n |= squareBB(makeSquare(File(nf), Rank(nr)));
        }
        KnightAttacks[s] = n;
        Bitboard k = 0;
        for (int dfi = -1; dfi <= 1; dfi++) {
            for (int dri = -1; dri <= 1; dri++) {
                if (dfi == 0 && dri == 0) continue;
                int nf = f + dfi, nr = r + dri;
                if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8)
                    k |= squareBB(makeSquare(File(nf), Rank(nr)));
            }
        }
        KingAttacks[s] = k;
    }
    for (Square s1 = SQ_A1; s1 < 64; s1++) {
        for (Square s2 = SQ_A1; s2 < 64; s2++) {
            SquareDistance[s1][s2] = std::max(fileDistance(s1, s2), rankDistance(s1, s2));
        }
    }
    for (Square s1 = SQ_A1; s1 < 64; s1++) {
        for (Square s2 = SQ_A1; s2 < 64; s2++) {
            if (s1 == s2) {
                BetweenBB[s1][s2] = 0;
                LineBB[s1][s2] = 0;
                continue;
            }
            int f1 = fileOf(s1), r1 = rankOf(s1);
            int f2 = fileOf(s2), r2 = rankOf(s2);
            int df = f2 - f1, dr = r2 - r1;
            int adf = std::abs(df), adr = std::abs(dr);
            Bitboard line = 0, between = 0;
            if (f1 == f2) {
                int rmin = std::min(r1, r2), rmax = std::max(r1, r2);
                for (int r = rmin; r <= rmax; r++) line |= squareBB(makeSquare(File(f1), Rank(r)));
                for (int r = rmin + 1; r < rmax; r++) between |= squareBB(makeSquare(File(f1), Rank(r)));
            } else if (r1 == r2) {
                int fmin = std::min(f1, f2), fmax = std::max(f1, f2);
                for (int f = fmin; f <= fmax; f++) line |= squareBB(makeSquare(File(f), Rank(r1)));
                for (int f = fmin + 1; f < fmax; f++) between |= squareBB(makeSquare(File(f), Rank(r1)));
            } else if (adf == adr) {
                int sf = df > 0 ? 1 : -1;
                int sr = dr > 0 ? 1 : -1;
                int f = f1, r = r1;
                while (true) {
                    line |= squareBB(makeSquare(File(f), Rank(r)));
                    if (f == f2) break;
                    f += sf; r += sr;
                }
                f = f1 + sf; r = r1 + sr;
                while (f != f2) {
                    between |= squareBB(makeSquare(File(f), Rank(r)));
                    f += sf; r += sr;
                }
            }
            LineBB[s1][s2] = line;
            BetweenBB[s1][s2] = between;
        }
    }
}
}
