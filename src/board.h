#pragma once
#include "types.h"
#include "bitboard.h"
#include "nnue.h"
#include <array>
#include <string>
#include <cstdint>
namespace Chess {
struct StateInfo {
    Piece capturedPiece;
    Square capturedSquare;
    CastlingRights oldCastling;
    Square oldEpSquare;
    int oldHalfmoveClock;
    uint64_t oldKey;
    Bitboard oldCheckers;
    Square oldKingSquare[2];
    Bitboard oldBlockBB;
};
class Board {
public:
    Board() { clear(); }
    Bitboard piecesByColor[2];
    Bitboard piecesByType[6];
    Bitboard occupied;
    Piece pieceOn[64];
    CastlingRights castling;
    Square epSquare;
    int halfmoveClock;
    int fullmove;
    Color sideToMove;
    uint64_t key;
    NnueState nnue;
    Bitboard checkers;
    Square kingSquare[2];
    Bitboard blockBB;
    static constexpr int MAX_HISTORY = 4096;
    uint64_t historyArray[MAX_HISTORY];
    int historySize;
    void clear();
    bool setFen(const std::string& fen);
    std::string fen() const;
    void nnueRefresh();
    void makeMove(Move m, StateInfo& st);
    void unmakeMove(Move m, const StateInfo& st);
    void makeNullMove(StateInfo& st);
    void unmakeNullMove(const StateInfo& st);
    inline Bitboard pieces(PieceType pt) const { return piecesByType[pt]; }
    inline Bitboard pieces(Color c) const { return piecesByColor[c]; }
    inline Bitboard pieces(Color c, PieceType pt) const { return piecesByColor[c] & piecesByType[pt]; }
    inline Bitboard pieces() const { return occupied; }
    inline Square kingSq(Color c) const { return kingSquare[c]; }
    bool inCheck() const { return checkers != 0; }
    bool inCheck(Color c) const;
    bool isSquareAttacked(Square s, Color byColor) const;
    Bitboard attackersTo(Square s, Bitboard occupied) const;
    Bitboard attackersTo(Square s, Color byColor, Bitboard occupied) const;
    bool isRepetition(int ply = 0) const;
    bool isDraw(int ply = 0) const;
    bool hasNonPawnMaterial(Color c) const;
    bool hasNonPawnMaterial() const;
    bool givesCheckFast(Move m) const;
    void computeKeys();
    bool canCastle(CastlingRights cr) const { return (castling & cr) != NO_CASTLING; }
};
struct Zobrist {
    std::array<std::array<uint64_t, 64>, 16> psq;
    std::array<uint64_t, 2> side;
    std::array<uint64_t, 16> castling;
    std::array<uint64_t, 8> epFile;
};
extern Zobrist zob;
void initZobrist();
extern const int PieceValue[6];
}
