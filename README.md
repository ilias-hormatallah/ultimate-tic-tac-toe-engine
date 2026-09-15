# Ultimate Tic-Tac-Toe Engine

A competitive Ultimate Tic-Tac-Toe engine in a single C++17 file: bitboard representation, alpha–beta search with principal variation search and late move reductions, iterative deepening under a time budget, and a 2M-entry transposition table.

**Winner of the ESILV Foundations-of-AI course tournament** (730 students, single-elimination 1v1 bracket, won the final).

## Build

```bash
g++ -O2 -std=c++17 uttt.cpp -o uttt
```

No dependencies beyond the standard library.

## Usage

Three modes:

**Interactive** — play against the engine in the terminal.
```bash
./uttt
```

**CLI** — one position in, one move out (used by the tournament harness).
```bash
./uttt cli "<state>" <time_ms>
```

**Pipe** — read positions from stdin, one per line, answer each with a move.
```bash
./uttt pipe <time_ms>
```

Moves are printed as `col row`, both 1-indexed on the 9×9 board.

### State format

23 space-separated integers:

| Field | Count | Meaning |
|---|---|---|
| `bx[0..8]` | 9 | 9-bit bitboards of X's cells, one per sub-board |
| `bo[0..8]` | 9 | 9-bit bitboards of O's cells, one per sub-board |
| `meta_x`, `meta_o` | 2 | 9-bit bitboards of sub-boards won by X / O |
| `meta_done` | 1 | 9-bit bitboard of finished sub-boards (won or full) |
| `constraint` | 1 | sub-board the player must play in, or `-1` if free |
| `current` | 1 | side to move, `1` = X, `2` = O |

Empty board, X to move: `0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 -1 1`

## How it works

- **Bitboards.** Each sub-board is a pair of 9-bit masks (X, O). Wins, threats and the leaf evaluation of a sub-board are precomputed in 512×512 lookup tables at startup, so the evaluation of a position is a handful of table reads.
- **Zobrist hashing** with incremental updates in `do_move`/`un_move`.
- **Transposition table** with 2²¹ entries, depth-preferred replacement, storing exact / lower / upper bounds and the best move for move ordering.
- **Iterative deepening** with a time budget; the search stops deepening once 65 % of the budget has been used, so the last completed iteration is always returned.
- **Alpha–beta with PVS.** The first move is searched with a full window, subsequent moves with a null window and re-searched only on fail-high.
- **Late move reductions.** Moves beyond the third, at depth ≥ 3, that are neither the hash move, a killer, nor a sub-board-winning move are searched at reduced depth and re-searched at full depth if they beat alpha.
- **Move ordering.** Hash move, immediate sub-board / meta-board wins, blocking moves, two killer moves per ply, history heuristic, and a penalty for sending the opponent to a sub-board where they hold threats.
- **Evaluation.** Sub-board tables weighted by strategic position (center and corners count more), meta-board line threats, and a bonus for holding two simultaneous meta-threats.

## Results

Each optimisation was measured in isolation against a fixed C++ reference engine (30 games, 1000 ms/move, empty starting board):

| Version | W–L–D | Avg. depth |
|---|---|---|
| Plain alpha–beta | 4–24–2 | ~4 |
| + transposition table (Zobrist) | 12–16–2 | ~6 |
| + move ordering (killers, history) | 15–12–3 | ~7 |
| + precomputed sub-board evaluation | 20–9–1 | ~8 |
| + apply/undo, refined leaf eval | 23–6–1 | ~9 |
| + LMR / PVS | 26–3–1 | ~11 |

Final version: 86.7 % win rate, 95 % Wilson interval ≈ [70 %, 95 %]. The report also lists the changes that were tried and rejected, and why.

**Search pathology.** Two builds differing only in the fraction of the time budget consumed before iterative deepening stops (40 % vs 65 %) were played against each other. At 1 s/move the 65 % build won clearly; at 10 s/move it *lost* — searching longer made play worse. The report proposes, as a hypothesis, that the move-ordering / LMR / PVS stack front-loads the good lines, so late deep iterations mostly explore new positions scored by an imperfect heuristic. This trade-off set the time threshold used in the tournament.

Full technical report available on request.

## License

MIT
