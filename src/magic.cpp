#include <cstdlib>
#include <iostream>
#include "magic.h"
#include <random>
#include <cstring>
namespace Chess {
Magic RookMagics[64];
Magic BishopMagics[64];
static const int RookRelevantBits[64] = {
    12, 11, 11, 11, 11, 11, 11, 12,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    12, 11, 11, 11, 11, 11, 11, 12
};
static const int BishopRelevantBits[64] = {
    6, 5, 5, 5, 5, 5, 5, 6,
    5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,
    6, 5, 5, 5, 5, 5, 5, 6
};
static Bitboard rookMask(Square sq) {
    Bitboard mask = 0;
    int f = fileOf(sq), r = rankOf(sq);
    for (int nr = r + 1; nr <= 6; nr++) mask |= squareBB(makeSquare(File(f), Rank(nr)));
    for (int nr = r - 1; nr >= 1; nr--) mask |= squareBB(makeSquare(File(f), Rank(nr)));
    for (int nf = f + 1; nf <= 6; nf++) mask |= squareBB(makeSquare(File(nf), Rank(r)));
    for (int nf = f - 1; nf >= 1; nf--) mask |= squareBB(makeSquare(File(nf), Rank(r)));
    return mask;
}
static Bitboard bishopMask(Square sq) {
    Bitboard mask = 0;
    int f = fileOf(sq), r = rankOf(sq);
    for (int nf = f + 1, nr = r + 1; nf <= 6 && nr <= 6; nf++, nr++) mask |= squareBB(makeSquare(File(nf), Rank(nr)));
    for (int nf = f + 1, nr = r - 1; nf <= 6 && nr >= 1; nf++, nr--) mask |= squareBB(makeSquare(File(nf), Rank(nr)));
    for (int nf = f - 1, nr = r + 1; nf >= 1 && nr <= 6; nf--, nr++) mask |= squareBB(makeSquare(File(nf), Rank(nr)));
    for (int nf = f - 1, nr = r - 1; nf >= 1 && nr >= 1; nf--, nr--) mask |= squareBB(makeSquare(File(nf), Rank(nr)));
    return mask;
}
static Bitboard rookAttacksSlow(Square sq, Bitboard occupied) {
    Bitboard attacks = 0;
    int f = fileOf(sq), r = rankOf(sq);
    for (int nr = r + 1; nr <= 7; nr++) {
        Square s = makeSquare(File(f), Rank(nr));
        attacks |= squareBB(s);
        if (occupied & s) break;
    }
    for (int nr = r - 1; nr >= 0; nr--) {
        Square s = makeSquare(File(f), Rank(nr));
        attacks |= squareBB(s);
        if (occupied & s) break;
    }
    for (int nf = f + 1; nf <= 7; nf++) {
        Square s = makeSquare(File(nf), Rank(r));
        attacks |= squareBB(s);
        if (occupied & s) break;
    }
    for (int nf = f - 1; nf >= 0; nf--) {
        Square s = makeSquare(File(nf), Rank(r));
        attacks |= squareBB(s);
        if (occupied & s) break;
    }
    return attacks;
}
static Bitboard bishopAttacksSlow(Square sq, Bitboard occupied) {
    Bitboard attacks = 0;
    int f = fileOf(sq), r = rankOf(sq);
    for (int nf = f + 1, nr = r + 1; nf <= 7 && nr <= 7; nf++, nr++) {
        Square s = makeSquare(File(nf), Rank(nr));
        attacks |= squareBB(s);
        if (occupied & s) break;
    }
    for (int nf = f + 1, nr = r - 1; nf <= 7 && nr >= 0; nf++, nr--) {
        Square s = makeSquare(File(nf), Rank(nr));
        attacks |= squareBB(s);
        if (occupied & s) break;
    }
    for (int nf = f - 1, nr = r + 1; nf >= 0 && nr <= 7; nf--, nr++) {
        Square s = makeSquare(File(nf), Rank(nr));
        attacks |= squareBB(s);
        if (occupied & s) break;
    }
    for (int nf = f - 1, nr = r - 1; nf >= 0 && nr >= 0; nf--, nr--) {
        Square s = makeSquare(File(nf), Rank(nr));
        attacks |= squareBB(s);
        if (occupied & s) break;
    }
    return attacks;
}
static Bitboard randomMagic(std::mt19937_64& rng) {
    return rng() & rng() & rng();
}
static Bitboard findMagic(Square sq, int relevantBits, bool bishop, std::mt19937_64& rng) {
    int size = 1 << relevantBits;
    Bitboard* occupancy = new Bitboard[size];
    Bitboard* reference = new Bitboard[size];
    Bitboard* used = new Bitboard[size];
    Bitboard mask = bishop ? bishopMask(sq) : rookMask(sq);
    for (int i = 0; i < size; i++) {
        occupancy[i] = 0;
    }
    for (int i = 0; i < size; i++) {
        Bitboard subset = 0;
        Bitboard m = mask;
        int idx = i;
        while (m) {
            Square s = pop_lsb(m);
            if (idx & 1) subset |= squareBB(s);
            idx >>= 1;
        }
        occupancy[i] = subset;
        reference[i] = bishop ? bishopAttacksSlow(sq, subset) : rookAttacksSlow(sq, subset);
    }
    for (int attempts = 0; attempts < 50000000; attempts++) {
        Bitboard magic = randomMagic(rng);
        if (popcount((mask * magic) & 0xFF00000000000000ULL) < 6) continue;
        std::memset(used, 0, size * sizeof(Bitboard));
        bool ok = true;
        for (int i = 0; i < size; i++) {
            unsigned idx = unsigned((occupancy[i] * magic) >> (64 - relevantBits));
            if (used[idx] == 0) {
                used[idx] = reference[i];
            } else if (used[idx] != reference[i]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            delete[] occupancy;
            delete[] reference;
            delete[] used;
            return magic;
        }
    }
    delete[] occupancy;
    delete[] reference;
    delete[] used;
    return 0;
}
void initMagic() {
    std::mt19937_64 rng(0xDEADBEEFCAFEBABEULL);
    for (Square s = SQ_A1; s < 64; s++) {
        int bits = RookRelevantBits[s];
        Bitboard mask = rookMask(s);
        Bitboard magic = findMagic(s, bits, false, rng);
        if (magic == 0) {
            std::cerr << "Failed to find rook magic for square " << s << std::endl;
            std::abort();
        }
        int size = 1 << bits;
        int shift = 64 - bits;
        Bitboard* table = new Bitboard[size];
        for (int i = 0; i < size; i++) {
            Bitboard subset = 0;
            Bitboard m = mask;
            int idx = i;
            while (m) {
                Square sq2 = pop_lsb(m);
                if (idx & 1) subset |= squareBB(sq2);
                idx >>= 1;
            }
            unsigned magicIdx = unsigned((subset * magic) >> shift);
            table[magicIdx] = rookAttacksSlow(s, subset);
        }
        RookMagics[s].mask = mask;
        RookMagics[s].magic = magic;
        RookMagics[s].shift = shift;
        RookMagics[s].attacks = table;
    }
    for (Square s = SQ_A1; s < 64; s++) {
        int bits = BishopRelevantBits[s];
        Bitboard mask = bishopMask(s);
        Bitboard magic = findMagic(s, bits, true, rng);
        if (magic == 0) {
            std::cerr << "Failed to find bishop magic for square " << s << std::endl;
            std::abort();
        }
        int size = 1 << bits;
        int shift = 64 - bits;
        Bitboard* table = new Bitboard[size];
        for (int i = 0; i < size; i++) {
            Bitboard subset = 0;
            Bitboard m = mask;
            int idx = i;
            while (m) {
                Square sq2 = pop_lsb(m);
                if (idx & 1) subset |= squareBB(sq2);
                idx >>= 1;
            }
            unsigned magicIdx = unsigned((subset * magic) >> shift);
            table[magicIdx] = bishopAttacksSlow(s, subset);
        }
        BishopMagics[s].mask = mask;
        BishopMagics[s].magic = magic;
        BishopMagics[s].shift = shift;
        BishopMagics[s].attacks = table;
    }
    for (Square s = SQ_A1; s < 64; s++) {
        for (int t = 0; t < 100; t++) {
            Bitboard occ = rng();
            Bitboard fastR = attacksRook(s, occ);
            Bitboard slowR = rookAttacksSlow(s, occ);
            Bitboard fastB = attacksBishop(s, occ);
            Bitboard slowB = bishopAttacksSlow(s, occ);
            if (fastR != slowR) {
                std::cerr << "ROOK MISMATCH at square " << s << " occ=" << std::hex << occ << std::dec << std::endl;
                std::abort();
            }
            if (fastB != slowB) {
                std::cerr << "BISHOP MISMATCH at square " << s << " occ=" << std::hex << occ << std::dec << std::endl;
                std::abort();
            }
        }
    }
}
}
