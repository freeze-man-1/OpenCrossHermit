// ============================================================================
// tactics_path.hpp — the walking route between two cells
//
// The original plans in two levels: a coarse route over the cell graph
// (FUN_00438ad0, FUN_0044ea80, FUN_00438d00, FUN_00437e40 into unit work
// +0x2f4) and a local one to the next coarse point (+0x3e0, FUN_0047d930),
// both weighted by the terrain's move rate for the unit's move type
// (FUN_0043bfa0). This is one A* search over the same costs: eight
// neighbours, diagonals only where both side cells are open.
// ============================================================================
#pragma once
#include "tactics_map.hpp"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace ch::tactics {

using Cell = std::pair<int, int>;

// Cells from `from` (excluded) to `to` (included). When `to` cannot be
// reached, the route ends on the reachable cell closest to it (empty if
// that is `from`). `blocked(cx, cy)` closes extra cells (other units' resting
// cells); the start and goal are never closed by it.
[[nodiscard]] std::vector<Cell> findPath(const TacticsMap& map, int moveType, Cell from, Cell to,
                                         const std::function<bool(int, int)>& blocked);

// The open cell nearest to `to` (by ring, then by distance from `from`)
// that `blocked` leaves free, within `range` rings; {-1, -1} if none.
[[nodiscard]] Cell nearestFree(const TacticsMap& map, int moveType, Cell to, Cell from, int range,
                               const std::function<bool(int, int)>& blocked);


// ---------------------------------------------------- the original router ---
// FUN_0047d210's two levels, as the original runs them. Move type 4 counts
// as 0 throughout, as FUN_0043bfa0 does.

// A cell another unit stands on (FUN_0043c0c0's count), for the searches
// that avoid units; null: none avoided.
using Occupied = std::function<bool(int, int)>;

// FUN_0044e440 / FUN_0044e4c0: mapNN.vpt, a block file. Blocks 0..3 hold
// each move type's node list ("MapEditFlIDCode", +0x14 count, +0x18 offsets):
// {u8 x, u8 y, s16 edges, then 12-byte edges {s16 node, float distance,
// s32 cost}}; blocks 4..7 the node of each cell (s16, width x height).
class RouteGraph {
public:
    struct Node {
        int x = 0, y = 0;
        std::vector<std::pair<int, int>> edges;   // {node, cost}
    };
    bool load(const std::vector<std::uint8_t>& vpt, int w, int h);
    void clear() {
        for (auto& n : nodes_) n.clear();
        for (auto& g : grid_) g.clear();
        w_ = h_ = 0;
    }
    [[nodiscard]] bool loaded() const noexcept { return w_ > 0; }
    [[nodiscard]] int nodeAt(int type, int cx, int cy) const;           // FUN_0044e6e0
    [[nodiscard]] const Node* node(int type, int no) const;             // FUN_0044e650
    [[nodiscard]] int width() const noexcept { return w_; }
    [[nodiscard]] int height() const noexcept { return h_; }

private:
    std::vector<Node> nodes_[4];
    std::vector<std::int16_t> grid_[4];
    int w_ = 0, h_ = 0;
};

// FUN_004388e0 / FUN_0044f680: every cell of the Bresenham line from `a`
// (excluded) to `b` is open, and with `occupied`, no one stands on it.
[[nodiscard]] bool clearLine(const TacticsMap& map, int moveType, Cell a, Cell b, const Occupied* occupied);
// FUN_00438d00: the last cell of that line before it is blocked ({a} if the
// first step is).
[[nodiscard]] Cell lineReach(const TacticsMap& map, int moveType, Cell a, Cell b, const Occupied* occupied);
// FUN_00470a50 cell by cell: the line's cells from `a` (excluded) to `b`.
[[nodiscard]] std::vector<Cell> lineCells(Cell a, Cell b);

// FUN_00437e40 -> FUN_004380b0, smoothed by FUN_00438610: the local route
// as waypoints. 1 found; 0 the goal cannot be reached.
int localRoute(const TacticsMap& map, int moveType, Cell from, Cell to, const Occupied* occupied,
               std::vector<Cell>& waypoints);
// FUN_0044ea80 -> FUN_0044ed40, smoothed by FUN_0044f1e0: the route over the
// map's node graph as waypoints. 1 found; 2 both cells share a node; 0 none.
int coarseRoute(const RouteGraph& graph, const TacticsMap& map, int moveType, Cell from, Cell to,
                std::vector<Cell>& waypoints);

} // namespace ch::tactics
