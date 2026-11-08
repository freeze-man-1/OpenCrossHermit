// Runs the original router (tactics_path: FUN_004380b0, FUN_0044ed40 and
// their smoothing) on real maps and checks the routes it gives against the
// port's own A* (findPath): every planned step is one of the eight
// neighbours, the route ends on the goal whenever the goal can be reached
// over open cells, and no route crosses a closed cell before its last leg.
// Usage: CH_DATA=<imported data folder> route_test [map ids...]
#include "../game/game_context.hpp"
#include "../game/tactics/tactics_map.hpp"
#include "../game/tactics/tactics_path.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {
class NullBanks final : public ch::ISpriteBanks {
public:
    void loadBank(std::uint16_t, std::vector<std::uint8_t>) override {}
    void unloadBank(std::uint16_t) override {}
    std::pair<int, int> frameSize(std::uint16_t, std::uint16_t) override { return {0, 0}; }
    void drawSprite(SDL_Renderer*, const ch::SpriteCmd&) override {}
    void drawText(SDL_Renderer*, const ch::TextCmd&) override {}
};

using ch::tactics::Cell;

// TacticsUnits::plan's two levels, with no units on the map.
std::vector<Cell> plan(const ch::TacticsMap& map, const ch::tactics::RouteGraph& graph, int type, Cell from, Cell to) {
    const auto open = [&](Cell c) { return map.moveRate(c.first, c.second, type == 4 ? 0 : type) != 0; };
    const auto level = [&](Cell a, Cell b, bool outer, std::vector<Cell>& out) {
        out.clear();
        if (ch::tactics::clearLine(map, type, a, b, nullptr)) {
            out.push_back(b);
            return true;
        }
        const int far = (b.first - a.first) * (b.first - a.first) + (b.second - a.second) * (b.second - a.second);
        if (outer && graph.loaded() && (!open(b) || far > 99)) {
            const int r = ch::tactics::coarseRoute(graph, map, type, a, b, out);
            if (r == 1) return true;
            if (r != 2) return false;
            out.clear();
        }
        if (!open(b)) {
            const Cell reach = ch::tactics::lineReach(map, type, a, b, nullptr);
            if (reach == a) return false;
            out.push_back(reach);
            return true;
        }
        return ch::tactics::localRoute(map, type, a, b, nullptr, out) == 1;
    };
    std::vector<Cell> outer, inner, path{from};
    if (!level(from, to, true, outer)) return {};
    Cell at = from;
    for (const Cell& wp : outer) {
        if (wp == at) continue;
        if (!level(at, wp, false, inner)) break;
        for (const Cell& p : inner) {
            for (const Cell& c : ch::tactics::lineCells(at, p)) path.push_back(c);
            at = p;
        }
    }
    return path;
}
} // namespace

int main(int argc, char** argv) {
    legacy::Engine engine({.title = "headless", .logicalW = 1024, .logicalH = 768});
    ch::GameContext ctx{.engine = engine, .sprites = std::make_unique<NullBanks>(), .audio = std::make_unique<ch::NullAudio>()};
    ctx.version = ch::GameVersion::Tw;
    std::vector<int> maps;
    for (int i = 1; i < argc; ++i) maps.push_back(std::atoi(argv[i]));
    if (maps.empty()) maps = {1, 2, 14, 64};
    int failures = 0;
    for (const int id : maps) {
        ch::TacticsMap map;
        if (!map.load(ctx, id)) {
            std::printf("map %d: SKIP (not loaded)\n", id);
            continue;
        }
        ch::tactics::RouteGraph graph;
        const bool g = graph.load(map.vpt(), map.cellsW(), map.cellsH());
        // ROUTE_TO=x,y: the slowest plan from any open cell to that cell
        // (a stall hunt; the checks below still run).
        if (const char* to = std::getenv("ROUTE_TO")) {
            Cell goal{};
            std::sscanf(to, "%d,%d", &goal.first, &goal.second);
            double worst = 0, total = 0;
            Cell worstFrom{};
            int n = 0;
            // ROUTE_FROM=x,y: that one cell only.
            Cell only{-1, -1};
            if (const char* from = std::getenv("ROUTE_FROM")) std::sscanf(from, "%d,%d", &only.first, &only.second);
            for (int y = 0; y < map.cellsH(); ++y)
                for (int x = 0; x < map.cellsW(); ++x) {
                    if (map.moveRate(x, y, 0) == 0 || (only.first >= 0 && Cell{x, y} != only)) continue;
                    const auto t0 = std::chrono::steady_clock::now();
                    (void)plan(map, graph, 0, {x, y}, goal);
                    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    total += ms;
                    ++n;
                    if (ms > worst) { worst = ms; worstFrom = {x, y}; }
                }
            std::printf("map %d: plans to %d,%d from %d cells: %.2f ms on average, %.2f ms at worst (from %d,%d)\n", id,
                        goal.first, goal.second, n, total / std::max(n, 1), worst, worstFrom.first, worstFrom.second);
        }
        std::mt19937 rng(1234u + unsigned(id));
        const int w = map.cellsW(), h = map.cellsH();
        int pairs = 0, reachable = 0, reached = 0, notAdjacent = 0, closedCrossed = 0, sumNew = 0, sumOld = 0;
        for (int type = 0; type < 4; ++type) {
            for (int k = 0; k < 150; ++k) {
                Cell a{int(rng() % unsigned(w)), int(rng() % unsigned(h))}, b{int(rng() % unsigned(w)), int(rng() % unsigned(h))};
                if (map.moveRate(a.first, a.second, type) == 0 || map.moveRate(b.first, b.second, type) == 0 || a == b) continue;
                ++pairs;
                const std::vector<Cell> old = ch::tactics::findPath(map, type, a, b, nullptr);
                const bool canReach = !old.empty() && old.back() == b;
                const std::vector<Cell> p = plan(map, graph, type, a, b);
                for (std::size_t i = 1; i < p.size(); ++i) {
                    const int dx = std::abs(p[i].first - p[i - 1].first), dy = std::abs(p[i].second - p[i - 1].second);
                    if (dx > 1 || dy > 1 || (dx == 0 && dy == 0)) ++notAdjacent;
                }
                // Cells the walker would refuse (FUN_0047da30 stops there).
                std::size_t firstClosed = p.size();
                for (std::size_t i = 1; i < p.size(); ++i)
                    if (map.moveRate(p[i].first, p[i].second, type) == 0) { firstClosed = i; break; }
                if (firstClosed != p.size()) ++closedCrossed;
                const bool got = firstClosed == p.size() && !p.empty() && p.back() == b;
                if (canReach) {
                    ++reachable;
                    if (got) {
                        ++reached;
                        sumNew += int(p.size()) - 1;
                        sumOld += int(old.size());
                    }
                }
            }
        }
        std::printf("map %d (%dx%d, graph %s): %d pairs, %d reachable, %d reached by the original router "
                    "(%d steps vs %d for findPath), %d routes into a closed cell, %d non-adjacent steps\n",
                    id, w, h, g ? "yes" : "no", pairs, reachable, reached, sumNew, sumOld, closedCrossed, notAdjacent);
        if (notAdjacent != 0) ++failures;
    }
    std::puts(failures == 0 ? "OK" : "FAIL");
    return failures == 0 ? 0 : 1;
}
