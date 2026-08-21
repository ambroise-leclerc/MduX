/**
 * @file Layout.cpp
 * @brief Integer-only Vertical/Row layout resolution.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 */
module;

module mdux.tools.medui.layout;

import std;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.diagnostics;

namespace mdux::tools::medui {

namespace {

/// Finds a named field on one component.
[[nodiscard]] const ast::Field* fieldFor(const ast::Node& node, std::string_view name) noexcept {
    const auto found = std::ranges::find(node.fields, name, &ast::Field::name);
    return found == node.fields.end() ? nullptr : &*found;
}

/// Finds a named field in an arbitrary field list.
[[nodiscard]] const ast::Field* fieldFor(std::span<const ast::Field> fields, std::string_view name) noexcept {
    const auto found = std::ranges::find(fields, name, &ast::Field::name);
    return found == fields.end() ? nullptr : &*found;
}

/// Returns a field value, failing loudly if semantic validation was bypassed.
[[nodiscard]] const ast::Value& valueOf(const ast::Field& field) {
    if (field.value == nullptr) {
        throw std::logic_error(std::format("layout received null value for field '{}'", field.name));
    }
    return *field.value;
}

/// Returns a validated component identifier.
[[nodiscard]] std::string idOf(const ast::Node& node) {
    const ast::Field* field = fieldFor(node, "id");
    if (field == nullptr || field->value == nullptr || field->value->kind != ast::ValueKind::Identifier || field->value->text.empty()) {
        throw std::logic_error(std::format("layout received component '{}' without a validated id", node.component));
    }
    return field->value->text;
}

/// Returns the authored `id` field position, falling back for malformed directly-built ASTs.
[[nodiscard]] ast::Position idFieldPosition(const ast::Node& node) noexcept {
    if (const ast::Field* field = fieldFor(node, "id")) {
        return field->namePosition;
    }
    return node.position;
}

/// Returns a validated size field.
[[nodiscard]] const ast::Size& sizeOf(const ast::Node& node, std::string_view name) {
    const ast::Field* field = fieldFor(node, name);
    if (field == nullptr || field->value == nullptr || field->value->kind != ast::ValueKind::Size) {
        throw std::logic_error(std::format("layout received component '{}' without validated field '{}'", node.component, name));
    }
    return field->value->size;
}

/// Returns an optional validated position field.
[[nodiscard]] const ast::Point* positionOf(const ast::Node& node) {
    const ast::Field* field = fieldFor(node, "position");
    if (field == nullptr) {
        return nullptr;
    }
    if (field->value == nullptr || field->value->kind != ast::ValueKind::Point) {
        throw std::logic_error(std::format("layout received component '{}' with an unvalidated position", node.component));
    }
    return &field->value->point;
}

/// Returns the source position used for a positioned-node diagnostic.
[[nodiscard]] ast::Position positionFieldPosition(const ast::Node& node) noexcept {
    if (const ast::Field* field = fieldFor(node, "position")) {
        return field->namePosition;
    }
    return node.position;
}

/// Tests complete rectangle containment without overflow-prone edge addition.
[[nodiscard]] bool contains(LayoutRect outer, LayoutRect inner) noexcept {
    if (inner.x < outer.x || inner.y < outer.y || inner.width < 0 || inner.height < 0) {
        return false;
    }
    const std::int64_t relativeX = inner.x - outer.x;
    const std::int64_t relativeY = inner.y - outer.y;
    return relativeX <= outer.width && relativeY <= outer.height && inner.width <= outer.width - relativeX && inner.height <= outer.height - relativeY;
}

/// Tests strict AABB overlap; touching edges are legal adjacency.
[[nodiscard]] bool overlaps(LayoutRect first, LayoutRect second) noexcept {
    // All accepted rectangles are non-negative and surface-contained, so these additions cannot
    // overflow. Shared edges are adjacency, not overlap.
    return first.x < second.x + second.width && second.x < first.x + first.width && first.y < second.y + second.height && second.y < first.y + first.height;
}

/// One fail-closed, integer-only layout pass.
class Solver {
public:
    /// Captures the diagnostic path and build-selected surface.
    Solver(std::string file, LayoutInputs inputs)
        : file_{std::move(file)},
          inputs_{inputs},
          result_{.surfaceWidth = inputs.surfaceWidth, .surfaceHeight = inputs.surfaceHeight, .nodes = {}, .diagnostics = {}} {}

    /// Resolves one semantically valid screen, returning no nodes on any diagnostic.
    [[nodiscard]] LayoutResult run(const ast::Screen& screen) {
        assertSingleRowLevel(screen);
        if (!preflightPositionedFill(screen.nodes)) {
            return std::move(result_);
        }
        if (!preflightPositiveDimensions(screen.nodes)) {
            return std::move(result_);
        }
        if (!readSurface(screen) || !readLayout(screen)) {
            return std::move(result_);
        }

        const LayoutRect content{.x = padding_, .y = padding_, .width = inputs_.surfaceWidth - 2 * padding_, .height = inputs_.surfaceHeight - 2 * padding_};

        std::vector<const ast::Node*> flow;
        for (const ast::Node& node : screen.nodes) {
            if (node.component == "Row" || positionOf(node) == nullptr) {
                flow.push_back(&node);
            }
        }

        std::vector<ast::Size> heights;
        heights.reserve(flow.size());
        for (const ast::Node* node : flow) {
            heights.push_back(sizeOf(*node, "height"));
        }
        const auto resolvedHeights = resolveAxis(heights, content.height, spacing_, screen.layoutPosition, "vertical layout");
        if (!resolvedHeights) {
            return std::move(result_);
        }

        std::int64_t cursorY   = content.y;
        std::size_t  flowIndex = 0;
        for (const ast::Node& node : screen.nodes) {
            if (node.component == "Row") {
                const LayoutRect rowBounds{.x = content.x, .y = cursorY, .width = content.width, .height = (*resolvedHeights)[flowIndex]};
                if (!resolveRow(node, rowBounds)) {
                    return std::move(result_);
                }
                cursorY += rowBounds.height + spacing_;
                ++flowIndex;
                continue;
            }

            if (const ast::Point* position = positionOf(node)) {
                const LayoutRect bounds{.x = position->x, .y = position->y, .width = fixedSize(node, "width"), .height = fixedSize(node, "height")};
                if (!contains(content, bounds)) {
                    report(Code::SurfaceExceeded, positionFieldPosition(node), std::format("component '{}' exceeds the padded surface", idOf(node)));
                    return std::move(result_);
                }
                appendLeaf(node, bounds, true);
                continue;
            }

            const ast::Size&   width         = sizeOf(node, "width");
            const std::int64_t resolvedWidth = width.fill ? content.width : width.pixels;
            if (resolvedWidth <= 0 || resolvedWidth > content.width) {
                report(Code::LayoutOverflow, width.position, std::format("component '{}' is wider than the vertical layout", idOf(node)));
                return std::move(result_);
            }
            const LayoutRect bounds{.x = content.x, .y = cursorY, .width = resolvedWidth, .height = (*resolvedHeights)[flowIndex]};
            appendLeaf(node, bounds, false);
            cursorY += bounds.height + spacing_;
            ++flowIndex;
        }

        if (!validateUniqueIds() || !validatePositionedOverlap()) {
            return std::move(result_);
        }
        return std::move(result_);
    }

private:
    /// Enforces the parser's one-level Row invariant for directly-built ASTs too.
    void assertSingleRowLevel(const ast::Screen& screen) const {
        for (const ast::Node& node : screen.nodes) {
            if (node.component != "Row" && !node.children.empty()) {
                throw std::logic_error(std::format("layout received children on non-Row component '{}'", node.component));
            }
            for (const ast::Node& child : node.children) {
                if (child.component == "Row" || !child.children.empty()) {
                    throw std::logic_error("layout received a nested Row; the parser must reject it before layout");
                }
            }
        }
    }

    /// Rejects the contradictory out-of-flow `position` plus `Fill` combination.
    bool preflightPositionedFill(std::span<const ast::Node> nodes) {
        for (const ast::Node& node : nodes) {
            if (positionOf(node) != nullptr && (sizeOf(node, "width").fill || sizeOf(node, "height").fill)) {
                // The common MedUI conformance case pins this declaration to MEDUI-E051. Keep the
                // shared diagnostic even though the failure is detected before axis resolution.
                report(Code::LayoutOverflow,
                       positionFieldPosition(node),
                       std::format("component '{}': position requires fixed width and height; "
                                   "Fill is flow-only",
                                   idOf(node)));
                return false;
            }
            if (!preflightPositionedFill(node.children)) {
                return false;
            }
        }
        return true;
    }

    /// Rejects zero-sized fixed component dimensions before any rectangle is emitted.
    bool preflightPositiveDimensions(std::span<const ast::Node> nodes) {
        for (const ast::Node& node : nodes) {
            const auto check = [&](std::string_view name) {
                const ast::Size& size = sizeOf(node, name);
                if (!size.fill && size.pixels <= 0) {
                    report(Code::LayoutOverflow, size.position, std::format("component '{}' {} must be greater than zero", idOf(node), name));
                    return false;
                }
                return true;
            };

            if (node.component == "Row") {
                if (!check("height") || !preflightPositiveDimensions(node.children)) {
                    return false;
                }
            } else if (!check("width") || !check("height")) {
                return false;
            }
        }
        return true;
    }

    /// Validates build dimensions and an optional authored surface pin.
    bool readSurface(const ast::Screen& screen) {
        if (inputs_.surfaceWidth <= 0 || inputs_.surfaceHeight <= 0 || inputs_.surfaceWidth > std::numeric_limits<std::int32_t>::max()
            || inputs_.surfaceHeight > std::numeric_limits<std::int32_t>::max()) {
            report(Code::SurfaceExceeded, screen.position, "build surface dimensions must be positive 32-bit pixel values");
            return false;
        }
        if (screen.surface && (screen.surface->x != inputs_.surfaceWidth || screen.surface->y != inputs_.surfaceHeight)) {
            report(Code::SurfaceExceeded,
                   screen.surface->position,
                   std::format("declared surface {}x{} does not match build surface {}x{}",
                               screen.surface->x,
                               screen.surface->y,
                               inputs_.surfaceWidth,
                               inputs_.surfaceHeight));
            return false;
        }
        return true;
    }

    /// Reads Vertical layout configuration and rejects an empty padded content box.
    bool readLayout(const ast::Screen& screen) {
        if (screen.layoutKind != "Vertical") {
            report(Code::ForbiddenConstruct,
                   screen.layoutPosition,
                   "the bounded layout solver accepts Vertical screens; Row supplies the only "
                   "horizontal grouping");
            return false;
        }
        const auto readPixel = [&](std::string_view name, std::int64_t defaultValue, std::int64_t& destination) {
            const ast::Field* field = fieldFor(screen.layout, name);
            if (field == nullptr) {
                destination = defaultValue;
                return true;
            }
            const ast::Value& value = valueOf(*field);
            if (value.kind != ast::ValueKind::Size || value.size.fill || value.size.pixels < 0) {
                report(Code::LayoutOverflow, value.position, std::format("layout {} must be a non-negative pixel size", name));
                return false;
            }
            destination = value.size.pixels;
            return true;
        };
        if (!readPixel("spacing", 0, spacing_) || !readPixel("padding", 0, padding_)) {
            return false;
        }
        if (padding_ >= inputs_.surfaceWidth - padding_ || padding_ >= inputs_.surfaceHeight - padding_) {
            report(Code::SurfaceExceeded, screen.layoutPosition, "layout padding leaves no surface content box");
            return false;
        }
        return true;
    }

    /// Resolves fixed and Fill sizes using TrustSC's equal-share rule; any remainder stays unused.
    [[nodiscard]] std::optional<std::vector<std::int64_t>>
    resolveAxis(std::span<const ast::Size> dimensions, std::int64_t available, std::int64_t spacing, ast::Position position, std::string_view context) {
        const std::int64_t gaps = dimensions.empty() ? 0 : static_cast<std::int64_t>(dimensions.size() - 1);
        if (spacing > 0 && gaps > (available / spacing)) {
            report(Code::LayoutOverflow, position, std::format("{} spacing exceeds its available space", context));
            return std::nullopt;
        }
        const std::int64_t spacingTotal = spacing * gaps;
        std::int64_t       fixedTotal   = 0;
        std::int64_t       fillCount    = 0;
        for (const ast::Size& dimension : dimensions) {
            if (dimension.fill) {
                ++fillCount;
                continue;
            }
            if (dimension.pixels <= 0 || dimension.pixels > available - spacingTotal - fixedTotal) {
                report(Code::LayoutOverflow, dimension.position, std::format("{} exceeds its available space", context));
                return std::nullopt;
            }
            fixedTotal += dimension.pixels;
        }
        const std::int64_t remaining = available - spacingTotal - fixedTotal;
        const std::int64_t fillSize  = fillCount == 0 ? 0 : remaining / fillCount;
        if (fillCount > 0 && fillSize <= 0) {
            report(Code::LayoutOverflow, position, std::format("Fill items in {} have no remaining space", context));
            return std::nullopt;
        }

        std::vector<std::int64_t> result;
        result.reserve(dimensions.size());
        for (const ast::Size& dimension : dimensions) {
            result.push_back(dimension.fill ? fillSize : dimension.pixels);
        }
        return result;
    }

    /// Resolves one Row to an optional Panel followed by its flat leaf children.
    bool resolveRow(const ast::Node& row, LayoutRect bounds) {
        const std::string rowId      = idOf(row);
        std::int64_t      rowSpacing = 0;
        if (const ast::Field* field = fieldFor(row, "spacing")) {
            const ast::Value& value = valueOf(*field);
            if (value.kind != ast::ValueKind::Size || value.size.fill || value.size.pixels < 0) {
                report(Code::LayoutOverflow, value.position, std::format("Row '{}' spacing must be a non-negative pixel size", rowId));
                return false;
            }
            rowSpacing = value.size.pixels;
        }

        if (fieldFor(row, "background") != nullptr) {
            result_.nodes.push_back(
                ResolvedNode{.id = rowId + "-background", .component = "Panel", .bounds = bounds, .source = row, .positioned = false, .synthetic = true});
        }

        std::vector<ast::Size> widths;
        for (const ast::Node& child : row.children) {
            if (positionOf(child) == nullptr) {
                widths.push_back(sizeOf(child, "width"));
            }
        }
        const auto resolvedWidths = resolveAxis(widths, bounds.width, rowSpacing, row.position, std::format("Row '{}'", rowId));
        if (!resolvedWidths) {
            return false;
        }

        std::int64_t cursorX   = bounds.x;
        std::size_t  flowIndex = 0;
        for (const ast::Node& child : row.children) {
            if (const ast::Point* position = positionOf(child)) {
                const LayoutRect childBounds{.x = position->x, .y = position->y, .width = fixedSize(child, "width"), .height = fixedSize(child, "height")};
                if (!contains(bounds, childBounds)) {
                    report(Code::LayoutOverflow, positionFieldPosition(child), std::format("component '{}' escapes Row '{}'", idOf(child), rowId));
                    return false;
                }
                appendLeaf(child, childBounds, true);
                continue;
            }

            const ast::Size&   height         = sizeOf(child, "height");
            const std::int64_t resolvedHeight = height.fill ? bounds.height : height.pixels;
            if (resolvedHeight <= 0 || resolvedHeight > bounds.height) {
                report(Code::LayoutOverflow, height.position, std::format("component '{}' is taller than Row '{}'", idOf(child), rowId));
                return false;
            }
            const LayoutRect childBounds{.x = cursorX, .y = bounds.y, .width = (*resolvedWidths)[flowIndex], .height = resolvedHeight};
            appendLeaf(child, childBounds, false);
            cursorX += childBounds.width + rowSpacing;
            ++flowIndex;
        }
        return true;
    }

    /// Returns a positioned node's fixed size after preflight validation.
    [[nodiscard]] std::int64_t fixedSize(const ast::Node& node, std::string_view name) const {
        const ast::Size& size = sizeOf(node, name);
        if (size.fill) {
            throw std::logic_error("positioned Fill reached layout after the preflight check");
        }
        return size.pixels;
    }

    /// Appends an owned authored leaf to the flat result.
    void appendLeaf(const ast::Node& node, LayoutRect bounds, bool positioned) {
        result_.nodes.push_back(
            ResolvedNode{.id = idOf(node), .component = node.component, .bounds = bounds, .source = node, .positioned = positioned, .synthetic = false});
    }

    /// Rechecks identifiers after synthetic background nodes have been added.
    bool validateUniqueIds() {
        std::map<std::string_view, ast::Position> seen;
        for (const ResolvedNode& node : result_.nodes) {
            const ast::Position idPosition  = idFieldPosition(node.source);
            const auto [previous, inserted] = seen.emplace(node.id, idPosition);
            if (!inserted) {
                report(Code::DuplicateNodeId,
                       idPosition,
                       std::format("resolved node id '{}' is already used at line {}, column {}", node.id, previous->second.line, previous->second.column));
                return false;
            }
        }
        return true;
    }

    /// Rejects overlap involving a positioned node; synthetic backgrounds are underlays.
    bool validatePositionedOverlap() {
        for (std::size_t first = 0; first < result_.nodes.size(); ++first) {
            for (std::size_t second = first + 1; second < result_.nodes.size(); ++second) {
                const ResolvedNode& a = result_.nodes[first];
                const ResolvedNode& b = result_.nodes[second];
                if (a.synthetic || b.synthetic || (!a.positioned && !b.positioned) || !overlaps(a.bounds, b.bounds)) {
                    continue;
                }
                const ResolvedNode& positioned = a.positioned ? a : b;
                const ResolvedNode& other      = a.positioned ? b : a;
                report(Code::LayoutOverflow,
                       positionFieldPosition(positioned.source),
                       std::format("positioned component '{}' overlaps component '{}'", positioned.id, other.id));
                return false;
            }
        }
        return true;
    }

    /// Records one finding and discards every partial rectangle produced before it.
    void report(Code code, ast::Position position, std::string message) {
        result_.nodes.clear();
        result_.diagnostics.push_back(diagnose(code, file_, position.line, position.column, std::move(message)));
    }

    std::string  file_;
    LayoutInputs inputs_{};
    LayoutResult result_{};
    std::int64_t spacing_{0};
    std::int64_t padding_{0};
};

}  // namespace

/// Implements the public layout-module entry point.
LayoutResult resolveLayout(const ast::Screen& screen, std::string file, LayoutInputs inputs) {
    return Solver{std::move(file), inputs}.run(screen);
}

}  // namespace mdux::tools::medui
