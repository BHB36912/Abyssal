#include "board.h"
#include "magic.h"
#include "movegen.h"
#include <sstream>
#include <stdexcept>
#include <random>
#include <algorithm>
namespace Chess {
Zobrist zob;
const int PieceValue[6] = { 100, 320, 330, 500, 900, 0 };
void initZobrist() {
    std::mt19937_64 rng(0x9E3779B97F4A7C15ULL);
    for (int p = 0; p < 16; p++)
        for (int s = 0; s < 64; s++)
            zob.psq[p][s] = rng();
    zob.side[WHITE] = 0;
    zob.side[BLACK] = rng();
    for (int i = 0; i < 16; i++) zob.castling[i] = rng();
    for (int i = 0; i < 8; i++) zob.epFile[i] = rng();
}
void Board::clear() {
    for (int i = 0; i < 2; i++) piecesByColor[i] = 0;
    for (int i = 0; i < 6; i++) piecesByType[i] = 0;
    occupied = 0;
    for (int i = 0; i < 64; i++) pieceOn[i] = NO_PIECE;
    castling = NO_CASTLING;
    epSquare = SQ_NONE;
    halfmoveClock = 0;
    fullmove = 1;
    sideToMove = WHITE;
    key = 0;
    checkers = 0;
    kingSquare[WHITE] = SQ_NONE;
    kingSquare[BLACK] = SQ_NONE;
    blockBB = 0;
    historySize = 0;
    nnue.zero();
}
void Board::nnueRefresh() {
    if (kingSquare[WHITE] >= 64 || kingSquare[BLACK] >= 64) {
        nnue.zero();
        return;
    }
    nnue.rebuild(pieceOn, kingSquare[WHITE], kingSquare[BLACK]);
}
bool Board::setFen(const std::string& fen) {
    clear();
    std::istringstream iss(fen);
    std::string boardPart, sidePart, castlingPart, epPart;
    int halfmove = 0, fullmove = 1;
    iss >> boardPart >> sidePart >> castlingPart >> epPart;
    iss >> halfmove >> fullmove;
    if (iss.fail()) {
        halfmove = 0;
        fullmove = 1;
    }
    if (castlingPart.empty()) castlingPart = "-";
    if (epPart.empty()) epPart = "-";
    if (boardPart.empty() || sidePart.empty()) return false;
    if (sidePart != "w" && sidePart != "b") return false;
    int rank = 7, file = 0;
    bool sawPiece = false;
    for (char c : boardPart) {
        if (c == '/') {
            if (file != 8 || rank <= 0) return false;
            rank--;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return false;
        } else {
            if (file > 7 || rank < 0) return false;
            Piece p = NO_PIECE;
            switch (c) {
                case 'P': p = W_PAWN; break;
                case 'N': p = W_KNIGHT; break;
                case 'B': p = W_BISHOP; break;
                case 'R': p = W_ROOK; break;
                case 'Q': p = W_QUEEN; break;
                case 'K': p = W_KING; break;
                case 'p': p = B_PAWN; break;
                case 'n': p = B_KNIGHT; break;
                case 'b': p = B_BISHOP; break;
                case 'r': p = B_ROOK; break;
                case 'q': p = B_QUEEN; break;
                case 'k': p = B_KING; break;
                default: return false;
            }
            sawPiece = true;
            Square sq = makeSquare(File(file), Rank(rank));
            pieceOn[sq] = p;
            piecesByColor[colorOf(p)] |= sq;
            piecesByType[typeOf(p)] |= sq;
            occupied |= sq;
            if (typeOf(p) == KING) kingSquare[colorOf(p)] = sq;
            file++;
        }
    }
    if (!sawPiece || file != 8) return false;
    sideToMove = (sidePart == "w") ? WHITE : BLACK;
    castling = NO_CASTLING;
    if (castlingPart != "-") {
        for (char c : castlingPart) {
            switch (c) {
                case 'K': castling |= WHITE_OO; break;
                case 'Q': castling |= WHITE_OOO; break;
                case 'k': castling |= BLACK_OO; break;
                case 'q': castling |= BLACK_OOO; break;
                default: break;
            }
        }
        auto checkRight = [&](CastlingRights cr, Square ksq, Square rsq,
                              Piece kingPc, Piece rookPc) {
            if (!(castling & cr)) return;
            if (pieceOn[ksq] != kingPc || pieceOn[rsq] != rookPc)
                castling = CastlingRights(castling & ~cr);
        };
        checkRight(WHITE_OO,  SQ_E1, SQ_H1, W_KING, W_ROOK);
        checkRight(WHITE_OOO, SQ_E1, SQ_A1, W_KING, W_ROOK);
        checkRight(BLACK_OO,  SQ_E8, SQ_H8, B_KING, B_ROOK);
        checkRight(BLACK_OOO, SQ_E8, SQ_A8, B_KING, B_ROOK);
    }
    if (epPart != "-" && epPart.size() >= 2 &&
        epPart[0] >= 'a' && epPart[0] <= 'h' && epPart[1] >= '1' && epPart[1] <= '8') {
        File f = File(epPart[0] - 'a');
        Rank r = Rank(epPart[1] - '1');
        epSquare = makeSquare(f, r);
        {
            Rank wantRank = (sideToMove == WHITE) ? RANK_6 : RANK_3;
            Square pusherSq = (sideToMove == WHITE) ? Square(epSquare - 8)
                                                    : Square(epSquare + 8);
            Piece pusher  = (sideToMove == WHITE) ? B_PAWN : W_PAWN;
            if (rankOf(epSquare) != wantRank || (occupied & epSquare) ||
                pusherSq < 0 || pusherSq > 63 || pieceOn[pusherSq] != pusher)
                epSquare = SQ_NONE;
            else if (!(PawnAttacks[Color(sideToMove ^ 1)][epSquare] &
                       piecesByColor[sideToMove] & piecesByType[PAWN]))
                epSquare = SQ_NONE;
        }
    } else if (epPart != "-") {
        return false;
    } else {
        epSquare = SQ_NONE;
    }
    halfmoveClock = halfmove;
    this->fullmove = fullmove;
    computeKeys();
    if ((piecesByColor[WHITE] & piecesByType[KING]) == 0 ||
        (piecesByColor[BLACK] & piecesByType[KING]) == 0 ||
        popcount(piecesByType[KING]) != 2)
        return false;
    if (piecesByType[PAWN] & (RANK_1_BB | RANK_8_BB)) return false;
    if (KingAttacks[kingSquare[WHITE]] & piecesByColor[BLACK] & piecesByType[KING])
        return false;
    kingSquare[WHITE] = lsb(piecesByColor[WHITE] & piecesByType[KING]);
    kingSquare[BLACK] = lsb(piecesByColor[BLACK] & piecesByType[KING]);
    Color us = sideToMove;
    Color them = Color(us ^ 1);
    checkers = attackersTo(kingSquare[us], them, occupied);
    blockBB = 0;
    if (checkers) {
        if (popcount(checkers) == 1) {
            Square cs = lsb(checkers);
            if (BetweenBB[kingSquare[us]][cs])
                blockBB = BetweenBB[kingSquare[us]][cs] | squareBB(cs);
            else
                blockBB = squareBB(cs);
        }
    }
    nnueRefresh();
    return true;
}
std::string Board::fen() const {
    static const char* pieceChars = "PNBRQK..pnbrqk.";
    std::string s;
    for (int r = 7; r >= 0; r--) {
        int empties = 0;
        for (int f = 0; f < 8; f++) {
            Square sq = makeSquare(File(f), Rank(r));
            Piece p = pieceOn[sq];
            if (p == NO_PIECE) {
                empties++;
            } else {
                if (empties > 0) { s += char('0' + empties); empties = 0; }
                s += pieceChars[p];
            }
        }
        if (empties > 0) s += char('0' + empties);
        if (r > 0) s += '/';
    }
    s += sideToMove == WHITE ? " w " : " b ";
    if (castling == NO_CASTLING) {
        s += '-';
    } else {
        if (castling & WHITE_OO) s += 'K';
        if (castling & WHITE_OOO) s += 'Q';
        if (castling & BLACK_OO) s += 'k';
        if (castling & BLACK_OOO) s += 'q';
    }
    s += ' ';
    if (epSquare == SQ_NONE) {
        s += '-';
    } else {
        s += char('a' + fileOf(epSquare));
        s += char('1' + rankOf(epSquare));
    }
    s += ' ';
    s += std::to_string(halfmoveClock);
    s += ' ';
    s += std::to_string(fullmove);
    return s;
}
void Board::computeKeys() {
    key = 0;
    for (Square s = SQ_A1; s < 64; s++) {
        Piece p = pieceOn[s];
        if (p == NO_PIECE) continue;
        key ^= zob.psq[p][s];
    }
    if (sideToMove == BLACK) key ^= zob.side[BLACK];
    key ^= zob.castling[int(castling)];
    if (epSquare != SQ_NONE &&
        (PawnAttacks[Color(sideToMove ^ 1)][epSquare] &
         piecesByColor[sideToMove] & piecesByType[PAWN]))
        key ^= zob.epFile[fileOf(epSquare)];
}
bool Board::isSquareAttacked(Square s, Color byColor) const {
    Bitboard attackers = attackersTo(s, byColor, occupied);
    return attackers != 0;
}
Bitboard Board::attackersTo(Square s, Color byColor, Bitboard occ) const {
    if (s >= 64) return 0;
    int bc = int(byColor) & 1;
    Bitboard attackers = 0;
    attackers |= PawnAttacks[bc ^ 1][s] & piecesByColor[bc] & piecesByType[PAWN];
    attackers |= KnightAttacks[s] & piecesByColor[bc] & piecesByType[KNIGHT];
    attackers |= KingAttacks[s] & piecesByColor[bc] & piecesByType[KING];
    attackers |= attacksBishop(s, occ) & piecesByColor[bc] & (piecesByType[BISHOP] | piecesByType[QUEEN]);
    attackers |= attacksRook(s, occ) & piecesByColor[bc] & (piecesByType[ROOK] | piecesByType[QUEEN]);
    return attackers;
}
Bitboard Board::attackersTo(Square s, Bitboard occ) const {
    Bitboard attackers = 0;
    attackers |= PawnAttacks[WHITE][s] & piecesByColor[BLACK] & piecesByType[PAWN];
    attackers |= PawnAttacks[BLACK][s] & piecesByColor[WHITE] & piecesByType[PAWN];
    attackers |= KnightAttacks[s] & (piecesByType[KNIGHT]);
    attackers |= KingAttacks[s] & (piecesByType[KING]);
    attackers |= attacksBishop(s, occ) & (piecesByType[BISHOP] | piecesByType[QUEEN]);
    attackers |= attacksRook(s, occ) & (piecesByType[ROOK] | piecesByType[QUEEN]);
    return attackers;
}
bool Board::inCheck(Color c) const {
    return isSquareAttacked(kingSquare[c], Color(c ^ 1));
}
bool Board::hasNonPawnMaterial(Color c) const {
    return (piecesByColor[c] & ~piecesByType[PAWN] & ~piecesByType[KING]) != 0;
}
bool Board::hasNonPawnMaterial() const {
    return hasNonPawnMaterial(WHITE) || hasNonPawnMaterial(BLACK);
}
bool Board::isRepetition(int ply) const {
    int scan = halfmoveClock;
    if (scan > MAX_HISTORY - 1) scan = MAX_HISTORY - 1;
    if (scan > historySize - 1) scan = historySize - 1;
    for (int i = historySize - 1; i >= 0 && i >= historySize - 1 - scan; i--) {
        if (historyArray[i & (MAX_HISTORY - 1)] == key) return true;
    }
    return false;
}
bool Board::isDraw(int ply) const {
    if (halfmoveClock >= 100) {
        if (!inCheck()) return true;
        MoveList list;
        generateEvasions(*this, list);
        Board tmp;
        StateInfo st;
        for (int i = 0; i < list.count; i++) {
            tmp = *this;
            tmp.makeMove(list.moves[i], st);
            if (!tmp.inCheck(sideToMove)) return true;
        }
        return false;
    }
    if (isRepetition(ply)) return true;
    Bitboard allPawns = piecesByType[PAWN];
    if (allPawns != 0) return false;
    Bitboard whiteRookQueen = pieces(WHITE, ROOK) | pieces(WHITE, QUEEN);
    Bitboard blackRookQueen = pieces(BLACK, ROOK) | pieces(BLACK, QUEEN);
    Bitboard whiteMinors = pieces(WHITE, KNIGHT) | pieces(WHITE, BISHOP);
    Bitboard blackMinors = pieces(BLACK, KNIGHT) | pieces(BLACK, BISHOP);
    if (whiteRookQueen || blackRookQueen) return false;
    if (!whiteMinors && !blackMinors) return true;
    if (popcount(whiteMinors) <= 1 && !blackMinors) return true;
    if (popcount(blackMinors) <= 1 && !whiteMinors) return true;
    if (popcount(whiteMinors) == 1 && popcount(blackMinors) == 1) return true;
    return false;
}
bool Board::givesCheckFast(Move m) const {
    Square from = fromSq(m);
    Square to = toSq(m);
    Color us = sideToMove;
    Color them = Color(us ^ 1);
    Square ksq = kingSquare[them];
    PieceType pt = typeOf(pieceOn[from]);
    Bitboard attacks = 0;
    switch (pt) {
        case PAWN:
            attacks = PawnAttacks[us][to];
            break;
        case KNIGHT:
            attacks = KnightAttacks[to];
            break;
        case BISHOP:
            attacks = attacksBishop(to, occupied ^ from);
            break;
        case ROOK:
            attacks = attacksRook(to, occupied ^ from);
            break;
        case QUEEN:
            attacks = attacksBishop(to, occupied ^ from) | attacksRook(to, occupied ^ from);
            break;
        case KING:
            attacks = KingAttacks[to];
            break;
        default: break;
    }
    if (attacks & ksq) return true;
    if (isPromo(m)) {
        PieceType promo = PieceType(promoType(m) + 1);
        if (promo == KNIGHT && (KnightAttacks[to] & ksq)) return true;
        if (promo == BISHOP && (attacksBishop(to, occupied ^ from) & ksq)) return true;
        if (promo == ROOK && (attacksRook(to, occupied ^ from) & ksq)) return true;
        if (promo == QUEEN && ((attacksBishop(to, occupied ^ from) | attacksRook(to, occupied ^ from)) & ksq)) return true;
    }
    {
        Bitboard occAfter = (occupied ^ squareBB(from)) | squareBB(to);
        Square rFrom = SQ_NONE, rTo = SQ_NONE;
        if (isEnPassant(m)) {
            Square capSq = (us == WHITE) ? Square(to - 8) : Square(to + 8);
            occAfter ^= squareBB(capSq);
        }
        if (isCastling(m)) {
            rTo = (to == SQ_G1) ? SQ_F1 : (to == SQ_C1) ? SQ_D1
                : (to == SQ_G8) ? SQ_F8 : SQ_D8;
            rFrom = (to == SQ_G1) ? SQ_H1 : (to == SQ_C1) ? SQ_A1
                  : (to == SQ_G8) ? SQ_H8 : SQ_A8;
            occAfter ^= squareBB(rFrom) | squareBB(rTo);
        }
        Bitboard ourRQ = pieces(us, ROOK) | pieces(us, QUEEN);
        Bitboard ourBQ = pieces(us, BISHOP) | pieces(us, QUEEN);
        Bitboard movedBit = squareBB(from) | squareBB(to);
        if (pt == QUEEN)      { ourRQ ^= movedBit; ourBQ ^= movedBit; }
        else if (pt == ROOK)  { ourRQ ^= movedBit; }
        else if (pt == BISHOP){ ourBQ ^= movedBit; }
        if (isCastling(m))
            ourRQ ^= (squareBB(rFrom) | squareBB(rTo));
        if ((attacksRook(ksq, occAfter) & ourRQ & ~squareBB(to)) ||
            (attacksBishop(ksq, occAfter) & ourBQ & ~squareBB(to)))
            return true;
    }
    if (isCastling(m)) {
        Square rTo = (to == SQ_G1) ? SQ_F1 : (to == SQ_C1) ? SQ_D1 : (to == SQ_G8) ? SQ_F8 : SQ_D8;
        if (attacksRook(rTo, occupied ^ from ^ to) & ksq) return true;
    }
    if (isEnPassant(m)) {
        Square capSq = (us == WHITE) ? Square(to - 8) : Square(to + 8);
        Bitboard occ2 = occupied ^ from ^ capSq ^ to;
        if ((attacksRook(ksq, occ2) & (pieces(us, ROOK) | pieces(us, QUEEN))) ||
            (attacksBishop(ksq, occ2) & (pieces(us, BISHOP) | pieces(us, QUEEN))))
            return true;
    }
    return false;
}
void Board::makeMove(Move m, StateInfo& st) {
    st.capturedPiece = NO_PIECE;
    st.capturedSquare = SQ_NONE;
    st.oldCastling = castling;
    st.oldEpSquare = epSquare;
    st.oldHalfmoveClock = halfmoveClock;
    st.oldKey = key;
    st.oldCheckers = checkers;
    st.oldKingSquare[0] = kingSquare[0];
    st.oldKingSquare[1] = kingSquare[1];
    st.oldBlockBB = blockBB;
    Color us = sideToMove;
    Color them = Color(us ^ 1);
    Square from = fromSq(m);
    Square to = toSq(m);
    Piece pc = pieceOn[from];
    PieceType pt = typeOf(pc);
    const bool nnueKingsValid = kingSquare[WHITE] < 64 && kingSquare[BLACK] < 64;
    const Square nnueWKing = kingSquare[WHITE];
    const Square nnueBKing = kingSquare[BLACK];
    const bool nnueMustRefresh = !nnueKingsValid || ((pt == KING) &&
        (KING_ZONE[us == WHITE ? from : Square(from ^ 56)] !=
         KING_ZONE[us == WHITE ? to   : Square(to ^ 56)]));
    historyArray[historySize & (MAX_HISTORY - 1)] = key;
    historySize++;
    if (pt == PAWN || pieceOn[to] != NO_PIECE || isEnPassant(m))
        halfmoveClock = 0;
    else
        halfmoveClock++;
    if (us == BLACK) fullmove++;
    if (isEnPassant(m)) {
        Square capSq = (us == WHITE) ? Square(to - 8) : Square(to + 8);
        Piece captured = pieceOn[capSq];
        st.capturedPiece = captured;
        st.capturedSquare = capSq;
        key ^= zob.psq[captured][capSq];
        piecesByColor[colorOf(captured)] ^= capSq;
        piecesByType[typeOf(captured)] ^= capSq;
        occupied ^= capSq;
        pieceOn[capSq] = NO_PIECE;
        if (!nnueMustRefresh)
            nnue.remove(typeOf(captured), capSq, colorOf(captured), nnueWKing, nnueBKing);
    } else if (pieceOn[to] != NO_PIECE) {
        Piece captured = pieceOn[to];
        st.capturedPiece = captured;
        st.capturedSquare = to;
        key ^= zob.psq[captured][to];
        piecesByColor[colorOf(captured)] ^= to;
        piecesByType[typeOf(captured)] ^= to;
        occupied ^= to;
        if (!nnueMustRefresh)
            nnue.remove(typeOf(captured), to, colorOf(captured), nnueWKing, nnueBKing);
    }
    key ^= zob.psq[pc][from] ^ zob.psq[pc][to];
    piecesByColor[us] ^= (squareBB(from) | squareBB(to));
    piecesByType[pt] ^= (squareBB(from) | squareBB(to));
    occupied ^= (squareBB(from) | squareBB(to));
    pieceOn[from] = NO_PIECE;
    pieceOn[to] = pc;
    if (!nnueMustRefresh)
        nnue.update(pt, from, to, us, nnueWKing, nnueBKing);
    if (isPromo(m)) {
        PieceType promoPt = promoType(m);
        PieceType actualPromo = PieceType(promoPt + 1);
        Piece promoPc = makePiece(us, actualPromo);
        key ^= zob.psq[pc][to] ^ zob.psq[promoPc][to];
        piecesByType[PAWN] ^= to;
        piecesByType[actualPromo] ^= to;
        pieceOn[to] = promoPc;
        if (!nnueMustRefresh) {
            nnue.remove(PAWN, to, us, nnueWKing, nnueBKing);
            nnue.add(actualPromo, to, us, nnueWKing, nnueBKing);
        }
    }
    if (isCastling(m)) {
        Square rFrom, rTo;
        if (to == SQ_G1) { rFrom = SQ_H1; rTo = SQ_F1; }
        else if (to == SQ_C1) { rFrom = SQ_A1; rTo = SQ_D1; }
        else if (to == SQ_G8) { rFrom = SQ_H8; rTo = SQ_F8; }
        else { rFrom = SQ_A8; rTo = SQ_D8; }
        Piece rookPc = pieceOn[rFrom];
        key ^= zob.psq[rookPc][rFrom] ^ zob.psq[rookPc][rTo];
        piecesByColor[us] ^= (squareBB(rFrom) | squareBB(rTo));
        piecesByType[ROOK] ^= (squareBB(rFrom) | squareBB(rTo));
        occupied ^= (squareBB(rFrom) | squareBB(rTo));
        pieceOn[rFrom] = NO_PIECE;
        pieceOn[rTo] = rookPc;
        if (!nnueMustRefresh)
            nnue.update(ROOK, rFrom, rTo, us, nnueWKing, nnueBKing);
    }
    if (pt == KING) kingSquare[us] = to;
    if (nnueMustRefresh) nnueRefresh();
    CastlingRights newCastling = castling;
    if (from == SQ_A1 || to == SQ_A1) newCastling = CastlingRights(newCastling & ~WHITE_OOO);
    if (from == SQ_H1 || to == SQ_H1) newCastling = CastlingRights(newCastling & ~WHITE_OO);
    if (from == SQ_E1) newCastling = CastlingRights(newCastling & ~(WHITE_OO | WHITE_OOO));
    if (from == SQ_A8 || to == SQ_A8) newCastling = CastlingRights(newCastling & ~BLACK_OOO);
    if (from == SQ_H8 || to == SQ_H8) newCastling = CastlingRights(newCastling & ~BLACK_OO);
    if (from == SQ_E8) newCastling = CastlingRights(newCastling & ~(BLACK_OO | BLACK_OOO));
    if (newCastling != castling) {
        key ^= zob.castling[int(castling)] ^ zob.castling[int(newCastling)];
        castling = newCastling;
    }
    if (epSquare != SQ_NONE) {
        key ^= zob.epFile[fileOf(epSquare)];
        epSquare = SQ_NONE;
    }
    if (pt == PAWN && std::abs(int(to) - int(from)) == 16) {
        epSquare = Square((from + to) / 2);
        if (PawnAttacks[us][epSquare] & (piecesByColor[them] & piecesByType[PAWN])) {
            key ^= zob.epFile[fileOf(epSquare)];
        } else {
            epSquare = SQ_NONE;
        }
    }
    key ^= zob.side[BLACK];
    sideToMove = them;
    Color newSide = sideToMove;
    Square newKingSq = kingSquare[newSide];
    if (newKingSq >= 64) {
        checkers = 0; blockBB = 0;
        return;
    }
    checkers = attackersTo(newKingSq, Color(newSide ^ 1), occupied);
    blockBB = 0;
    if (checkers && popcount(checkers) == 1) {
        Square cs = lsb(checkers);
        if (BetweenBB[newKingSq][cs])
            blockBB = BetweenBB[newKingSq][cs] | squareBB(cs);
        else
            blockBB = squareBB(cs);
    }
}
void Board::unmakeMove(Move m, const StateInfo& st) {
    sideToMove = Color(sideToMove ^ 1);
    Color us = sideToMove;
    Square from = fromSq(m);
    Square to = toSq(m);
    Piece pc = pieceOn[to];
    PieceType pt = typeOf(pc);
    const PieceType moverChildPt = pt;
    const bool nnueKingsValid = kingSquare[WHITE] < 64 && kingSquare[BLACK] < 64;
    const bool nnueMustRefresh = !nnueKingsValid || isCastling(m) ||
        (moverChildPt == KING &&
         (KING_ZONE[us == WHITE ? from : Square(from ^ 56)] !=
          KING_ZONE[us == WHITE ? to   : Square(to ^ 56)]));
    if (isPromo(m)) {
        PieceType actualPromo = PieceType(promoType(m) + 1);
        piecesByType[actualPromo] ^= to;
        piecesByType[PAWN] ^= from;
        pc = makePiece(us, PAWN);
        pt = PAWN;
    } else {
        piecesByType[pt] ^= (squareBB(from) | squareBB(to));
    }
    piecesByColor[us] ^= (squareBB(from) | squareBB(to));
    occupied ^= (squareBB(from) | squareBB(to));
    pieceOn[to] = NO_PIECE;
    pieceOn[from] = pc;
    if (isCastling(m)) {
        Square rFrom, rTo;
        if (to == SQ_G1) { rFrom = SQ_H1; rTo = SQ_F1; }
        else if (to == SQ_C1) { rFrom = SQ_A1; rTo = SQ_D1; }
        else if (to == SQ_G8) { rFrom = SQ_H8; rTo = SQ_F8; }
        else { rFrom = SQ_A8; rTo = SQ_D8; }
        Piece rookPc = pieceOn[rTo];
        piecesByColor[us] ^= (squareBB(rFrom) | squareBB(rTo));
        piecesByType[ROOK] ^= (squareBB(rFrom) | squareBB(rTo));
        occupied ^= (squareBB(rFrom) | squareBB(rTo));
        pieceOn[rTo] = NO_PIECE;
        pieceOn[rFrom] = rookPc;
    }
    if (st.capturedPiece != NO_PIECE) {
        Square capSq = st.capturedSquare;
        Piece captured = st.capturedPiece;
        piecesByColor[colorOf(captured)] ^= capSq;
        piecesByType[typeOf(captured)] ^= capSq;
        occupied ^= capSq;
        pieceOn[capSq] = captured;
    }
    castling = st.oldCastling;
    epSquare = st.oldEpSquare;
    halfmoveClock = st.oldHalfmoveClock;
    if (us == BLACK) fullmove--;
    key = st.oldKey;
    checkers = st.oldCheckers;
    kingSquare[0] = st.oldKingSquare[0];
    kingSquare[1] = st.oldKingSquare[1];
    blockBB = st.oldBlockBB;
    if (nnueMustRefresh) {
        nnueRefresh();
    } else {
        const Square pw = kingSquare[WHITE];
        const Square pb = kingSquare[BLACK];
        if (st.capturedPiece != NO_PIECE)
            nnue.add(typeOf(st.capturedPiece), st.capturedSquare,
                            colorOf(st.capturedPiece), pw, pb);
        if (isPromo(m)) {
            nnue.remove(moverChildPt, to, us, pw, pb);
            nnue.add(PAWN, from, us, pw, pb);
        } else {
            nnue.update(moverChildPt, to, from, us, pw, pb);
        }
    }
    historySize--;
}
void Board::makeNullMove(StateInfo& st) {
    st.capturedPiece = NO_PIECE;
    st.oldCastling = castling;
    st.oldEpSquare = epSquare;
    st.oldHalfmoveClock = halfmoveClock;
    st.oldKey = key;
    st.oldCheckers = checkers;
    st.oldKingSquare[0] = kingSquare[0];
    st.oldKingSquare[1] = kingSquare[1];
    st.oldBlockBB = blockBB;
    historyArray[historySize & (MAX_HISTORY - 1)] = key;
    historySize++;
    if (epSquare != SQ_NONE) {
        key ^= zob.epFile[fileOf(epSquare)];
        epSquare = SQ_NONE;
    }
    key ^= zob.side[BLACK];
    sideToMove = Color(sideToMove ^ 1);
    halfmoveClock++;
}
void Board::unmakeNullMove(const StateInfo& st) {
    sideToMove = Color(sideToMove ^ 1);
    castling = st.oldCastling;
    epSquare = st.oldEpSquare;
    halfmoveClock = st.oldHalfmoveClock;
    key = st.oldKey;
    checkers = st.oldCheckers;
    kingSquare[0] = st.oldKingSquare[0];
    kingSquare[1] = st.oldKingSquare[1];
    blockBB = st.oldBlockBB;
    historySize--;
}
}
