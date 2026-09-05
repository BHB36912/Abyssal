#pragma once
#include "board.h"
#include "types.h"
#include <vector>
namespace Chess {
struct MoveList {
    Move moves[256];
    int count = 0;
    void add(Move m) { moves[count++] = m; }
    void clear() { count = 0; }
    Move& operator[](int i) { return moves[i]; }
    const Move& operator[](int i) const { return moves[i]; }
    int size() const { return count; }
};
void generateMoves(const Board& b, MoveList& list);
void generateCaptures(const Board& b, MoveList& list);
void generateQuiet(const Board& b, MoveList& list);
void generateEvasions(const Board& b, MoveList& list);
uint64_t perft(Board& b, int depth);
void perftDivide(Board& b, int depth);
std::string moveToUci(Move m);
Move uciToMove(const Board& b, const std::string& s);
}
