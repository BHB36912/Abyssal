#pragma once
#include "board.h"
#include "transposition.h"
#include <atomic>
#include <chrono>
namespace Chess {
struct SearchLimits {
    int maxDepth = 64;
    long long timeLimit = 0;
    long long moveTime = 0;
    long long nodesLimit = 0;
    bool infinite = false;
    bool ponder = false;
};
struct SearchInfo {
    std::atomic<bool> stop;
    std::atomic<long long> nodes;
    std::chrono::steady_clock::time_point startTime;
    long long allocatedTime;
    int maxPly;
    Move pv[64][64];
    int pvLength[64];
    int seldepth;
    Move killers[64][2];
    Move currMove[64];
    int history[16][64];
    int staticEval[64];
    Piece currPiece[64];
    Move counterMove[16][64];
    int captureHistory[16][64];
    Move excludedMove[64];
    bool pondering = false;
    bool ponderClockStarted = false;
    long long nodesLimit = 0;
};
struct SharedSearchData {
    std::atomic<bool> stop;
    std::atomic<int> bestDepth;
    std::atomic<int> bestScore;
    std::atomic<uint32_t> bestMove;
    std::atomic<uint32_t> bestMoveGuard;
    Board* board;
    SearchLimits* limits;
    int numThreads;
};
void initSearch();
Move search(Board& b, SearchLimits limits, SearchInfo& info);
Move searchLazySMP(Board& b, SearchLimits limits, int numThreads);
void uciOutLine(const std::string& line);
extern std::atomic<bool> GlobalStop;
extern std::atomic<bool> GlobalPonderHit;
}
