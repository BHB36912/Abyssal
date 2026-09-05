# Abyssal 

UCI chess engine (C++20) with an embedded efficiently-updatable neural network
evaluation.

## Build

    make -j

Requires a 64-bit Linux toolchain with AVX/AVX2 support (g++ recommended).
The network weights (`src/net.bin`) are embedded into the binary at compile
time via `incbin`; no runtime files are needed.

## UCI options

- `Hash` (1..2048 MB, default 128)
- `Threads` (1..8, default 2, Lazy SMP)
- `Clear Hash`

## Supported `go` parameters

`depth`, `nodes`, `movetime`, `wtime`/`btime`/`winc`/`binc`, `movestogo`,
`infinite`, `ponder`, plus `ponderhit`/`stop`.

## Debug commands

`perft <d>`, `bench [d]`, `eval`, `verify` (zobrist + NNUE accumulator
cross-check).

## License

Distributed under the **GNU General Public License v3.0**. See `LICENSE` for more details.

### Credits & Acknowledgements
* **NNUE Training:** Pipeline design and training methodology inspired by https://github.com/A1exL1ang/NNUE-Trainer
* **Training Data:** The embedded neural network was trained from scratch using public `.binpack` datasets provided by the Stockfish project.
