#pragma once

#include <Core/Types.h>
#include <Common/ThreadPool.h>
#include <Poco/Logger.h>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>

#include "PolygonDictionary.h"

#include <numeric>

namespace DB
{

namespace bg = boost::geometry;

using Coord = IPolygonDictionary::Coord;
using Point = IPolygonDictionary::Point;
using Polygon = IPolygonDictionary::Polygon;
using Ring = IPolygonDictionary::Ring;
using Box = bg::model::box<IPolygonDictionary::Point>;

/** Generate edge indexes during its construction in
 *  the following way: sort all polygon's vertexes by x coordinate, and then store all interesting
 *  polygon edges for each adjacent x coordinates. For each query finds interesting edges and
 *  iterates over them, finding required polygon. If there is more than one any such polygon may be returned.
 */
class BucketsPolygonIndex
{
public:
    /** A two-dimensional point in Euclidean coordinates. */
    using Point = IPolygonDictionary::Point;
    /** A polygon in boost is a an outer ring of points with zero or more cut out inner rings. */
    using Polygon = IPolygonDictionary::Polygon;
    /** A ring in boost used for describing the polygons. */
    using Ring = IPolygonDictionary::Ring;

    BucketsPolygonIndex() = default;

    /** Builds an index by splitting all edges with provided sorted x coordinates. */
    BucketsPolygonIndex(const std::vector<Polygon> & polygons, const std::vector<Coord> & splits);

    /** Builds an index by splitting all edges with all points x coordinates. */
    BucketsPolygonIndex(const std::vector<Polygon> & polygons);

    /** Finds polygon id the same way as IPolygonIndex. */
    bool find(const Point & point, size_t & id) const;

    /** Edge describes edge (adjacent points) of any polygon, and contains polygon's id.
     *  Invariant here is first point has x not greater than second point.
     */
    struct Edge
    {
        Point l;
        Point r;
        size_t polygon_id;
        size_t edge_id;

        Coord k;
        Coord b;

        Edge(const Point & l, const Point & r, size_t polygon_id, size_t edge_id);

        static bool compare1(const Edge & a, const Edge & b);
        static bool compare2(const Edge & a, const Edge & b);
    };

    struct EdgeLine
    {
        explicit EdgeLine(const Edge & e): k(e.k), b(e.b), polygon_id(e.polygon_id) {}
        Coord k;
        Coord b;
        size_t polygon_id;
    };

private:
    /** Returns unique x coordinates among all points. */
    std::vector<Coord> uniqueX(const std::vector<Polygon> & polygons);

    /** Builds indexes described above. */
    void indexBuild(const std::vector<Polygon> & polygons);

    /** Auxiliary function for adding ring to index */
    void indexAddRing(const Ring & ring, size_t polygon_id);

    Poco::Logger * log;

    /** Sorted distinct coordinates of all vertexes. */
    std::vector<Coord> sorted_x;
    std::vector<Edge> all_edges;

    /** Edges from all polygons, classified by sorted_x borders.
     *  edges_index[i] stores all interesting edges in range ( sorted_x[i]; sorted_x[i + 1] ]
     *  That means edges_index.size() + 1 == sorted_x.size()
     * 
     *  std::vector<std::vector<Edge>> edges_index;
     */

    /** TODO: fix this and previous comments.
     *  This edges_index_tree stores the same info as edges_index, but more efficiently.
     *  To do that, edges_index_tree is actually a segment tree of segments between x coordinates.
     *  edges_index_tree.size() == edges_index.size() * 2 == n * 2, and as in usual segment tree,
     *  edges_index_tree[i] combines segments edges_index_tree[i*2] and edges_index_tree[i*2+1].
     *  Every polygon's edge covers a segment of x coordinates, and can be added to this tree by
     *  placing it into O(log n) vertexes of this tree.
     */
    std::vector<std::vector<EdgeLine>> edges_index_tree;
};

template <class ReturnCell>
class ICell
{
public:
    virtual ~ICell() = default;
    [[nodiscard]] virtual const ReturnCell * find(Coord x, Coord y) const = 0;
};

class FinalCell : public ICell<FinalCell>
{
public:
    explicit FinalCell(const std::vector<size_t> & polygon_ids_, const std::vector<Polygon> & polygons_, const Box & box_);
    std::vector<size_t> polygon_ids;
    std::vector<uint8_t> is_covered_by;

private:
    [[nodiscard]] const FinalCell * find(Coord x, Coord y) const override;
};

class FinalCellWithSlabs : public ICell<FinalCellWithSlabs>
{
public:
    explicit FinalCellWithSlabs(const std::vector<size_t> & polygon_ids_, const std::vector<Polygon> & polygons_, const Box & box_);

    BucketsPolygonIndex index;
    std::vector<size_t> corresponding_ids;
    size_t first_covered = -1;

private:
    [[nodiscard]] const FinalCellWithSlabs * find(Coord x, Coord y) const override;
};

template <class ReturnCell>
class DividedCell : public ICell<ReturnCell>
{
public:
    explicit DividedCell(std::vector<std::unique_ptr<ICell<ReturnCell>>> children_): children(std::move(children_)) {}

    [[nodiscard]] const ReturnCell * find(Coord x, Coord y) const override
    {
        auto x_ratio = x * kSplit;
        auto y_ratio = y * kSplit;
        auto x_bin = static_cast<int>(x_ratio);
        auto y_bin = static_cast<int>(y_ratio);
        return children[y_bin + x_bin * kSplit]->find(x_ratio - x_bin, y_ratio - y_bin);
    }

    static constexpr size_t kSplit = 4;

private:
    std::vector<std::unique_ptr<ICell<ReturnCell>>> children;
};

/** A recursively built grid containing information about polygons intersecting each cell.
*  The starting cell is the bounding box of the given polygons which are stored by reference.
*  For every cell a vector of indices of intersecting polygons is stored, in the order originally provided upon
*  construction. A cell is recursively split into kSplit * kSplit equal cells up to the point where the cell
*  intersects a small enough number of polygons or the maximum allowed depth is exceeded.
*  Both of these parameters are set in the constructor.
*/
template <class ReturnCell>
class GridRoot : public ICell<ReturnCell>
{
public:
    GridRoot(size_t min_intersections_, size_t max_depth_, const std::vector<Polygon> & polygons_):
            kMinIntersections(min_intersections_), kMaxDepth(max_depth_), polygons(polygons_)
    {
        setBoundingBox();
        std::vector<size_t> order(polygons.size());
        std::iota(order.begin(), order.end(), 0);
        root = makeCell(min_x, min_y, max_x, max_y, order);
    }

    /** Retrieves the cell containing a given point.
     *  A null pointer is returned when the point falls outside the grid.
     */
    [[nodiscard]] const ReturnCell * find(Coord x, Coord y) const override
    {
        if (x < min_x || x >= max_x)
            return nullptr;
        if (y < min_y || y >= max_y)
            return nullptr;
        return root->find((x - min_x) / (max_x - min_x), (y - min_y) / (max_y - min_y));
    }

    /** When a cell is split every side is split into kSplit pieces producing kSplit * kSplit equal smaller cells. */
    static constexpr size_t kMultiProcessingDepth = 2;

private:
    std::unique_ptr<ICell<ReturnCell>> root = nullptr;
    Coord min_x = 0, min_y = 0;
    Coord max_x = 0, max_y = 0;
    const size_t kMinIntersections;
    const size_t kMaxDepth;

    const std::vector<Polygon> & polygons;

    std::unique_ptr<ICell<ReturnCell>> makeCell(Coord current_min_x, Coord current_min_y, Coord current_max_x, Coord current_max_y, std::vector<size_t> possible_ids, size_t depth = 0)
    {
        auto current_box = Box(Point(current_min_x, current_min_y), Point(current_max_x, current_max_y));
        possible_ids.erase(std::remove_if(possible_ids.begin(), possible_ids.end(), [&](const auto id)
        {
            return !bg::intersects(current_box, polygons[id]);
        }), possible_ids.end());
        if (possible_ids.size() <= kMinIntersections || depth++ == kMaxDepth)
            return std::make_unique<ReturnCell>(possible_ids, polygons, current_box);
        auto x_shift = (current_max_x - current_min_x) / DividedCell<ReturnCell>::kSplit;
        auto y_shift = (current_max_y - current_min_y) / DividedCell<ReturnCell>::kSplit;
        std::vector<std::unique_ptr<ICell<ReturnCell>>> children;
        children.resize(DividedCell<ReturnCell>::kSplit * DividedCell<ReturnCell>::kSplit);
        std::vector<ThreadFromGlobalPool> threads{};
        for (size_t i = 0; i < DividedCell<ReturnCell>::kSplit; current_min_x += x_shift, ++i)
        {
            auto handle_row = [this, &children, &y_shift, &x_shift, &possible_ids, &depth, i](Coord x, Coord y)
            {
                for (size_t j = 0; j < DividedCell<ReturnCell>::kSplit; y += y_shift, ++j)
                {
                    children[i * DividedCell<ReturnCell>::kSplit + j] = makeCell(x, y, x + x_shift, y + y_shift, possible_ids, depth);
                }
            };
            if (depth <= kMultiProcessingDepth)
                threads.emplace_back(handle_row, current_min_x, current_min_y);
            else
                handle_row(current_min_x, current_min_y);
        }
        for (auto & thread : threads)
            thread.join();
        return std::make_unique<DividedCell<ReturnCell>>(std::move(children));
    }

    void setBoundingBox()
    {
        bool first = true;
        std::for_each(polygons.begin(), polygons.end(), [&](const auto & polygon)
        {
            bg::for_each_point(polygon, [&](const Point & point)
            {
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
                if (first)
                    first = false;
            });
        });
    }
};

}
