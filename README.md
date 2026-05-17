# Red Black Knights and Variants

Fast C++ simulators and PNG renderers for knight-placement games on an infinite spiral-numbered board.

## Board and Turn Model

- Infinite chessboard on integer coordinates.
- Spiral numbering starts at `n=1` at `(0,0)` and grows counterclockwise.
- Players take turns and place on the smallest legal `n` for their ruleset.

## Executables

- `red_black_knights.exe`
  - Default 2-player rules (`red` vs `black`), plus `three_knights` (`red`, `black`, `green`).
- `n_knights.exe`
  - Generalized `N`-player version with fixed color order:
    1. red
    2. black
    3. green
    4. blue
    5. yellow
    6. purple
    7. orange
    8+. auto-generated distinct colors
- `dominance_knights.exe`
  - Generalized dominance-graph rules: an edge `A>B` means attacks from player `A` block placements by player `B`.

## Build (Windows, MinGW)

```powershell
g++ -std=c++20 -O3 -Wall -Wextra -pedantic src/main.cpp -o red_black_knights.exe -lole32 -lwindowscodecs
g++ -std=c++20 -O3 -Wall -Wextra -pedantic src/n_knights.cpp -o n_knights.exe -lole32 -lwindowscodecs
g++ -std=c++20 -O3 -Wall -Wextra -pedantic src/dominance_knights.cpp -o dominance_knights.exe -lole32 -lwindowscodecs
```

## Quick Start

### 1) Default rules (red/black)

```powershell
.\red_black_knights.exe simulate --moves 1000000
.\red_black_knights.exe render --state outputs\default\states\state_1000000.json --xmin -500 --xmax 499 --ymin -500 --ymax 499 --cell 1 --overlay false
```

### 2) Three knights

```powershell
.\red_black_knights.exe simulate --rules three_knights --moves 1000000
.\red_black_knights.exe render --state outputs\three_knights\states\state_1000000.json --xmin -500 --xmax 499 --ymin -500 --ymax 499 --cell 1 --overlay false
```

### 3) N knights

```powershell
.\n_knights.exe simulate-render --players 8 --moves 1000000 --cells 1000000 --cell 1
```

Batch generation script (multiple `N` and sizes):

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate_n_knights_images.ps1
```

### 4) Dominance graph / RPS-style

Cycle mode (`1>2>3>...>N>1`):

```powershell
.\dominance_knights.exe simulate-render --players 3 --moves 1000000 --cells 1000000 --dominance cycle --cell 1
```

Custom edges:

```powershell
.\dominance_knights.exe simulate-render --players 5 --moves 1000000 --cells 1000000 --dominance custom --edges "1>2,2>3,3>1,4>5" --cell 1
```

## Output Layout

- Rendered images are tracked in git under `outputs/**/renders/`.
- JSON state files are generated locally under `outputs/**/states/` and ignored by git.

## License

MIT. See [LICENSE](LICENSE).