/**
 * @file Draw.cpp
 * @brief Implementation of the governed fixed-budget draw types.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 *
 * Nothing here allocates, throws, or recurses. Every budget check is written as a subtraction
 * against the remaining capacity rather than an addition compared to the limit, so a count near
 * the top of its range cannot overflow into looking acceptable.
 */
module;

module mdux.draw;

import std;
import mdux.core.result;
import mdux.core.units;

namespace mdux::draw {

using mdux::core::err;
using mdux::core::Result;
using mdux::core::ResultVoid;

namespace {

/// Vertices and indices one rectangle costs. Two triangles sharing a diagonal.
constexpr std::uint32_t verticesPerRect = 4;
constexpr std::uint32_t indicesPerRect = 6;

}  // namespace

std::string_view describe(DrawError error) noexcept {
    switch (error) {
    case DrawError::EmptyBudget:
        return "budget has no room for a primitive";
    case DrawError::BudgetExceedsIndexWidth:
        return "budget exceeds what a 16-bit index can address";
    case DrawError::StorageTooSmall:
        return "supplied storage is smaller than the budget claims";
    case DrawError::VertexBudgetExceeded:
        return "vertex budget exceeded";
    case DrawError::IndexBudgetExceeded:
        return "index budget exceeded";
    case DrawError::CommandBudgetExceeded:
        return "command budget exceeded";
    case DrawError::DegenerateRect:
        return "rectangle has zero or negative width or height";
    case DrawError::WrongList:
        return "a rollback marker names a position in a different list, or in none";
    case DrawError::DegenerateQuad:
        return "a quad's corners enclose no area, or one of them is not finite";
    }
    return "unknown draw error";
}

Result<DrawList, DrawError> DrawList::create(std::span<UiVertex> vertices, std::span<Index> indices,
                                             std::span<DrawCommand> commands,
                                             const DrawBudget& budget) noexcept {
    if (budget.maxVertices < verticesPerRect || budget.maxIndices < indicesPerRect ||
        budget.maxCommands == 0) {
        return err(DrawError::EmptyBudget);
    }
    if (budget.maxVertices > maxIndexableVertices) {
        return err(DrawError::BudgetExceedsIndexWidth);
    }
    if (vertices.size() < budget.maxVertices || indices.size() < budget.maxIndices ||
        commands.size() < budget.maxCommands) {
        return err(DrawError::StorageTooSmall);
    }

    DrawList list;
    list.vertices_ = vertices;
    list.indices_ = indices;
    list.commands_ = commands;
    list.budget_ = budget;
    return list;
}

void DrawList::reset() noexcept {
    vertexCount_ = 0;
    indexCount_ = 0;
    commandCount_ = 0;
    clip_ = {};
}

DrawList::Marker DrawList::mark() const noexcept {
    Marker marker;
    marker.owner = this;
    marker.vertexCount = vertexCount_;
    marker.indexCount = indexCount_;
    marker.commandCount = commandCount_;
    // The last command's index count, because addRect() extends it in place when the clip has not
    // changed. Restoring commandCount_ alone would leave that command claiming indices the
    // rollback took away.
    marker.lastCommandIndexCount = commandCount_ == 0 ? 0U : commands_[commandCount_ - 1].indexCount;
    marker.clip = clip_;
    return marker;
}

ResultVoid<DrawError> DrawList::rollback(const Marker& marker) noexcept {
    // Compared, never dereferenced: a marker that outlived its list is a mismatch here rather than
    // a read through a dangling pointer.
    if (marker.owner != this) {
        return err(DrawError::WrongList);
    }
    // Backwards only. Every counter this restores indexes storage the list validated at create(),
    // so a marker naming a larger position could put commandCount_ past the span and make the
    // write below out of bounds. Refusing is cheap; the alternative is trusting a number that came
    // from outside this call.
    if (marker.vertexCount > vertexCount_ || marker.indexCount > indexCount_ ||
        marker.commandCount > commandCount_) {
        return err(DrawError::WrongList);
    }

    vertexCount_ = marker.vertexCount;
    indexCount_ = marker.indexCount;
    commandCount_ = marker.commandCount;
    if (commandCount_ > 0) {
        commands_[commandCount_ - 1].indexCount = marker.lastCommandIndexCount;
    }
    clip_ = marker.clip;
    // The storage is not cleared. Nothing reads past the counters - vertices(), indices() and
    // commands() all subspan by them - so zeroing would be work no observer can tell apart from
    // not doing it, on the failure path of a frame that is about to be discarded anyway.
    return {};
}

void DrawList::setClip(const mdux::core::Rect& clip) noexcept {
    // Recorded rather than applied: the next primitive notices the change and starts a command.
    // Doing it here would emit an empty command for a clip nothing was drawn under.
    clip_ = clip;
}

ResultVoid<DrawError> DrawList::addSolidRect(const mdux::core::Rect& rect,
                                             mdux::core::ColorRgba8 color) noexcept {
    // A solid primitive still carries uv, because the vertex layout is fixed and the fragment
    // shader ignores it in this mode. Zero rather than left undefined: an uninitialised float in
    // a buffer that is hashed or compared would make a frame non-reproducible.
    return addRect(rect, color, DrawMode::Solid, mdux::core::Rect{});
}

ResultVoid<DrawError> DrawList::addSolidQuad(const std::array<Point2F, 4>& corners,
                                             mdux::core::ColorRgba8 color) noexcept {
    // Finiteness first, because every test below it is a comparison and a NaN compares false
    // against all of them - so an unchecked NaN would fall through the area test as "not
    // degenerate" and reach the rasteriser, where it is undefined behaviour rather than an
    // invisible primitive. Same reasoning as `medui::quantise()`'s NaN clause, different remedy:
    // there the value has a defensible zero, here it does not.
    for (const Point2F& corner : corners) {
        if (!std::isfinite(corner.x) || !std::isfinite(corner.y)) {
            return err(DrawError::DegenerateQuad);
        }
    }

    // Twice the signed area, by the shoelace formula over the ring. Zero means the four corners are
    // collinear or coincident, which is `addRect()`'s DegenerateRect in the shape a quad has it.
    // The sign is not checked: a caller may wind either way, and the fixed 0-1-2 / 0-2-3 split
    // draws both - the pipeline disables face culling, so winding is not a visibility question.
    //
    // Measured relative to the first corner, and accumulated in double. Both matter, and neither is
    // caution for its own sake: over absolute coordinates the two products are of similar magnitude
    // and their difference is the area, so a small quad far from the origin loses that difference to
    // rounding. A 1px cap at (4096, 4096) cancels to exactly zero in float and reads as collinear -
    // which is a trace refusing to draw because of where it was placed. Translating first is exact
    // (a float minus a float is exact in double) and makes the products small; the double
    // accumulation then keeps a unit area from vanishing under coordinates a surface can reach.
    // Translation does not change an area, so this is the same test, computed where it survives.
    const double originX     = static_cast<double>(corners[0].x);
    const double originY     = static_cast<double>(corners[0].y);
    double       doubledArea = 0.0;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const Point2F& a  = corners[i];
        const Point2F& b  = corners[(i + 1) % corners.size()];
        const double   ax = static_cast<double>(a.x) - originX;
        const double   ay = static_cast<double>(a.y) - originY;
        const double   bx = static_cast<double>(b.x) - originX;
        const double   by = static_cast<double>(b.y) - originY;
        doubledArea += (ax * by) - (bx * ay);
    }
    if (!(doubledArea > 0.0) && !(doubledArea < 0.0)) {
        return err(DrawError::DegenerateQuad);
    }

    const std::uint32_t packed = packColor(color);
    const auto modeValue = static_cast<std::uint32_t>(DrawMode::Solid);
    std::array<UiVertex, 4> vertices{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        // uv is zero rather than left alone, for addSolidRect()'s reason: the vertex layout is
        // fixed and an uninitialised float in a buffer that is compared makes a frame irreproducible.
        vertices[i] = UiVertex{.x = corners[i].x,
                               .y = corners[i].y,
                               .u = 0.0F,
                               .v = 0.0F,
                               .color = packed,
                               .mode = modeValue};
    }
    return appendQuad(vertices);
}

ResultVoid<DrawError> DrawList::addRect(const mdux::core::Rect& rect, mdux::core::ColorRgba8 color,
                                        DrawMode mode, const mdux::core::Rect& uv) noexcept {
    // Widening only - the integer overload cannot express a fractional coordinate, which is
    // exactly why glyphs go through mdux.text.draw instead.
    return addRect(rect, color, mode,
                   UvRect{.u0 = static_cast<float>(uv.x),
                          .v0 = static_cast<float>(uv.y),
                          .u1 = static_cast<float>(uv.right()),
                          .v1 = static_cast<float>(uv.bottom())});
}

ResultVoid<DrawError> DrawList::addRect(const mdux::core::Rect& rect, mdux::core::ColorRgba8 color,
                                        DrawMode mode, const UvRect& uv) noexcept {
    if (rect.width <= 0 || rect.height <= 0) {
        return err(DrawError::DegenerateRect);
    }

    const auto left = static_cast<float>(rect.x);
    const auto top = static_cast<float>(rect.y);
    const auto right = static_cast<float>(rect.right());
    const auto bottom = static_cast<float>(rect.bottom());
    const auto u0 = uv.u0;
    const auto v0 = uv.v0;
    const auto u1 = uv.u1;
    const auto v1 = uv.v1;
    const std::uint32_t packed = packColor(color);
    const auto modeValue = static_cast<std::uint32_t>(mode);

    // Corner order is fixed: top-left, top-right, bottom-right, bottom-left. Two frames built
    // from the same primitives must produce byte-identical buffers, which a varying order would
    // break for no benefit.
    return appendQuad(std::array<UiVertex, 4>{
        UiVertex{.x = left, .y = top, .u = u0, .v = v0, .color = packed, .mode = modeValue},
        UiVertex{.x = right, .y = top, .u = u1, .v = v0, .color = packed, .mode = modeValue},
        UiVertex{.x = right, .y = bottom, .u = u1, .v = v1, .color = packed, .mode = modeValue},
        UiVertex{.x = left, .y = bottom, .u = u0, .v = v1, .color = packed, .mode = modeValue}});
}

ResultVoid<DrawError> DrawList::appendQuad(const std::array<UiVertex, 4>& corners) noexcept {
    // Subtraction against remaining capacity, never addition against the limit: with counts near
    // the top of their range an addition could wrap and read as acceptable.
    if (budget_.maxVertices - vertexCount_ < verticesPerRect) {
        return err(DrawError::VertexBudgetExceeded);
    }
    if (budget_.maxIndices - indexCount_ < indicesPerRect) {
        return err(DrawError::IndexBudgetExceeded);
    }

    // A new command is needed when nothing has been recorded yet, or when the clip changed since
    // the current command started.
    const bool needsCommand =
        commandCount_ == 0 || commands_[commandCount_ - 1].clip != clip_;
    if (needsCommand && commandCount_ == budget_.maxCommands) {
        return err(DrawError::CommandBudgetExceeded);
    }

    // Past this point nothing can fail, so the list never holds a half-recorded primitive.
    const auto baseVertex = static_cast<Index>(vertexCount_);
    for (std::size_t i = 0; i < corners.size(); ++i) {
        vertices_[vertexCount_ + i] = corners[i];
    }

    indices_[indexCount_ + 0] = static_cast<Index>(baseVertex + 0);
    indices_[indexCount_ + 1] = static_cast<Index>(baseVertex + 1);
    indices_[indexCount_ + 2] = static_cast<Index>(baseVertex + 2);
    indices_[indexCount_ + 3] = static_cast<Index>(baseVertex + 0);
    indices_[indexCount_ + 4] = static_cast<Index>(baseVertex + 2);
    indices_[indexCount_ + 5] = static_cast<Index>(baseVertex + 3);

    if (needsCommand) {
        commands_[commandCount_] =
            DrawCommand{.firstIndex = indexCount_, .indexCount = indicesPerRect, .clip = clip_};
        ++commandCount_;
    } else {
        commands_[commandCount_ - 1].indexCount += indicesPerRect;
    }

    vertexCount_ += verticesPerRect;
    indexCount_ += indicesPerRect;
    return {};
}

}  // namespace mdux::draw
