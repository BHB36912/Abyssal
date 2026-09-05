#include "board.h"
#include "movegen.h"
#include "search.h"
#include "transposition.h"
#include "evaluate.h"
#include "bitboard.h"
#include "magic.h"
#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace Chess;
static Board board;
static SearchLimits limits;
static int g_threads = 1;
static std::thread searchThread;
static void stopAndJoinSearch() {
    GlobalStop.store(true, std::memory_order_relaxed);
    if (searchThread.joinable()) searchThread.join();
    GlobalStop.store(false, std::memory_order_relaxed);
}
static void runSearch(Board b, SearchLimits lims) {
    Move bestMove = searchLazySMP(b, lims, g_threads);
    if (lims.ponder) {
        while (!GlobalStop.load(std::memory_order_relaxed) &&
               !GlobalPonderHit.load(std::memory_order_relaxed))
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (bestMove == MOVE_NONE) {
        MoveList list;
        generateMoves(b, list);
        StateInfo st;
        Color us = b.sideToMove;
        for (int i = 0; i < list.count; i++) {
            b.makeMove(list.moves[i], st);
            bool illegal = b.inCheck(us);
            b.unmakeMove(list.moves[i], st);
            if (!illegal) { bestMove = list.moves[i]; break; }
        }
    }
    uciOutLine(std::string("bestmove ") + moveToUci(bestMove));
}
void uciNewGame() {
    stopAndJoinSearch();
    TT.clear();
}
void handleGo(std::istringstream& iss) {
    limits = SearchLimits();
    std::string token;
    long long wtime = 0, btime = 0, winc = 0, binc = 0, movetime = 0;
    long long nodesLimit = 0;
    int movestogo = 0, depth = 0;
    bool infinite = false;
    bool hasClock = false;
    while (iss >> token) {
        if (token == "wtime") { iss >> wtime; hasClock = true; }
        else if (token == "btime") { iss >> btime; hasClock = true; }
        else if (token == "winc") iss >> winc;
        else if (token == "binc") iss >> binc;
        else if (token == "movestogo") iss >> movestogo;
        else if (token == "depth") iss >> depth;
        else if (token == "nodes") iss >> nodesLimit;
        else if (token == "movetime") iss >> movetime;
        else if (token == "infinite") infinite = true;
        else if (token == "ponder") limits.ponder = true;
    }
    GlobalPonderHit.store(false, std::memory_order_relaxed);
    limits.infinite = infinite;
    limits.nodesLimit = nodesLimit;
    if (depth > 0) {
        limits.maxDepth = depth;
    } else {
        limits.maxDepth = 64;
    }
    Color us = board.sideToMove;
    long long myTime = (us == WHITE) ? wtime : btime;
    if (myTime < 0) myTime = 0;
    long long myInc = (us == WHITE) ? winc : binc;
    if (myInc < 0) myInc = 0;
    if (movetime > 0) {
        limits.moveTime = movetime;
        limits.timeLimit = 0;
    } else if (hasClock) {
        long long reserve = myTime > 8000 ? 100 : 30;
        long long usable = myTime - reserve;
        if (usable < 5) usable = 5;
        long long allocated;
        if (movestogo > 0) {
            allocated = usable / movestogo + myInc * 3 / 4;
            long long cap = usable / 2;
            if (allocated > cap) allocated = cap;
        } else {
            int est = 50 - (int)board.fullmove;
            if (myInc >= 100) est -= 8;
            if (est < 12) est = 12;
            allocated = usable / est + myInc * 3 / 4;
            long long cap = usable / 6;
            if (usable < 3000) cap = usable / 12;
            if (usable < 1000) cap = usable / 20;
            if (myInc > 0) {
                long long incCap = myInc * 3 / 4;
                if (incCap > usable / 2) incCap = usable / 2;
                if (incCap > cap) cap = incCap;
            }
            if (allocated > cap) allocated = cap;
        }
        if (allocated < 5) allocated = 5;
        limits.timeLimit = allocated;
        limits.moveTime = 0;
    } else if (!infinite && depth <= 0 && !limits.ponder) {
        limits.moveTime = 1000;
        limits.timeLimit = 0;
    } else {
        limits.moveTime = 0;
        limits.timeLimit = 0;
    }
    stopAndJoinSearch();
    searchThread = std::thread(runSearch, board, limits);
}
void handlePosition(std::istringstream& iss) {
    std::string token;
    iss >> token;
    std::string fen;
    if (token == "startpos") {
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        iss >> token;
    } else if (token == "fen") {
        while (iss >> token && token != "moves") {
            if (!fen.empty()) fen += ' ';
            fen += token;
        }
    } else {
        return;
    }
    Board nb;
    if (!nb.setFen(fen)) return;
    if (token == "moves") {
        while (iss >> token) {
            Move m = uciToMove(nb, token);
            if (m == MOVE_NONE) break;
            StateInfo st;
            nb.makeMove(m, st);
        }
    }
    board = nb;
}
void setOption(std::istringstream& iss) {
    std::string token;
    iss >> token;
    std::string name;
    while (iss >> token && token != "value") {
        if (!name.empty()) name += ' ';
        name += token;
    }
    std::string value;
    while (iss >> token) {
        if (!value.empty()) value += ' ';
        value += token;
    }
    if (name == "Hash") {
        try {
            int mb = std::stoi(value);
            if (mb < 1) mb = 1;
            if (mb > 2048) mb = 2048;
            stopAndJoinSearch();
            TT.resize(mb);
        } catch (...) {}
    } else if (name == "Clear Hash") {
        stopAndJoinSearch();
        TT.clear();
    } else if (name == "Threads") {
        try {
            int n = std::stoi(value);
            if (n < 1) n = 1;
            if (n > 8) n = 8;
            g_threads = n;
        } catch (...) {}
    }
}
int main() {
    initBitboards();
    initMagic();
    initZobrist();
    initNet();
    initSearch();
    TT.resize(128);
    board.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string token;
        iss >> token;
        if (token == "uci") {
            uciOutLine("id name Abyssal");
            uciOutLine("id author Bach Bui Hoang");
            uciOutLine("option name Hash type spin default 128 min 1 max 2048");
            uciOutLine("option name Threads type spin default 2 min 1 max 8");
            uciOutLine("option name Clear Hash type button");
            uciOutLine("uciok");
        } else if (token == "isready") {
            uciOutLine("readyok");
        } else if (token == "ucinewgame") {
            uciNewGame();
            GlobalPonderHit.store(false, std::memory_order_relaxed);
        } else if (token == "setoption") {
            setOption(iss);
        } else if (token == "position") {
            handlePosition(iss);
        } else if (token == "go") {
            handleGo(iss);
        } else if (token == "ponderhit") {
            GlobalPonderHit.store(true, std::memory_order_relaxed);
        } else if (token == "quit") {
            stopAndJoinSearch();
            break;
        } else if (token == "stop") {
            GlobalStop.store(true, std::memory_order_relaxed);
        } else if (token == "eval") {
            int score = evaluate(board);
            std::cout << "eval (side to move): " << score << std::endl;
            int wScore = (board.sideToMove == WHITE) ? score : -score;
            std::cout << "eval (white perspective): " << wScore << std::endl;
            std::cout << "eval engine: NNUE, " << popcount(board.occupied) << " pieces" << std::endl;
            std::cout.flush();
        } else if (token == "perft") {
            int depth;
            iss >> depth;
            uint64_t nodes = perft(board, depth);
            std::cout << "nodes " << nodes << std::endl;
            std::cout.flush();
        } else if (token == "bench") {
            int depth = 10;
            iss >> depth;
            if (depth < 1) depth = 10;
            static const char* benchFens[] = {
                "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
                "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
                "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
                "r1bqk2r/pp2bppp/2n1pn2/2pp4/3P4/2N1PN2/PP2BPPP/R1BQK2R w KQkq - 0 9",
                "4k3/5pp1/8/2Pp4/1P6/6P1/7P/4K3 w - d6 0 34",
                "8/p6k/4p3/2P1P3/6K1/8/P7/8 w - - 0 1",
                "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 b - - 0 10"
            };
            long long totalNodes = 0;
            auto t0 = std::chrono::steady_clock::now();
            for (const char* f : benchFens) {
                Board b;
                b.setFen(f);
                SearchLimits sl;
                sl.maxDepth = depth;
                SearchInfo info;
                Move bm = search(b, sl, info);
                long long n = info.nodes.load();
                totalNodes += n;
                std::cout << "bench: nodes " << n << " best " << moveToUci(bm) << "  (" << f << ")" << std::endl;
            }
            auto t1 = std::chrono::steady_clock::now();
            long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            long long nps = ms > 0 ? totalNodes * 1000 / ms : 0;
            std::cout << "bench total: depth " << depth << " nodes " << totalNodes
                      << " time " << ms << " nps " << nps << std::endl;
            std::cout.flush();
        } else if (token == "verify") {
            Board tmp = board;
            uint64_t k0 = tmp.key;
            tmp.computeKeys();
            bool ok = (k0 == tmp.key);
            NnueState fresh = tmp.nnue;
            fresh.rebuild(tmp.pieceOn, tmp.kingSquare[WHITE], tmp.kingSquare[BLACK]);
            bool nnueOk = (std::memcmp(fresh.accum, tmp.nnue.accum, sizeof(fresh.accum)) == 0);
            if (ok && nnueOk) {
                std::cout << "verify: OK" << std::endl;
            } else {
                std::cout << "verify: MISMATCH!";
                if (k0 != tmp.key) std::cout << " key";
                if (!nnueOk) {
                    std::cout << " nnue";
                    int wEval = tmp.nnue.evaluate(WHITE, popcount(tmp.occupied));
                    int freshEval = fresh.evaluate(WHITE, popcount(tmp.occupied));
                    std::cout << " (incr=" << wEval << " fresh=" << freshEval << ")";
                }
                std::cout << std::endl;
            }
            std::cout.flush();
        }
    }
    return 0;
}
