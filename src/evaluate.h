#pragma once
#include "board.h"
namespace Chess {
int evaluate(const Board& b);
int see(const Board& b, Move m);
}
