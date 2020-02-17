#include "PolygonDictionaryUtils.h"

#include <algorithm>
#include <numeric>

namespace DB
{

FinalCell::FinalCell(std::vector<size_t> polygon_ids_): polygon_ids(std::move(polygon_ids_)) {}

const ICell * FinalCell::find(Float64, Float64) const
{
    return this;
}

DividedCell::DividedCell(std::vector<std::unique_ptr<ICell>> children_): children(std::move(children_)) {}

const ICell * DividedCell::find(Float64 x, Float64 y) const
{
    auto x_ratio = x * GridRoot::kSplit;
    auto y_ratio = y * GridRoot::kSplit;
    auto x_bin = static_cast<int>(x_ratio);
    auto y_bin = static_cast<int>(y_ratio);
    return children[x_bin + y_bin * GridRoot::kSplit]->find(x_ratio - x_bin, y_ratio - y_bin);
}

GridRoot::GridRoot(const size_t min_intersections_, const size_t max_depth_, const std::vector<Polygon> & polygons_):
kMinIntersections(min_intersections_),
kMaxDepth(max_depth_),
polygons(polygons_)
{
    setBoundingBox();
    std::vector<size_t> ids(polygons.size());
    std::iota(ids.begin(), ids.end(), 0);
    root = makeCell(min_x, min_y, max_x, max_y, ids);
}

const ICell * GridRoot::find(Float64 x, Float64 y) const
{
    if (x < min_x || x >= max_x)
        return nullptr;
    if (y < min_y || y >= max_y)
        return nullptr;
    return root->find((x - min_x) / (max_x - min_x), (y - min_y) / (max_y - min_y));
}

std::unique_ptr<ICell> GridRoot::makeCell(Float64 current_min_x, Float64 current_min_y, Float64 current_max_x, Float64 current_max_y, std::vector<size_t> possible_ids, size_t depth)
{
    ++depth;
    auto current_box = Box(Point(current_min_x, current_min_y), Point(current_max_x, current_max_y));
    possible_ids.erase(std::remove_if(possible_ids.begin(), possible_ids.end(), [&](const auto & id) {
        return !bg::intersects(current_box, polygons[id]);
    }), possible_ids.end());
    if (possible_ids.size() <= kMinIntersections || depth == kMaxDepth)
        return std::make_unique<FinalCell>(possible_ids);
    auto x_shift = (current_max_x - current_min_x) / kSplit;
    auto y_shift = (current_max_y - current_min_y) / kSplit;
    std::vector<std::unique_ptr<ICell>> children;
    children.reserve(kSplit * kSplit);
    for (size_t i = 0; i < kSplit; current_min_x += x_shift, ++i)
    {
        for (size_t j = 0; j < kSplit; current_min_y += y_shift, ++j)
        {
            children.push_back(makeCell(current_min_x, current_min_y, current_min_x + x_shift, current_min_y + y_shift, possible_ids, depth + 1));
        }
    }
    return std::make_unique<DividedCell>(children);
}

void GridRoot::setBoundingBox()
{
    bool first = true;
    std::for_each(polygons.begin(), polygons.end(), [&](const auto & polygon) {
        bg::for_each_point(polygon, [&](const Point & point) {
            auto x = point.get<0>();
            auto y = point.get<1>();
            if (first || x < min_x)
                min_x = x;
            if (first || x > max_x)
                max_x = x;
            if (first || y < min_y)
                min_y = y;
            if (first || y > max_y)
                max_y = y;
            first = false;
        });
    });
}

}