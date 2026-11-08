// ============================================================================
// tactics_path.cpp
// ============================================================================
#include "tactics_path.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <set>
#include <tuple>

namespace ch::tactics {

namespace {
// A straight step and a diagonal one, in the time units of FUN_00470250's
// walk: a cell across or down takes the same time (the vertical speed is
// halved over half the height), a diagonal about 1.6 times that.
constexpr int kStraight = 100, kDiagonal = 160;

bool open(const TacticsMap& map, int moveType, int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= map.cellsW() || cy >= map.cellsH()) return false;
    return moveType == 4 || map.moveRate(cx, cy, moveType) != 0;   // type 4 flies over everything
}
} // namespace

std::vector<Cell> findPath(const TacticsMap& map, int moveType, Cell from, Cell to,
                           const std::function<bool(int, int)>& blocked) {
    const int w = map.cellsW(), h = map.cellsH();
    if (w <= 0 || h <= 0 || from == to) return {};
    const auto index = [w](int cx, int cy) { return std::size_t(cy) * std::size_t(w) + std::size_t(cx); };
    const auto passable = [&](int cx, int cy) {
        if (!open(map, moveType, cx, cy)) return false;
        if (Cell{cx, cy} == to || Cell{cx, cy} == from) return true;
        return !blocked || !blocked(cx, cy);
    };
    const auto heuristic = [&](int cx, int cy) {
        const int dx = std::abs(cx - to.first), dy = std::abs(cy - to.second);
        return kStraight * (std::max(dx, dy) - std::min(dx, dy)) + kDiagonal * std::min(dx, dy);
    };
    // The slowest terrain sets the step's cost (rates are percent, 100 normal).
    const auto stepCost = [&](int cx, int cy, bool diagonal) {
        int rate = moveType == 4 ? 100 : map.moveRate(cx, cy, moveType);
        if (rate <= 0) rate = 0x32;                // FUN_0043b8b0's floor
        return (diagonal ? kDiagonal : kStraight) * 100 / rate;
    };

    std::vector<int> cost(std::size_t(w) * std::size_t(h), -1);
    std::vector<int> parent(cost.size(), -1);
    using Entry = std::pair<int, int>;             // {estimate, index}
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> todo;
    cost[index(from.first, from.second)] = 0;
    todo.emplace(heuristic(from.first, from.second), int(index(from.first, from.second)));
    static constexpr int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static constexpr int kDy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    const int start = int(index(from.first, from.second));
    const int goal = to.first >= 0 && to.second >= 0 && to.first < w && to.second < h ? int(index(to.first, to.second)) : -1;
    int best = start, bestH = heuristic(from.first, from.second);   // the closest cell reached
    while (!todo.empty()) {
        const auto [est, at] = todo.top();
        todo.pop();
        if (at == goal) break;
        const int cx = at % w, cy = at / w, here = cost[std::size_t(at)];
        if (est - heuristic(cx, cy) > here) continue;   // stale entry
        if (const int hh = heuristic(cx, cy); hh < bestH || (hh == bestH && here < cost[std::size_t(best)])) {
            best = at;
            bestH = hh;
        }
        for (int d = 0; d < 8; ++d) {
            const int nx = cx + kDx[d], ny = cy + kDy[d];
            if (!passable(nx, ny)) continue;
            const bool diagonal = d >= 4;
            if (diagonal && (!passable(cx + kDx[d], cy) || !passable(cx, cy + kDy[d]))) continue;
            const int next = here + stepCost(nx, ny, diagonal);
            const std::size_t n = index(nx, ny);
            if (cost[n] >= 0 && cost[n] <= next) continue;
            cost[n] = next;
            parent[n] = at;
            todo.emplace(next + heuristic(nx, ny), int(n));
        }
    }
    const int end = goal >= 0 && cost[std::size_t(goal)] >= 0 ? goal : best;
    std::vector<Cell> path;
    for (int at = end; at != start; at = parent[std::size_t(at)])
        path.emplace_back(at % w, at / w);
    std::reverse(path.begin(), path.end());
    return path;
}

Cell nearestFree(const TacticsMap& map, int moveType, Cell to, Cell from, int range,
                 const std::function<bool(int, int)>& blocked) {
    for (int r = 0; r <= range; ++r) {
        Cell best{-1, -1};
        int bestDist = 0;
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const int cx = to.first + dx, cy = to.second + dy;
                if (!open(map, moveType, cx, cy) || (blocked && blocked(cx, cy))) continue;
                const int d = (cx - from.first) * (cx - from.first) + (cy - from.second) * (cy - from.second);
                if (best.first < 0 || d < bestDist) {
                    best = {cx, cy};
                    bestDist = d;
                }
            }
        }
        if (best.first >= 0) return best;
    }
    return {-1, -1};
}


// ---------------------------------------------------- the original router ---

namespace {
bool walkable(const TacticsMap& map, int moveType, int cx, int cy) {   // FUN_0043bfa0
    return map.moveRate(cx, cy, moveType == 4 ? 0 : moveType) != 0;
}
int dist2(int ax, int ay, int bx, int by) { return (ax - bx) * (ax - bx) + (ay - by) * (ay - by); }

// FUN_004388e0's stepping: along the longer axis, the shorter one when the
// error reaches it. `visit` returns false to stop.
template <class F>
bool bresenham(Cell a, Cell b, F visit) {
    int dx = b.first - a.first, dy = b.second - a.second;
    const int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    dx = std::abs(dx);
    dy = std::abs(dy);
    int x = a.first, y = a.second, err = 0;
    if (dx < dy) {
        for (int i = 0; i < dy; ++i) {
            y += sy;
            err += dx;
            if (dy <= err) {
                x += sx;
                err -= dy;
            }
            if (!visit(x, y)) return false;
        }
    } else {
        for (int i = 0; i < dx; ++i) {
            x += sx;
            err += dy;
            if (dx <= err) {
                y += sy;
                err -= dx;
            }
            if (!visit(x, y)) return false;
        }
    }
    return true;
}

// The searches' node pool (FUN_004396e0) with the open list FUN_00439b20
// keeps sorted by f: a node goes in front of those with an equal f, and the
// head is taken next (FUN_00439c20). `at_` is the per-position table
// (+0x88, FUN_00439820 / FUN_004398d0).
struct SearchNode {
    int x = 0, y = 0, g = 0, h = 0, f = 0;
    int parent = -1, child = -1, id = -1;
    bool open = false;
    std::int64_t order = 0;   // its place among equal f: the latest pushed first
};

class Search {
public:
    // FUN_00439380 makes the three lists, FUN_00439550 the w x h nodes and
    // the per-position table (+0x88); FUN_00439c60 empties the open and
    // closed lists between searches (here each search is a new Search).
    // FUN_004394a0 is its teardown (the lists and the table freed), the
    // vector members' own here.
    Search(int w, int h) : w_(w), h_(h), at_(std::size_t(w) * std::size_t(h), -1) {}
    std::vector<SearchNode> nodes;
    // The open list in FUN_00439b20's order (f, then the latest pushed of
    // equal f first), kept as an ordered set: the original's walk down a
    // linked list made each insertion cost the list's length, which on a
    // large search (a goal walled in) took seconds.
    std::set<std::tuple<int, std::int64_t, int>> openList;   // (f, -push number, node)
    [[nodiscard]] bool openEmpty() const { return openList.empty(); }

    void clearTable() { std::fill(at_.begin(), at_.end(), -1); }     // FUN_00439790
    [[nodiscard]] int lookup(int x, int y) const { return at_[std::size_t(y) * std::size_t(w_) + std::size_t(x)]; }
    void store(int x, int y, int n) { at_[std::size_t(y) * std::size_t(w_) + std::size_t(x)] = n; }
    [[nodiscard]] bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
    void push(int n) {   // FUN_00439a90 -> FUN_00439b20: in front of those with an equal f
        SearchNode& e = nodes[std::size_t(n)];
        e.open = true;
        e.order = -++pushes_;
        openList.emplace(e.f, e.order, n);
    }
    void unlink(int n) {
        SearchNode& e = nodes[std::size_t(n)];
        openList.erase({e.f, e.order, n});
    }
    int pop() {   // FUN_00439750 / FUN_00439ae0 (FUN_00427cd0 takes the head)
        const int n = std::get<2>(*openList.begin());
        openList.erase(openList.begin());
        nodes[std::size_t(n)].open = false;
        return n;
    }
    int make(int x, int y, int g, int h, int parent, int id) {
        SearchNode n;
        n.x = x;
        n.y = y;
        n.g = g;
        n.h = h;
        n.f = g + h;
        n.parent = parent;
        n.id = id;
        nodes.push_back(n);
        return int(nodes.size()) - 1;
    }
    // A cheaper way to a known node: re-sorted, and a closed one reopened.
    void relax(int n, int parent, int g) {
        SearchNode& e = nodes[std::size_t(n)];
        if (g >= e.g) return;
        if (e.open) unlink(n);
        e.parent = parent;
        e.g = g;
        e.f = e.g + e.h;
        push(n);
    }

private:
    int w_, h_;
    std::vector<int> at_;
    std::int64_t pushes_ = 0;
};

// DAT_005ff910: the eight neighbours {dx, dy, cost}; DAT_005ff908 squared
// the cost of a closed cell, 100 an occupied one.
struct Step { int dx, dy, cost; };
constexpr Step kSteps[8] = {{0, -1, 1}, {1, -1, 2}, {1, 0, 1}, {1, 1, 2}, {0, 1, 1}, {-1, 1, 2}, {-1, 0, 1}, {-1, -1, 2}};
constexpr int kClosedCost = 60 * 60, kOccupiedCost = 100;
constexpr int kSearchLimit = 0x3e9;     // FUN_004380b0's expansions per call
constexpr int kResumeLimit = 32;            // our cap on FUN_00437e40's retries (see localRoute)

// FUN_004380b0: 1 at the goal, 0 with the open list empty, -1 after 0x3e9
// expansions (the open list is kept for the next call).
int localSearch(Search& s, const TacticsMap& map, int moveType, Cell to, const Occupied* occupied, int& found) {
    for (int round = 0; round < kSearchLimit; ++round) {
        if (s.openEmpty()) return 0;          // FUN_004399c0: the open list's head
        const int cur = s.pop();
        const SearchNode c = s.nodes[std::size_t(cur)];
        if (c.x == to.first && c.y == to.second) {
            found = cur;
            return 1;
        }
        for (const Step& st : kSteps) {
            const int nx = c.x + st.dx, ny = c.y + st.dy;
            if (c.parent >= 0 && s.nodes[std::size_t(c.parent)].x == nx && s.nodes[std::size_t(c.parent)].y == ny) continue;
            if (!s.inside(nx, ny)) continue;
            int cost = st.cost;
            if (nx != to.first || ny != to.second) {
                if (!walkable(map, moveType, nx, ny)) cost = kClosedCost;
                if (occupied && (*occupied)(nx, ny)) cost = kOccupiedCost;
            }
            const int g = c.g + cost;
            const int known = s.lookup(nx, ny);
            if (known < 0) {
                const int n = s.make(nx, ny, g, dist2(nx, ny, to.first, to.second), cur, -1);
                s.push(n);
                s.store(nx, ny, n);
            } else {
                s.relax(known, cur, g);
            }
        }
        s.store(c.x, c.y, cur);
    }
    return -1;
}

// FUN_00438610: child links from the start, then from each point the
// farthest later point FUN_004388e0 sees, up to 100 of them.
void localPath(Search& s, int goal, const TacticsMap& map, int moveType, const Occupied* occupied,
               std::vector<Cell>& out) {
    out.clear();
    auto& n = s.nodes;
    if (n[std::size_t(goal)].parent < 0) return;
    n[std::size_t(goal)].child = -1;
    int at = goal;
    do {
        const int p = n[std::size_t(at)].parent;
        n[std::size_t(p)].child = at;
        at = p;
    } while (n[std::size_t(at)].parent >= 0);
    const int first = n[std::size_t(at)].child;
    if (n[std::size_t(first)].child < 0) {
        out.emplace_back(n[std::size_t(first)].x, n[std::size_t(first)].y);
        return;
    }
    int cur = at, best = first;
    do {
        const Cell from{n[std::size_t(cur)].x, n[std::size_t(cur)].y};
        best = n[std::size_t(cur)].child;
        int c = best;
        while (clearLine(map, moveType, from, {n[std::size_t(c)].x, n[std::size_t(c)].y}, occupied)) {
            best = c;
            if (n[std::size_t(c)].child < 0) break;
            c = n[std::size_t(c)].child;
        }
        cur = best;
        out.emplace_back(n[std::size_t(best)].x, n[std::size_t(best)].y);
    } while (n[std::size_t(best)].child >= 0 && out.size() < 100);
}
} // namespace

bool RouteGraph::load(const std::vector<std::uint8_t>& vpt, int w, int h) {
    clear();
    const auto u32 = [&](std::size_t o) -> std::uint32_t {
        return o + 4 <= vpt.size() ? std::uint32_t(vpt[o]) | std::uint32_t(vpt[o + 1]) << 8 | std::uint32_t(vpt[o + 2]) << 16 |
                                         std::uint32_t(vpt[o + 3]) << 24
                                   : 0u;
    };
    const auto s16 = [&](std::size_t o) -> int {
        return o + 2 <= vpt.size() ? static_cast<std::int16_t>(vpt[o] | vpt[o + 1] << 8) : 0;
    };
    if (w <= 0 || h <= 0 || u32(4) < 8) return false;
    for (int t = 0; t < 4; ++t) {
        const std::size_t b = u32(8 + std::size_t(t) * 4), g = u32(8 + std::size_t(t + 4) * 4);
        const int count = static_cast<std::int32_t>(u32(b + 0x14));
        if (b == 0 || g == 0 || count <= 0 || g + std::size_t(w) * std::size_t(h) * 2 > vpt.size()) return false;
        nodes_[t].resize(std::size_t(count));
        for (int i = 0; i < count; ++i) {
            const std::size_t r = b + u32(b + 0x18 + std::size_t(i) * 4);   // FUN_0044e5d0 rebases these
            Node& n = nodes_[t][std::size_t(i)];
            n.x = r < vpt.size() ? vpt[r] : 0;
            n.y = r + 1 < vpt.size() ? vpt[r + 1] : 0;
            const int e = s16(r + 2);
            for (int k = 0; k < e; ++k)
                n.edges.emplace_back(s16(r + 4 + std::size_t(k) * 12), static_cast<std::int32_t>(u32(r + 0xc + std::size_t(k) * 12)));
        }
        grid_[t].resize(std::size_t(w) * std::size_t(h));
        for (std::size_t i = 0; i < grid_[t].size(); ++i) grid_[t][i] = static_cast<std::int16_t>(s16(g + i * 2));
    }
    w_ = w;
    h_ = h;
    return true;
}

int RouteGraph::nodeAt(int type, int cx, int cy) const {
    if (type == 4) type = 0;
    if (!loaded() || type < 0 || type > 3 || cx < 0 || cy < 0 || cx >= w_ || cy >= h_) return -1;
    return grid_[type][std::size_t(cy) * std::size_t(w_) + std::size_t(cx)];
}

const RouteGraph::Node* RouteGraph::node(int type, int no) const {
    if (type == 4) type = 0;
    if (type < 0 || type > 3 || no < 0 || no >= int(nodes_[type].size())) return nullptr;
    return &nodes_[type][std::size_t(no)];
}

bool clearLine(const TacticsMap& map, int moveType, Cell a, Cell b, const Occupied* occupied) {
    return bresenham(a, b, [&](int x, int y) {
        return walkable(map, moveType, x, y) && !(occupied && (*occupied)(x, y));
    });
}

Cell lineReach(const TacticsMap& map, int moveType, Cell a, Cell b, const Occupied* occupied) {
    Cell last = a;
    bresenham(a, b, [&](int x, int y) {
        if (!walkable(map, moveType, x, y) || (occupied && (*occupied)(x, y))) return false;
        last = {x, y};
        return true;
    });
    return last;
}

std::vector<Cell> lineCells(Cell a, Cell b) {
    std::vector<Cell> out;
    bresenham(a, b, [&](int x, int y) {
        out.emplace_back(x, y);
        return true;
    });
    return out;
}

// FUN_00437e40: a closed goal is taken as it is; else the search runs from
// the start. The original repeats FUN_004380b0 (0x3e9 expansions each) until
// it reaches the goal, each time clearing the position table and adding the
// start again to what is left of the open list. When the cheapest cells
// around the start number more than one round's expansions (a goal walled
// in, reached only across closed cells at 60 * 60), each round so explores
// the same cells again and never gets further: the port's rounds go on with
// the search instead (the table and the closed cells kept), which reaches
// such a goal across the walls within a few rounds, and stop on an empty
// list or after kResumeLimit rounds. A goal found in the first round, the
// usual case, gives the original's route.
int localRoute(const TacticsMap& map, int moveType, Cell from, Cell to, const Occupied* occupied,
               std::vector<Cell>& waypoints) {
    waypoints.clear();
    if (!walkable(map, moveType, to.first, to.second)) {
        waypoints.push_back(to);
        return 1;
    }
    Search s(map.cellsW(), map.cellsH());
    if (!s.inside(from.first, from.second) || !s.inside(to.first, to.second)) return 0;
    const int start = s.make(from.first, from.second, 0, dist2(from.first, from.second, to.first, to.second), -1, -1);
    s.push(start);
    s.store(from.first, from.second, start);
    for (int round = 0; round < kResumeLimit; ++round) {
        int found = -1;
        const int r = localSearch(s, map, moveType, to, occupied, found);
        if (r == 1) {
            localPath(s, found, map, moveType, occupied, waypoints);
            return 1;
        }
        if (r == 0) return 0;
    }
    return 0;
}

// FUN_0044ea80 + FUN_0044ed40 (no limit on its expansions) + FUN_0044f1e0.
int coarseRoute(const RouteGraph& graph, const TacticsMap& map, int moveType, Cell from, Cell to,
                std::vector<Cell>& waypoints) {
    waypoints.clear();
    const int a = graph.nodeAt(moveType, from.first, from.second), b = graph.nodeAt(moveType, to.first, to.second);
    const RouteGraph::Node* na = graph.node(moveType, a);
    const RouteGraph::Node* nb = graph.node(moveType, b);
    if (!na || !nb) return 0;
    if (a == b) return 2;
    Search s(graph.width(), graph.height());
    const int start = s.make(na->x, na->y, 0, dist2(na->x, na->y, nb->x, nb->y), -1, a);
    s.push(start);
    if (s.inside(na->x, na->y)) s.store(na->x, na->y, start);
    int found = -1;
    while (!s.openEmpty()) {
        const int cur = s.pop();
        const SearchNode c = s.nodes[std::size_t(cur)];
        if (c.id == b) {
            found = cur;
            break;
        }
        const RouteGraph::Node* here = graph.node(moveType, c.id);
        if (here) {
            for (const auto& [to2, cost] : here->edges) {
                const RouteGraph::Node* nn = graph.node(moveType, to2);
                if (!nn || (nn->x == c.x && nn->y == c.y)) continue;
                if (c.parent >= 0 && s.nodes[std::size_t(c.parent)].x == nn->x && s.nodes[std::size_t(c.parent)].y == nn->y) continue;
                if (!s.inside(nn->x, nn->y)) continue;
                const int g = c.g + cost;
                const int known = s.lookup(nn->x, nn->y);
                if (known < 0) {
                    const int n = s.make(nn->x, nn->y, g, dist2(nn->x, nn->y, nb->x, nb->y), cur, to2);
                    s.push(n);
                    s.store(nn->x, nn->y, n);
                } else {
                    s.relax(known, cur, g);
                }
            }
        }
        if (s.inside(c.x, c.y)) s.store(c.x, c.y, cur);
    }
    if (found < 0) return 0;
    // FUN_0044f1e0: the goal cell, the nodes back to the start, the start
    // cell (at most 0x63 points); then from the start the nearest-to-goal
    // point in sight (FUN_0044f680, units not counted), again and again.
    std::vector<Cell> pts{to};
    bool cut = false;
    for (int n = found; n >= 0; n = s.nodes[std::size_t(n)].parent) {
        pts.emplace_back(s.nodes[std::size_t(n)].x, s.nodes[std::size_t(n)].y);
        if (pts.size() > 0x63) {
            cut = true;
            break;
        }
    }
    if (!cut) pts.push_back(from);
    const int last = int(pts.size()) - 1;
    int limit = last, j = 0;
    Cell cur = pts[std::size_t(last)];
    for (int guard = 0; guard < 0x4000; ++guard) {
        bool jumped = false;
        while (j != limit) {
            if (clearLine(map, moveType, cur, pts[std::size_t(j)], nullptr)) {
                waypoints.push_back(pts[std::size_t(j)]);
                cur = pts[std::size_t(j)];
                if (j == 0) return 1;
                limit = j;
                j = 0;
                jumped = true;
                break;
            }
            ++j;
        }
        if (jumped) continue;
        if (limit != last) waypoints.push_back(cur);
        // Stepped back to the goal itself: nothing nearer is left to try.
        if (--limit <= 0) {
            waypoints.push_back(pts[0]);
            return 1;
        }
        cur = pts[std::size_t(limit)];
        j = 0;
    }
    return 1;
}

} // namespace ch::tactics
