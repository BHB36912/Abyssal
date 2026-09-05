#include "search.h"
#include "tunables.h"
#include "movegen.h"
#include "evaluate.h"
#include "magic.h"
#include <iostream>
#include <thread>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <memory>
#include <vector>
#include <mutex>
#include <sstream>
#include <string>
namespace Chess {
std::atomic<bool> GlobalStop(false);
std::atomic<bool> GlobalPonderHit(false);
static std::mutex gUciIOMutex;
void uciOutLine(const std::string& line) {
    std::lock_guard<std::mutex> lk(gUciIOMutex);
    std::cout << line << std::endl;
}
static int LMRTable[64][64];
static int HistoryLimit = 16384;
static thread_local int contHistory[16][64][16][64];
// History gravity: a depth-squared bonus is damped by the current score so the
// entry approaches the limit asymptotically instead of clamping at it.
static inline void addHistoryG(int& h, int bonus) {
    h += bonus - h * std::abs(bonus) / HistoryLimit;
}
static inline void updateContEntries(SearchInfo& info, int ply, Piece pc, Square to, int bonus) {
    for (int back = 1; back <= 2; back++) {
        int p = ply - back;
        if (p < 0) break;
        Piece prevPc = info.currPiece[p];
        Move prevM = info.currMove[p];
        if (prevPc == NO_PIECE || prevM == MOVE_NONE || prevM == MOVE_NULL) continue;
        int& h = contHistory[prevPc][toSq(prevM)][pc][to];
        addHistoryG(h, bonus);
    }
}
static Move validateTTMove(Board& b, Move ttMove) {
    if (ttMove == MOVE_NONE) return MOVE_NONE;
    Square tFrom = fromSq(ttMove);
    Square tTo = toSq(ttMove);
    bool ok = (tFrom < 64 && tTo < 64 && tFrom != tTo);
    if (ok) {
        Piece mover = b.pieceOn[tFrom];
        ok = (mover != NO_PIECE && colorOf(mover) == b.sideToMove);
        if (ok && b.pieceOn[tTo] != NO_PIECE &&
            colorOf(b.pieceOn[tTo]) == b.sideToMove) ok = false;
    }
    if (ok && isEnPassant(ttMove)) {
        ok = (b.epSquare != SQ_NONE && tTo == b.epSquare &&
              typeOf(b.pieceOn[tFrom]) == PAWN);
    }
    if (ok && isCastling(ttMove)) {
        if (b.sideToMove == WHITE) {
            ok = (tTo == SQ_G1 || tTo == SQ_C1);
            CastlingRights need = (tTo == SQ_G1) ? WHITE_OO : WHITE_OOO;
            Square rFrom = (tTo == SQ_G1) ? SQ_H1 : SQ_A1;
            ok = ok && tFrom == SQ_E1 && b.canCastle(need) &&
                 typeOf(b.pieceOn[tFrom]) == KING &&
                 b.pieceOn[rFrom] == makePiece(WHITE, ROOK);
        } else {
            ok = (tTo == SQ_G8 || tTo == SQ_C8);
            CastlingRights need = (tTo == SQ_G8) ? BLACK_OO : BLACK_OOO;
            Square rFrom = (tTo == SQ_G8) ? SQ_H8 : SQ_A8;
            ok = ok && tFrom == SQ_E8 && b.canCastle(need) &&
                 typeOf(b.pieceOn[tFrom]) == KING &&
                 b.pieceOn[rFrom] == makePiece(BLACK, ROOK);
        }
    }
    if (ok && isPromo(ttMove)) {
        bool whitePromo = (b.sideToMove == WHITE &&
                           rankOf(tFrom) == 6 && rankOf(tTo) == 7);
        bool blackPromo = (b.sideToMove == BLACK &&
                           rankOf(tFrom) == 1 && rankOf(tTo) == 0);
        ok = (typeOf(b.pieceOn[tFrom]) == PAWN && (whitePromo || blackPromo));
    }
    return ok ? ttMove : MOVE_NONE;
}
static inline int quietHistory(SearchInfo& info, int ply, Piece pc, Square to,
                               Piece prevPc1, Square prevTo1, Piece prevPc2, Square prevTo2) {
    int h = info.history[pc][to];
    if (prevPc1 != NO_PIECE) h += contHistory[prevPc1][prevTo1][pc][to];
    if (prevPc2 != NO_PIECE) h += contHistory[prevPc2][prevTo2][pc][to];
    return h;
}
void initSearch() {
    for (int d = 0; d < 64; d++) {
        for (int m = 0; m < 64; m++) {
            LMRTable[d][m] = (d >= 3 && m >= 3) ? std::max(0, int(std::log(d) * std::log(m) / (PHX_LMR_DIV / 100.0))) : 0;
        }
    }
}
static int mvvLva(PieceType attacker, PieceType victim) {
    return 6 * (5 - victim) + (5 - attacker);
}
struct ScoredMove {
    Move move;
    int score;
};
static inline bool checkTime(SearchInfo& info) {
    if (info.stop.load(std::memory_order_relaxed)) return true;
    if (GlobalStop.load(std::memory_order_relaxed)) return true;
    if (info.pondering && !GlobalPonderHit.load(std::memory_order_relaxed)) return false;
    if (info.pondering && !info.ponderClockStarted) {
        info.ponderClockStarted = true;
        info.startTime = std::chrono::steady_clock::now();
    }
    if (info.nodesLimit > 0 &&
        info.nodes.load(std::memory_order_relaxed) >= info.nodesLimit) {
        info.stop.store(true, std::memory_order_relaxed);
        return true;
    }
    if (info.allocatedTime <= 0) return false;
    if ((info.nodes.load(std::memory_order_relaxed) & 511) == 0) {
        auto now = std::chrono::steady_clock::now();
        long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - info.startTime).count();
        if (elapsed >= info.allocatedTime) {
            info.stop.store(true, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}
static inline void pickNext(ScoredMove* moves, int count, int i) {
    int best = i;
    for (int j = i + 1; j < count; j++) {
        if (moves[j].score > moves[best].score) best = j;
    }
    if (best != i) std::swap(moves[i], moves[best]);
}
static int qSearch(Board& b, SearchInfo& info, int alpha, int beta, int ply, int depth) {
    info.nodes.fetch_add(1, std::memory_order_relaxed);
    if (checkTime(info)) return 0;
    if (ply >= info.maxPly) info.maxPly = ply + 1;
    if (ply >= 64) return evaluate(b);
    bool ttHit = false;
    TTData ttd = TT.probe(b.key, ttHit);
    if (ttHit) {
        int ttScore = ttd.score;
        if (ttScore >= VALUE_MATE_IN_MAX) ttScore -= ply;
        else if (ttScore <= -VALUE_MATE_IN_MAX) ttScore += ply;
        if (ttd.bound == BOUND_EXACT) return ttScore;
        if (ttd.bound == BOUND_LOWER && ttScore >= beta) return ttScore;
        if (ttd.bound == BOUND_UPPER && ttScore <= alpha) return ttScore;
    }
    bool inCheck = b.inCheck();
    int standPat = -VALUE_INFINITE;
    if (!inCheck) {
        standPat = (ttHit && ttd.eval != VALUE_NONE) ? ttd.eval : evaluate(b);
        if (standPat >= beta) {
            int spTT = standPat >= VALUE_MATE_IN_MAX ? standPat + ply : standPat;
            TT.save(b.key, spTT, standPat, 0, BOUND_LOWER, MOVE_NONE);
            return standPat;
        }
        if (standPat > alpha) alpha = standPat;
    }
    int originalAlpha = alpha;
    int bestScore = standPat;
    Move bestMove = MOVE_NONE;
    if (inCheck) {
        MoveList list;
        generateEvasions(b, list);
        if (list.count == 0) return -VALUE_MATE + ply;
        ScoredMove sm[256];
        int cnt = 0;
        for (int i = 0; i < list.count; i++) {
            Move m = list.moves[i];
            int score = 0;
            if (b.pieceOn[toSq(m)] != NO_PIECE) {
                score = 10000 + mvvLva(typeOf(b.pieceOn[fromSq(m)]), typeOf(b.pieceOn[toSq(m)]));
            }
            sm[cnt].move = m;
            sm[cnt].score = score;
            cnt++;
        }
        std::sort(sm, sm + cnt, [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });
        StateInfo st;
        Color us = b.sideToMove;
        bestScore = -VALUE_INFINITE;
        int legalCount = 0;
        for (int i = 0; i < cnt; i++) {
            Move m = sm[i].move;
            b.makeMove(m, st);
            if (b.inCheck(us)) { b.unmakeMove(m, st); continue; }
            legalCount++;
            int score = -qSearch(b, info, -beta, -alpha, ply + 1, depth - 1);
            b.unmakeMove(m, st);
            if (info.stop.load(std::memory_order_relaxed)) return 0;
            if (score > bestScore) { bestScore = score; bestMove = m; }
            if (score > alpha) alpha = score;
            if (alpha >= beta) break;
        }
        if (legalCount == 0) return -VALUE_MATE + ply;
        int evTT = bestScore;
        if (evTT >= VALUE_MATE_IN_MAX) evTT += ply;
        else if (evTT <= -VALUE_MATE_IN_MAX) evTT -= ply;
        Bound evBound = bestScore >= beta ? BOUND_LOWER : (bestScore > originalAlpha ? BOUND_EXACT : BOUND_UPPER);
        TT.save(b.key, evTT, VALUE_NONE, 0, evBound, bestMove);
        return bestScore;
    }
    MoveList list;
    generateCaptures(b, list);
    ScoredMove sm[256];
    int cnt = 0;
    for (int i = 0; i < list.count; i++) {
        Move m = list.moves[i];
        int score = 0;
        if (isPromo(m)) score = 20000 + (promoType(m) == 3 ? 1000 : 0);
        if (b.pieceOn[toSq(m)] != NO_PIECE) {
            score += 10000 + mvvLva(typeOf(b.pieceOn[fromSq(m)]), typeOf(b.pieceOn[toSq(m)]));
        } else if (isEnPassant(m)) {
            score = 10000 + mvvLva(PAWN, PAWN);
        }
        sm[cnt].move = m;
        sm[cnt].score = score;
        cnt++;
    }
    if (depth >= 0) {
        MoveList quiet;
        generateQuiet(b, quiet);
        for (int i = 0; i < quiet.count && cnt < 256; i++) {
            Move m = quiet.moves[i];
            if (!b.givesCheckFast(m)) continue;
            Piece pc = b.pieceOn[fromSq(m)];
            sm[cnt].move = m;
            sm[cnt].score = 9000 + std::min(999, info.history[pc][toSq(m)]);
            cnt++;
        }
    }
    std::sort(sm, sm + cnt, [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });
    StateInfo st;
    Color us = b.sideToMove;
    int deltaMargin = alpha - standPat - 200;
    for (int i = 0; i < cnt; i++) {
        Move m = sm[i].move;
        if (!isPromo(m) && b.pieceOn[toSq(m)] != NO_PIECE) {
            int victimValue = PieceValue[typeOf(b.pieceOn[toSq(m)])];
            if (victimValue < deltaMargin) continue;
        }
        if (!isPromo(m) && see(b, m) < PHX_QSEE_PRUNE) continue;
        b.makeMove(m, st);
        if (b.inCheck(us)) { b.unmakeMove(m, st); continue; }
        int score = -qSearch(b, info, -beta, -alpha, ply + 1, depth - 1);
        b.unmakeMove(m, st);
        if (info.stop.load(std::memory_order_relaxed)) return 0;
        if (score > bestScore) { bestScore = score; bestMove = m; }
        if (score >= beta) {
            int cTT = score >= VALUE_MATE_IN_MAX ? score + ply : score;
            TT.save(b.key, cTT, standPat, 0, BOUND_LOWER, m);
            return score;
        }
        if (score > alpha) alpha = score;
    }
    {
        int qTT = bestScore;
        if (qTT >= VALUE_MATE_IN_MAX) qTT += ply;
        else if (qTT <= -VALUE_MATE_IN_MAX) qTT -= ply;
        Bound qBound = bestScore > originalAlpha ? BOUND_EXACT : BOUND_UPPER;
        TT.save(b.key, qTT, standPat, 0, qBound, bestMove);
    }
    return alpha;
}
template<bool PVNode>
static int search(Board& b, SearchInfo& info, int depth, int alpha, int beta, int ply, bool cutNode) {
    info.nodes.fetch_add(1, std::memory_order_relaxed);
    if (checkTime(info)) return 0;
    if (ply >= 63) return evaluate(b);
    if (ply >= info.maxPly) info.maxPly = ply + 1;
    info.pvLength[ply] = 0;
    bool root = (ply == 0);
    Color us = b.sideToMove;
    bool inCheck = b.inCheck();
    if (!root) {
        if (b.isDraw(ply)) return VALUE_DRAW;
    }
    alpha = std::max(alpha, -VALUE_MATE + ply);
    beta = std::min(beta, VALUE_MATE - ply - 1);
    if (alpha >= beta) return alpha;
    Move excluded = info.excludedMove[ply];
    bool ttHit = false;
    TTData ttd = TT.probe(b.key, ttHit);
    Move ttMove = MOVE_NONE;
    int ttScore = VALUE_NONE;
    int ttEval = VALUE_NONE;
    int ttDepth = 0;
    Bound ttBound = BOUND_NONE;
    if (ttHit) {
        ttScore = ttd.score;
        ttEval = ttd.eval;
        ttDepth = ttd.depth;
        ttBound = Bound(ttd.bound);
        if (ttScore >= VALUE_MATE_IN_MAX) ttScore -= ply;
        else if (ttScore <= -VALUE_MATE_IN_MAX) ttScore += ply;
        ttMove = validateTTMove(b, Move(ttd.move));
        if (!PVNode && excluded == MOVE_NONE && ttDepth >= depth) {
            if (ttBound == BOUND_EXACT) return ttScore;
            if (ttBound == BOUND_LOWER && ttScore >= beta) return ttScore;
            if (ttBound == BOUND_UPPER && ttScore <= alpha) return ttScore;
        }
    }
    if (inCheck) depth++;
    if (!ttMove && !inCheck && !root && depth >= 4 && !PVNode) depth--;
    if (depth <= 0) {
        return qSearch(b, info, alpha, beta, ply, 0);
    }
    int eval;
    if (ttHit && ttEval != VALUE_NONE) {
        eval = (ttEval + evaluate(b)) / 2;
    } else {
        eval = evaluate(b);
    }
    if (ttHit && excluded == MOVE_NONE && std::abs(ttScore) < VALUE_MATE_IN_MAX) {
        if (ttBound == BOUND_LOWER && ttScore > eval) eval = ttScore;
        else if (ttBound == BOUND_UPPER && ttScore < eval) eval = ttScore;
    }
    bool improving = false;
    if (!inCheck && ply >= 2 && info.staticEval[ply - 2] != VALUE_NONE) {
        improving = eval > info.staticEval[ply - 2];
    }
    info.staticEval[ply] = eval;
    if (!PVNode && !inCheck && depth <= 6 && excluded == MOVE_NONE && std::abs(eval) < VALUE_MATE_IN_MAX) {
        int rfpMar = improving ? PHX_RFP_MARGIN * 3 / 4 : PHX_RFP_MARGIN;
        if (eval - rfpMar * depth >= beta) return eval;
    }
    if (!PVNode && !inCheck && excluded == MOVE_NONE && depth <= 2 && eval + PHX_RAZOR_MAR + 150 * depth <= alpha &&
        std::abs(alpha) < VALUE_MATE_IN_MAX) {
        int razorScore = qSearch(b, info, alpha, beta, ply, 0);
        if (razorScore <= alpha) return razorScore;
    }
    if (!PVNode && !inCheck && excluded == MOVE_NONE && depth >= 3 && eval >= beta && b.hasNonPawnMaterial(us) && ply > 0 &&
        std::abs(beta) < VALUE_MATE_IN_MAX) {
        int R = PHX_NULL_R_BASE / 100 + depth / 4 + (eval - beta) / 200;
        R = std::min(R, depth - 2);
        if (R < 1) R = 1;
        StateInfo st;
        info.currMove[ply] = MOVE_NULL;
        info.currPiece[ply] = NO_PIECE;
        b.makeNullMove(st);
        int nullScore = -search<false>(b, info, depth - 1 - R, -beta, -beta + 1, ply + 1, true);
        b.unmakeNullMove(st);
        if (info.stop.load(std::memory_order_relaxed)) return 0;
        if (nullScore >= beta) {
            if (nullScore >= VALUE_MATE_IN_MAX) return beta;
            return nullScore;
        }
    }
    // ProbCut: if the static eval already suggests a fail-high by a wide margin,
    // try a few sound captures; a shallow wide-window search proving the cutoff
    // is verified with a deeper reduced search before pruning the node.
    if (!PVNode && !root && !inCheck && excluded == MOVE_NONE &&
        depth >= PHX_PC_DEPTH && std::abs(beta) < VALUE_MATE_IN_MAX &&
        eval >= beta + PHX_PC_MARGIN) {
        int pcBeta = beta + PHX_PC_MARGIN;
        MoveList pcList;
        generateCaptures(b, pcList);
        StateInfo pcSt;
        int pcTried = 0;
        for (int i = 0; i < pcList.count && pcTried < PHX_PC_TRIES; i++) {
            Move m = pcList.moves[i];
            if (b.pieceOn[toSq(m)] == NO_PIECE && !isEnPassant(m)) continue;
            if (isPromo(m) && promoType(m) != 3) continue;
            if (see(b, m) < PHX_PC_SEE) continue;
            b.makeMove(m, pcSt);
            if (b.inCheck(us)) { b.unmakeMove(m, pcSt); continue; }
            pcTried++;
            int v = 0;
            int pcScore = -qSearch(b, info, -pcBeta, -pcBeta + 1, ply + 1, 0);
            if (pcScore >= pcBeta) {
                v = -search<false>(b, info, depth - 4, -pcBeta, -pcBeta + 1, ply + 1, !cutNode);
            }
            b.unmakeMove(m, pcSt);
            if (info.stop.load(std::memory_order_relaxed)) return 0;
            if (v >= pcBeta) {
                if (v >= VALUE_MATE_IN_MAX) return pcBeta;
                return v;
            }
        }
    }
    int singular = 0;
    if (!root && excluded == MOVE_NONE && ttMove != MOVE_NONE &&
        depth >= PHX_SE_DEPTH && ttDepth >= depth - 3 && ttBound != BOUND_UPPER &&
        ttScore != VALUE_NONE && std::abs(ttScore) < VALUE_MATE_IN_MAX) {
        int singularBeta = ttScore - PHX_SE_MARGIN * depth;
        info.excludedMove[ply] = ttMove;
        int v = search<false>(b, info, depth / 2, singularBeta - 1, singularBeta, ply, cutNode);
        info.excludedMove[ply] = MOVE_NONE;
        if (info.stop.load(std::memory_order_relaxed)) return 0;
        if (v < singularBeta) singular = 1;
        else if (singularBeta >= beta) return singularBeta;
        else if (ttScore >= beta) singular = -1;
    }
    // Internal Iterative Deepening: at PV nodes without a TT move, run a short
    // null-window probe first so its best move can seed move ordering.
    if (PVNode && !root && !inCheck && excluded == MOVE_NONE && ttMove == MOVE_NONE &&
        depth >= PHX_IID_DEPTH) {
        search<false>(b, info, depth / 2, alpha, alpha + 1, ply, cutNode);
        if (info.stop.load(std::memory_order_relaxed)) return 0;
        bool iidHit = false;
        TTData iidData = TT.probe(b.key, iidHit);
        if (iidHit) ttMove = validateTTMove(b, Move(iidData.move));
    }
    MoveList list;
    generateMoves(b, list);
    Move prevMove = MOVE_NONE;
    Piece prevPc = NO_PIECE;
    Square prevTo = SQ_NONE;
    if (ply >= 1) {
        prevMove = info.currMove[ply - 1];
        if (prevMove != MOVE_NONE && prevMove != MOVE_NULL) {
            Piece p = info.currPiece[ply - 1];
            if (p != NO_PIECE) { prevPc = p; prevTo = toSq(prevMove); }
        }
    }
    Piece prevPc2 = NO_PIECE;
    Square prevTo2 = SQ_NONE;
    if (ply >= 2) {
        Move prevMove2 = info.currMove[ply - 2];
        if (prevMove2 != MOVE_NONE && prevMove2 != MOVE_NULL) {
            Piece p = info.currPiece[ply - 2];
            if (p != NO_PIECE) { prevPc2 = p; prevTo2 = toSq(prevMove2); }
        }
    }
    ScoredMove sm[256];
    int cnt = 0;
    Move bestMove = MOVE_NONE;
    for (int i = 0; i < list.count; i++) {
        Move m = list.moves[i];
        if (m == excluded) continue;
        int score = 0;
        Square from = fromSq(m);
        Square to = toSq(m);
        Piece pc = b.pieceOn[from];
        PieceType pt = typeOf(pc);
        bool isCapture = b.pieceOn[to] != NO_PIECE || isEnPassant(m);
        if (m == ttMove) {
            score = 1000000;
        } else if (isCapture) {
            PieceType victim = isEnPassant(m) ? PAWN : typeOf(b.pieceOn[to]);
            int ch = info.captureHistory[pc][to];
            if (ch > 1500) ch = 1500;
            else if (ch < -1500) ch = -1500;
            score = 900000 + mvvLva(pt, victim) * 100 + ch;
        } else if (m == info.killers[ply][0]) {
            score = 800000;
        } else if (m == info.killers[ply][1]) {
            score = 799999;
        } else if (prevPc != NO_PIECE && prevTo != SQ_NONE &&
                   m == info.counterMove[prevPc][prevTo]) {
            score = 750000;
        } else {
            score = quietHistory(info, ply, pc, to, prevPc, prevTo, prevPc2, prevTo2);
        }
        if (isPromo(m)) {
            score += 500000 + (promoType(m) == 3 ? 100000 : promoType(m) == 2 ? 50000 : 0);
        }
        sm[cnt].move = m;
        sm[cnt].score = score;
        cnt++;
    }
    int bestScore = -VALUE_INFINITE;
    int oldAlpha = alpha;
    int moveCount = 0;
    for (int i = 0; i < cnt; i++) {
        pickNext(sm, cnt, i);
        Move m = sm[i].move;
        Square from = fromSq(m);
        Square to = toSq(m);
        Piece pc = b.pieceOn[from];
        PieceType pt = typeOf(pc);
        bool isCapture = b.pieceOn[to] != NO_PIECE || isEnPassant(m);
        bool isQuiet = !isCapture && !isPromo(m);
        if (!PVNode && !inCheck && depth <= 3 && isQuiet && moveCount >= 3 + 2 * depth) {
            continue;
        }
        if (!PVNode && !inCheck && depth <= 3 && isQuiet && moveCount > 0 &&
            !b.givesCheckFast(m)) {
            int futMar = improving ? PHX_FUT_MAR * 5 / 6 : PHX_FUT_MAR;
            if (eval + futMar * depth + 100 <= alpha) continue;
        }
        if (!PVNode && !inCheck && depth <= 3 && isQuiet && moveCount > 0 &&
            quietHistory(info, ply, pc, to, prevPc, prevTo, prevPc2, prevTo2) < -PHX_HIST_PRUNE * depth) {
            continue;
        }
        if (moveCount > 0 && isCapture && !isPromo(m)) {
            int seeVal = see(b, m);
            if (seeVal < -100 * depth && depth > 1 && !b.givesCheckFast(m)) continue;
        }
        StateInfo st;
        info.currPiece[ply] = pc;
        b.makeMove(m, st);
        if (b.inCheck(us)) { b.unmakeMove(m, st); continue; }
        int extension = 0;
        if (!inCheck && depth >= 3 && moveCount <= 3 &&
            b.givesCheckFast(m) && see(b, m) >= 0) {
            extension = 1;
        }
        if (m == ttMove) {
            if (singular > 0) extension = std::max(extension, 1);
            else if (singular < 0) extension -= 1;
        }
        int score;
        moveCount++;
        info.currMove[ply] = m;
        if (moveCount == 1) {
            score = -search<true>(b, info, depth - 1 + extension, -beta, -alpha, ply + 1, false);
        } else {
            int reduction = 0;
            if (depth >= 3 && moveCount >= 3 && isQuiet) {
                reduction = LMRTable[std::min(63, depth)][std::min(63, moveCount)];
                if (m == info.killers[ply][0] || m == info.killers[ply][1]) reduction -= 1;
                if (inCheck) reduction -= 1;
                if (!PVNode) reduction += 1;
                if (!improving) reduction += 1;
                if (b.givesCheckFast(m)) reduction -= 1;
                int hq = quietHistory(info, ply, pc, to, prevPc, prevTo, prevPc2, prevTo2);
                if (hq > 8000) reduction -= 1;
                else if (hq < -8000) reduction += 1;
                reduction = std::max(0, std::min(reduction, depth - 2));
            }
            int newDepth = depth - 1 + extension - reduction;
            score = -search<false>(b, info, newDepth, -alpha - 1, -alpha, ply + 1, !cutNode ? true : false);
            if (score > alpha && reduction > 0) {
                score = -search<false>(b, info, depth - 1 + extension, -alpha - 1, -alpha, ply + 1, false);
            }
            if (score > alpha && PVNode) {
                score = -search<true>(b, info, depth - 1 + extension, -beta, -alpha, ply + 1, false);
            }
        }
        b.unmakeMove(m, st);
        if (info.stop.load(std::memory_order_relaxed)) return 0;
        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
            if (score > alpha) {
                alpha = score;
                if (PVNode) {
                    int childLen = info.pvLength[ply + 1];
                    if (childLen < 0 || childLen > 63 - ply) childLen = 0;
                    info.pv[ply][0] = m;
                    for (int j = 0; j < childLen; j++) {
                        info.pv[ply][j + 1] = info.pv[ply + 1][j];
                    }
                    info.pvLength[ply] = childLen + 1;
                }
            }
        }
        if (alpha >= beta) {
            if (isQuiet) {
                if (info.killers[ply][0] != m) {
                    info.killers[ply][1] = info.killers[ply][0];
                    info.killers[ply][0] = m;
                }
                int bonus = depth * depth * 4;
                if (bonus > 4096) bonus = 4096;
                int& h = info.history[pc][to];
                addHistoryG(h, bonus);
                updateContEntries(info, ply, pc, to, bonus);
                if (ply >= 1) {
                    Piece prevPcS = info.currPiece[ply - 1];
                    Move prevMS = info.currMove[ply - 1];
                    if (prevPcS != NO_PIECE && prevMS != MOVE_NONE && prevMS != MOVE_NULL)
                        info.counterMove[prevPcS][toSq(prevMS)] = m;
                }
                for (int j = 0; j < i; j++) {
                    Move mj = sm[j].move;
                    if (mj == m) continue;
                    bool mjCapture = b.pieceOn[toSq(mj)] != NO_PIECE || isEnPassant(mj);
                    if (mjCapture || isPromo(mj)) continue;
                    Piece pcj = b.pieceOn[fromSq(mj)];
                    Square toj = toSq(mj);
                    int& hj = info.history[pcj][toj];
                    addHistoryG(hj, -bonus);
                    updateContEntries(info, ply, pcj, toj, -bonus);
                }
            } else if (isCapture) {
                int bonus = depth * depth * 2;
                if (bonus > 2048) bonus = 2048;
                int& ch = info.captureHistory[pc][to];
                addHistoryG(ch, bonus);
                for (int j = 0; j < i; j++) {
                    Move mj = sm[j].move;
                    if (mj == m) continue;
                    bool mjCapture = b.pieceOn[toSq(mj)] != NO_PIECE || isEnPassant(mj);
                    if (!mjCapture || isPromo(mj)) continue;
                    int& chj = info.captureHistory[b.pieceOn[fromSq(mj)]][toSq(mj)];
                    addHistoryG(chj, -bonus);
                }
            }
            break;
        }
    }
    if (moveCount == 0) {
        if (excluded != MOVE_NONE) return alpha;
        if (inCheck) return -VALUE_MATE + ply;
        return VALUE_DRAW;
    }
    Bound bound = bestScore >= beta ? BOUND_LOWER :
                  (bestScore <= oldAlpha ? BOUND_UPPER : BOUND_EXACT);
    int scoreTT = bestScore;
    if (scoreTT >= VALUE_MATE_IN_MAX) scoreTT += ply;
    else if (scoreTT <= -VALUE_MATE_IN_MAX) scoreTT -= ply;
    if (excluded == MOVE_NONE) TT.save(b.key, scoreTT, eval, depth, bound, bestMove);
    return bestScore;
}
Move search(Board& b, SearchLimits limits, SearchInfo& info) {
    info.stop.store(false, std::memory_order_relaxed);
    info.nodes.store(0, std::memory_order_relaxed);
    info.startTime = std::chrono::steady_clock::now();
    info.maxPly = 0;
    info.seldepth = 0;
    info.allocatedTime = limits.moveTime > 0 ? limits.moveTime : limits.timeLimit;
    if (limits.infinite) info.allocatedTime = 0;
    info.pondering = limits.ponder;
    info.ponderClockStarted = false;
    info.nodesLimit = limits.nodesLimit;
    if (info.allocatedTime > 100) info.allocatedTime -= 30;
    std::memset(info.killers, 0, sizeof(info.killers));
    std::memset(info.currMove, 0, sizeof(info.currMove));
    std::memset(info.history, 0, sizeof(info.history));
    std::memset(info.counterMove, 0, sizeof(info.counterMove));
    std::memset(info.captureHistory, 0, sizeof(info.captureHistory));
    std::memset(info.pv, 0, sizeof(info.pv));
    std::memset(info.pvLength, 0, sizeof(info.pvLength));
    std::memset(info.excludedMove, 0, sizeof(info.excludedMove));
    for (int i = 0; i < 64; i++) {
        info.staticEval[i] = VALUE_NONE;
        info.currPiece[i] = NO_PIECE;
    }
    std::memset(contHistory, 0, sizeof(contHistory));
    TT.newSearch();
    Move bestMove = MOVE_NONE;
    int bestScore = 0;
    int prevScore = 0;
    Move prevBest = MOVE_NONE;
    int stableCount = 0;
    for (int depth = 1; depth <= limits.maxDepth; depth++) {
        std::memset(info.pvLength, 0, sizeof(info.pvLength));
        int delta = PHX_ASP_DELTA + depth * depth / 8;
        int alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
        if (depth >= 4 && std::abs(prevScore) < VALUE_MATE_IN_MAX) {
            alpha = prevScore - delta;
            beta = prevScore + delta;
        }
        int score;
        while (true) {
            info.pvLength[0] = 0;
            score = search<true>(b, info, depth, alpha, beta, 0, false);
            if (info.stop.load(std::memory_order_relaxed)) break;
            if (score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max(-VALUE_INFINITE, score - delta);
            } else if (score >= beta) {
                alpha = (alpha + beta) / 2;
                beta = std::min(VALUE_INFINITE, score + delta);
            } else {
                break;
            }
            delta += delta / 2;
        }
        if (info.stop.load(std::memory_order_relaxed) && bestMove != MOVE_NONE) {
            break;
        }
        if (info.pvLength[0] > 0) {
            bestMove = info.pv[0][0];
            bestScore = score;
        }
        prevScore = score;
        if (bestMove == prevBest && bestMove != MOVE_NONE) stableCount++;
        else stableCount = 0;
        prevBest = bestMove;
        auto now = std::chrono::steady_clock::now();
        long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - info.startTime).count();
        long long nps = elapsed > 0 ? (info.nodes.load() * 1000 / elapsed) : 0;
        std::ostringstream os;
        os << "info";
        os << " depth " << depth;
        os << " seldepth " << info.maxPly;
        os << " nodes " << info.nodes.load();
        if (nps > 0) os << " nps " << nps;
        os << " time " << elapsed;
        os << " hashfull " << TT.hashfull();
        if (std::abs(score) >= VALUE_MATE_IN_MAX) {
            int mateIn = (VALUE_MATE - std::abs(score) + 1) / 2;
            if (score < 0) mateIn = -mateIn;
            os << " score mate " << mateIn;
        } else {
            os << " score cp " << score;
        }
        os << " pv";
        for (int i = 0; i < info.pvLength[0] && i < 64; i++) {
            os << " " << moveToUci(info.pv[0][i]);
        }
        uciOutLine(os.str());
        if (std::abs(score) >= VALUE_MATE_IN_MAX &&
            (!info.pondering || GlobalPonderHit.load(std::memory_order_relaxed))) break;
        if (checkTime(info)) break;
        if (info.allocatedTime > 0 && depth >= 6 && stableCount >= 4 &&
            (!info.pondering || GlobalPonderHit.load(std::memory_order_relaxed))) {
            auto now2 = std::chrono::steady_clock::now();
            long long elapsed2 = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - info.startTime).count();
            if (elapsed2 >= info.allocatedTime * 60 / 100) break;
        }
    }
    return bestMove;
}
static void printUciInfo(SearchInfo* info, int depth, int score, long long nodes) {
    auto now = std::chrono::steady_clock::now();
    long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - info->startTime).count();
    std::ostringstream os;
    os << "info depth " << depth;
    os << " seldepth " << info->maxPly;
    os << " nodes " << nodes;
    long long nps = elapsed > 0 ? (nodes * 1000 / elapsed) : 0;
    if (nps > 0) os << " nps " << nps;
    os << " time " << elapsed;
    os << " hashfull " << TT.hashfull();
    if (std::abs(score) >= VALUE_MATE_IN_MAX) {
        int mateIn = (VALUE_MATE - std::abs(score) + 1) / 2;
        if (score < 0) mateIn = -mateIn;
        os << " score mate " << mateIn;
    } else {
        os << " score cp " << score;
    }
    os << " pv";
    for (int i = 0; i < info->pvLength[0] && i < 64; i++) {
        os << " " << moveToUci(info->pv[0][i]);
    }
    uciOutLine(os.str());
}
static void smpThread(Board b, SearchLimits limits, SearchInfo* info, SharedSearchData* shared, int threadId) {
    int startDepth = 1 + (threadId % 2);
    info->stop.store(false, std::memory_order_relaxed);
    info->nodes.store(0, std::memory_order_relaxed);
    info->startTime = std::chrono::steady_clock::now();
    info->maxPly = 0;
    info->allocatedTime = limits.moveTime > 0 ? limits.moveTime : limits.timeLimit;
    if (limits.infinite) info->allocatedTime = 0;
    info->pondering = limits.ponder;
    info->ponderClockStarted = false;
    info->nodesLimit = limits.nodesLimit;
    if (info->allocatedTime > 100) info->allocatedTime -= 30;
    std::memset(info->killers, 0, sizeof(info->killers));
    std::memset(info->currMove, 0, sizeof(info->currMove));
    std::memset(info->history, 0, sizeof(info->history));
    std::memset(info->counterMove, 0, sizeof(info->counterMove));
    std::memset(info->captureHistory, 0, sizeof(info->captureHistory));
    std::memset(info->pv, 0, sizeof(info->pv));
    std::memset(info->pvLength, 0, sizeof(info->pvLength));
    std::memset(info->excludedMove, 0, sizeof(info->excludedMove));
    for (int i = 0; i < 64; i++) {
        info->staticEval[i] = VALUE_NONE;
        info->currPiece[i] = NO_PIECE;
    }
    std::memset(contHistory, 0, sizeof(contHistory));
    int prevScore = 0;
    Move localBestMove = MOVE_NONE;
    int localBestScore = 0;
    int localBestDepth = 0;
    Move prevBest = MOVE_NONE;
    int stableCount = 0;
    for (int depth = startDepth; depth <= limits.maxDepth; depth++) {
        std::memset(info->pvLength, 0, sizeof(info->pvLength));
        int delta = PHX_ASP_DELTA + depth * depth / 8;
        int alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
        if (depth >= 4 && std::abs(prevScore) < VALUE_MATE_IN_MAX) {
            alpha = prevScore - delta;
            beta = prevScore + delta;
        }
        int score;
        while (true) {
            info->pvLength[0] = 0;
            score = search<true>(b, *info, depth, alpha, beta, 0, false);
            if (info->stop.load(std::memory_order_relaxed) || GlobalStop.load(std::memory_order_relaxed)) break;
            if (score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max(-VALUE_INFINITE, score - delta);
            } else if (score >= beta) {
                alpha = (alpha + beta) / 2;
                beta = std::min(VALUE_INFINITE, score + delta);
            } else {
                break;
            }
            delta += delta / 2;
        }
        bool stopped = info->stop.load(std::memory_order_relaxed) || GlobalStop.load(std::memory_order_relaxed);
        if (!stopped && info->pvLength[0] > 0) {
            localBestMove = info->pv[0][0];
            localBestScore = score;
            localBestDepth = depth;
            prevScore = score;
            if (depth > shared->bestDepth.load(std::memory_order_relaxed)) {
                shared->bestMoveGuard.store(uint32_t(depth), std::memory_order_relaxed);
                shared->bestMove.store(uint32_t(info->pv[0][0]), std::memory_order_relaxed);
                shared->bestScore.store(score, std::memory_order_relaxed);
                shared->bestDepth.store(depth, std::memory_order_relaxed);
            }
            if (threadId == 0) {
                printUciInfo(info, depth, score, info->nodes.load());
                if (localBestMove == prevBest) stableCount++;
                else stableCount = 0;
                prevBest = localBestMove;
                if ((!info->pondering || GlobalPonderHit.load(std::memory_order_relaxed)) &&
                    info->allocatedTime > 0 && depth >= 6 && stableCount >= 4) {
                    auto now2 = std::chrono::steady_clock::now();
                    long long elapsed2 = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - info->startTime).count();
                    if (elapsed2 >= info->allocatedTime * 60 / 100) {
                        shared->stop.store(true, std::memory_order_relaxed);
                        GlobalStop.store(true, std::memory_order_relaxed);
                    }
                }
            }
        }
        if (stopped && localBestMove != MOVE_NONE) break;
        if (std::abs(score) >= VALUE_MATE_IN_MAX &&
            (!info->pondering || GlobalPonderHit.load(std::memory_order_relaxed))) {
            if (info->pvLength[0] > 0 && score >= shared->bestScore.load(std::memory_order_relaxed)) {
                shared->bestMoveGuard.store(uint32_t(depth), std::memory_order_relaxed);
                shared->bestMove.store(uint32_t(info->pv[0][0]), std::memory_order_relaxed);
                shared->bestScore.store(score, std::memory_order_relaxed);
                shared->bestDepth.store(depth, std::memory_order_relaxed);
                if (threadId != 0) printUciInfo(info, depth, score, info->nodes.load());
            }
            shared->stop.store(true, std::memory_order_relaxed);
            GlobalStop.store(true, std::memory_order_relaxed);
            break;
        }
        if (info->allocatedTime > 0 &&
            (!info->pondering || GlobalPonderHit.load(std::memory_order_relaxed))) {
            auto now = std::chrono::steady_clock::now();
            long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - info->startTime).count();
            if (elapsed >= info->allocatedTime) {
                shared->stop.store(true, std::memory_order_relaxed);
                GlobalStop.store(true, std::memory_order_relaxed);
                break;
            }
        }
        if (shared->stop.load(std::memory_order_relaxed) || GlobalStop.load(std::memory_order_relaxed)) break;
    }
    // Signal completion so the Lazy SMP monitor does not linger after the
    // depth limit has been reached by every worker.
    shared->stop.store(true, std::memory_order_relaxed);
    info->seldepth = localBestDepth;
    if (threadId == 0) {
        if (localBestMove != MOVE_NONE && localBestDepth >= shared->bestDepth.load(std::memory_order_relaxed)) {
            shared->bestMoveGuard.store(uint32_t(localBestDepth), std::memory_order_relaxed);
            shared->bestMove.store(uint32_t(localBestMove), std::memory_order_relaxed);
            shared->bestScore.store(localBestScore, std::memory_order_relaxed);
            shared->bestDepth.store(localBestDepth, std::memory_order_relaxed);
        }
    }
}
Move searchLazySMP(Board& b, SearchLimits limits, int numThreads) {
    if (numThreads <= 1) {
        SearchInfo info;
        return search(b, limits, info);
    }
    SharedSearchData shared;
    shared.stop.store(false, std::memory_order_relaxed);
    shared.bestDepth.store(0, std::memory_order_relaxed);
    shared.bestScore.store(0, std::memory_order_relaxed);
    shared.bestMove.store(uint32_t(MOVE_NONE), std::memory_order_relaxed);
    shared.bestMoveGuard.store(0, std::memory_order_relaxed);
    shared.board = &b;
    shared.limits = &limits;
    shared.numThreads = numThreads;
    TT.newSearch();
    std::vector<std::thread> threads;
    std::vector<std::unique_ptr<SearchInfo>> infos;
    for (int i = 0; i < numThreads; i++) {
        infos.push_back(std::make_unique<SearchInfo>());
    }
    auto startTime = std::chrono::steady_clock::now();
    long long allocatedTime = limits.moveTime > 0 ? limits.moveTime : limits.timeLimit;
    if (allocatedTime > 100) allocatedTime -= 30;
    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back(smpThread, b, limits, infos[i].get(), &shared, i);
    }
    if (allocatedTime > 0) {
        auto clockStart = startTime;
        bool clockRunning = !limits.ponder;
        while (!shared.stop.load(std::memory_order_relaxed) && !GlobalStop.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (limits.ponder && !GlobalPonderHit.load(std::memory_order_relaxed)) continue;
            if (!clockRunning) {
                clockRunning = true;
                clockStart = std::chrono::steady_clock::now();
            }
            auto now = std::chrono::steady_clock::now();
            long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - clockStart).count();
            if (elapsed >= allocatedTime) {
                GlobalStop.store(true, std::memory_order_relaxed);
                shared.stop.store(true, std::memory_order_relaxed);
                break;
            }
        }
    }
    for (auto& t : threads) t.join();
    GlobalStop.store(false, std::memory_order_relaxed);
    Move finalBest = Move(shared.bestMove.load(std::memory_order_relaxed));
    if (finalBest == MOVE_NONE && infos[0]->pvLength[0] > 0)
        finalBest = infos[0]->pv[0][0];
    return finalBest;
}
}
