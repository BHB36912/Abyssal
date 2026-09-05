#pragma once
#include "types.h"
#include "board.h"
#include <atomic>
#include <vector>
#include <cstring>
namespace Chess {
struct TTData {
    uint16_t key;
    int16_t score;
    int16_t eval;
    uint8_t depth;
    uint8_t bound;
    uint8_t generation;
    uint32_t move;
};
struct TTEntry {
    std::atomic<uint64_t> lo;
    std::atomic<uint64_t> hi;
    void zero() {
        lo.store(0, std::memory_order_relaxed);
        hi.store(0, std::memory_order_relaxed);
    }
    static uint64_t packLo(uint16_t key, uint8_t gen, uint8_t bound, uint8_t depth, int16_t score) {
        return uint64_t(uint16_t(score))
             | (uint64_t(depth) << 16)
             | (uint64_t(bound) << 24)
             | (uint64_t(gen) << 32)
             | (uint64_t(key) << 40);
    }
    static uint64_t packHi(uint32_t move, int16_t eval) {
        return uint64_t(uint32_t(move))
             | (uint64_t(uint16_t(eval)) << 32);
    }
    static inline uint16_t loKey(uint64_t lo)   { return uint16_t(lo >> 40); }
    static inline uint8_t  loGen(uint64_t lo)   { return uint8_t(lo >> 32); }
    static inline uint8_t  loBound(uint64_t lo) { return uint8_t(lo >> 24); }
    static inline uint8_t  loDepth(uint64_t lo) { return uint8_t(lo >> 16); }
    static inline int16_t  loScore(uint64_t lo) { return int16_t(uint16_t(lo)); }
    static inline uint32_t hiMove(uint64_t hi)  { return uint32_t(hi); }
    static inline int16_t  hiEval(uint64_t hi)  { return int16_t(uint16_t(hi >> 32)); }
};
static_assert(sizeof(TTEntry) == 16, "TTEntry should be 16 bytes");
class TranspositionTable {
public:
    TranspositionTable() : mask_(0) {}
    void resize(size_t mb) {
        size_t bytes = mb * 1024 * 1024;
        size_t count = bytes / sizeof(TTEntry);
        size_t mask = 1;
        while (mask * 2 <= count) mask *= 2;
        count = mask;
        mask_ = count - 1;
        table_ = std::vector<TTEntry>(count);
        clear();
    }
    void clear() {
        for (auto& e : table_) e.zero();
    }
    void newSearch() {
        generation_++;
    }
    TTData probe(uint64_t key, bool& found) const {
        TTData d{};
        found = false;
        size_t idx = key & mask_;
        const TTEntry& tte = table_[idx];
        uint64_t lo1 = tte.lo.load(std::memory_order_acquire);
        uint64_t lo2 = tte.lo.load(std::memory_order_acquire);
        if (lo1 != lo2) return d;
        uint64_t hi  = tte.hi.load(std::memory_order_relaxed);
        uint64_t lo3 = tte.lo.load(std::memory_order_acquire);
        if (lo3 != lo1) return d;
        if (TTEntry::loKey(lo1) != uint16_t(key >> 48)) return d;
        d.key        = TTEntry::loKey(lo1);
        d.score      = TTEntry::loScore(lo1);
        d.depth      = TTEntry::loDepth(lo1);
        d.bound      = TTEntry::loBound(lo1);
        d.generation = TTEntry::loGen(lo1);
        d.move       = TTEntry::hiMove(hi);
        d.eval       = TTEntry::hiEval(hi);
        found = true;
        return d;
    }
    Move getTTMove(uint64_t key) const {
        bool found;
        TTData d = probe(key, found);
        return found ? Move(d.move) : MOVE_NONE;
    }
    void save(uint64_t key, int score, int eval, uint8_t depth, Bound bound, Move move) {
        size_t idx = key & mask_;
        TTEntry& tte = table_[idx];
        uint64_t lo1 = tte.lo.load(std::memory_order_acquire);
        uint64_t lo2 = tte.lo.load(std::memory_order_acquire);
        bool sameKey = (lo1 == lo2) && (TTEntry::loKey(lo1) == uint16_t(key >> 48));
        uint8_t gen = sameKey ? TTEntry::loGen(lo1) : uint8_t(0);
        uint8_t oldDepth = sameKey ? TTEntry::loDepth(lo1) : uint8_t(0);
        if (!sameKey || gen != generation_ || bound == BOUND_EXACT || depth + 2 >= oldDepth) {
            if (score > 32767) score = 32767;
            if (score < -32768) score = -32768;
            if (eval > 32767) eval = 32767;
            if (eval < -32768) eval = -32768;
            uint64_t hi = TTEntry::packHi(uint32_t(move), int16_t(eval));
            uint64_t lo = TTEntry::packLo(uint16_t(key >> 48), generation_, uint8_t(bound), depth, int16_t(score));
            tte.hi.store(hi, std::memory_order_relaxed);
            tte.lo.store(lo, std::memory_order_release);
        }
    }
    int hashfull() const {
        if (table_.empty()) return 0;
        int count = 0;
        size_t stride = table_.size() / 1000;
        if (stride == 0) stride = 1;
        for (int i = 0; i < 1000; i++) {
            uint64_t lo = table_[size_t(i) * stride].lo.load(std::memory_order_relaxed);
            if (TTEntry::loKey(lo) != 0) count++;
        }
        return count;
    }
private:
    std::vector<TTEntry> table_;
    size_t mask_;
    uint8_t generation_ = 0;
};
extern TranspositionTable TT;
}
