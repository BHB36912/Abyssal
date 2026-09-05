#include <immintrin.h>
#include <cstring>
#include <algorithm>
#include "incbin/incbin.h"
#include "nnue.h"
#include "types.h"
namespace Chess {
INCBIN(engNet, "net.bin");
alignas(32) static NetW L1W[IN_DIM * HID];
alignas(32) static NetW L1B[HID];
alignas(32) static NetW L2W[OUT_BUCKETS * HID * 2];
alignas(32) static NetW L2B[OUT_BUCKETS];
static inline int halfIdx(PieceType pt, Square sq, Color col, Color persp, Square ksq) {
    return featureIndex(pt, col, sq, persp, ksq) * HID;
}
#if defined(__AVX__) || defined(__AVX2__)
static inline void accAdd(int16_t* acc, const NetW* w) {
    auto a = reinterpret_cast<__m256i*>(acc);
    auto v = reinterpret_cast<const __m256i*>(w);
    for (int i = 0; i < HID / 16; i++) a[i] = _mm256_add_epi16(a[i], v[i]);
}
static inline void accSub(int16_t* acc, const NetW* w) {
    auto a = reinterpret_cast<__m256i*>(acc);
    auto v = reinterpret_cast<const __m256i*>(w);
    for (int i = 0; i < HID / 16; i++) a[i] = _mm256_sub_epi16(a[i], v[i]);
}
static inline void accMove(int16_t* acc, const NetW* wFrom, const NetW* wTo) {
    auto a = reinterpret_cast<__m256i*>(acc);
    auto vf = reinterpret_cast<const __m256i*>(wFrom);
    auto vt = reinterpret_cast<const __m256i*>(wTo);
    for (int i = 0; i < HID / 16; i++)
        a[i] = _mm256_add_epi16(a[i], _mm256_sub_epi16(vt[i], vf[i]));
}
static inline void accFill(int16_t* acc) {
    auto a = reinterpret_cast<__m256i*>(acc);
    auto b = reinterpret_cast<const __m256i*>(&L1B[0]);
    for (int i = 0; i < HID / 16; i++) a[i] = b[i];
}
#else
static inline void accAdd(int16_t* acc, const NetW* w) {
    for (int i = 0; i < HID; i++) acc[i] = int16_t(acc[i] + w[i]);
}
static inline void accSub(int16_t* acc, const NetW* w) {
    for (int i = 0; i < HID; i++) acc[i] = int16_t(acc[i] - w[i]);
}
static inline void accMove(int16_t* acc, const NetW* wFrom, const NetW* wTo) {
    for (int i = 0; i < HID; i++) acc[i] = int16_t(acc[i] - wFrom[i] + wTo[i]);
}
static inline void accFill(int16_t* acc) {
    for (int i = 0; i < HID; i++) acc[i] = L1B[i];
}
#endif
void NnueState::add(PieceType pt, Square sq, Color col, Square wking, Square bking) {
    accAdd(&accum[0], &L1W[halfIdx(pt, sq, col, WHITE, wking)]);
    accAdd(&accum[HID], &L1W[halfIdx(pt, sq, col, BLACK, bking)]);
}
void NnueState::remove(PieceType pt, Square sq, Color col, Square wking, Square bking) {
    accSub(&accum[0], &L1W[halfIdx(pt, sq, col, WHITE, wking)]);
    accSub(&accum[HID], &L1W[halfIdx(pt, sq, col, BLACK, bking)]);
}
void NnueState::update(PieceType pt, Square from, Square to, Color col, Square wking, Square bking) {
    accMove(&accum[0],
            &L1W[halfIdx(pt, from, col, WHITE, wking)],
            &L1W[halfIdx(pt, to,   col, WHITE, wking)]);
    accMove(&accum[HID],
            &L1W[halfIdx(pt, from, col, BLACK, bking)],
            &L1W[halfIdx(pt, to,   col, BLACK, bking)]);
}
void NnueState::rebuild(const Piece* board, Square wking, Square bking) {
    if (wking >= 64 || bking >= 64) { zero(); return; }
    accFill(&accum[0]);
    accFill(&accum[HID]);
    for (int s = 0; s < 64; s++) {
        Square sq = Square(s);
        if (board[sq] != NO_PIECE)
            add(typeOf(board[sq]), sq, colorOf(board[sq]), wking, bking);
    }
}
int NnueState::evaluate(Color col, int pieceCount) const {
    const int topIdx = (col == WHITE ? 0 : HID);
    const int botIdx = (col == WHITE ? HID : 0);
    const int ob = outputBucket(pieceCount);
    const int owIdx = ob * HID * 2;
    int eval = L2B[ob];
#if defined(__AVX__) || defined(__AVX2__)
    auto vectorEval = _mm256_setzero_si256();
    const auto vLo = _mm256_set1_epi16(ACT_MIN);
    const auto vHi = _mm256_set1_epi16(ACT_MAX);
    const auto aTop = reinterpret_cast<const __m256i*>(&accum[topIdx]);
    const auto wTop = reinterpret_cast<const __m256i*>(&L2W[owIdx]);
    const auto aBot = reinterpret_cast<const __m256i*>(&accum[botIdx]);
    const auto wBot = reinterpret_cast<const __m256i*>(&L2W[owIdx + HID]);
    for (int i = 0; i < HID / 16; i++) {
        vectorEval = _mm256_add_epi32(vectorEval,
            _mm256_madd_epi16(
                _mm256_min_epi16(_mm256_max_epi16(aTop[i], vLo), vHi),
                wTop[i]));
    }
    for (int i = 0; i < HID / 16; i++) {
        vectorEval = _mm256_add_epi32(vectorEval,
            _mm256_madd_epi16(
                _mm256_min_epi16(_mm256_max_epi16(aBot[i], vLo), vHi),
                wBot[i]));
    }
    {
        alignas(32) int32_t lanes[8];
        _mm256_store_si256(reinterpret_cast<__m256i*>(lanes), vectorEval);
        for (int i = 0; i < 8; i++) eval += lanes[i];
    }
#else
    for (int i = 0; i < HID; i++)
        eval += std::clamp(accum[topIdx + i], ACT_MIN, ACT_MAX) * L2W[owIdx + i];
    for (int i = 0; i < HID; i++)
        eval += std::clamp(accum[botIdx + i], ACT_MIN, ACT_MAX) * L2W[owIdx + HID + i];
#endif
    eval = ((eval * EVAL_SCALE) / (QA * QB));
    return eval;
}
void initNet() {
    int idx = 0;
    std::memcpy(L1W, gengNetData + idx, IN_DIM * HID * sizeof(NetW));
    idx += IN_DIM * HID * sizeof(NetW);
    std::memcpy(L1B, gengNetData + idx, HID * sizeof(NetW));
    idx += HID * sizeof(NetW);
    std::memcpy(L2W, gengNetData + idx, OUT_BUCKETS * HID * 2 * sizeof(NetW));
    idx += OUT_BUCKETS * HID * 2 * sizeof(NetW);
    std::memcpy(L2B, gengNetData + idx, OUT_BUCKETS * sizeof(NetW));
}
}
