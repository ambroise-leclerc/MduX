/**
 * @brief Governed-zone fixed-budget draw types: what a frame is, before Vulkan sees it.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 *
 * Part of MduXCore. There is no Vulkan handle, no descriptor, and no device concept anywhere in
 * this module - `mdux_verify_trust_zones()` would fail the configure step if there were. A
 * `DrawList` is a description of a frame that could equally be rendered by something else,
 * inspected by a test, or compared against an expected frame without a GPU present. That is what
 * makes #125's headless verification and #126's pixel test possible at all.
 *
 * ## Fixed budget means the storage is the caller's
 *
 * A `DrawList` never allocates. It is constructed over spans the caller owns - typically a
 * `static` array sized from a budget the `.medui` compiler computed for the screen - and it fails
 * explicitly the moment a frame would exceed them. That failure is the point: a UI that silently
 * grew its buffers would have a per-frame cost nobody bounded, and on a device the first symptom
 * of that is a missed deadline rather than an error.
 *
 * `reset()` returns the list to empty without touching the storage, so the per-frame cost is a
 * few integer stores and nothing else.
 *
 * ## The vertex layout is shared with the shader, and neither side may drift
 *
 * `UiVertex` is exactly the 24-byte layout `recipes/shader/mdux-ui/ui.vert` declares. The static
 * assertions below pin the size, alignment and every member offset; the shader's own comment
 * names the same four fields. There is no mechanism that checks the two automatically - a GLSL
 * source is not something this module can read - so the assertions and the paired comments are
 * the mechanism, and changing one without the other should be uncomfortable.
 *
 * `DrawMode`'s values are likewise the shader's `modeSolid` / `modeCoverageR8` constants.
 */
module;

// offsetof is a macro, and `import std;` exports no macros - so the one thing that can pin the
// shader's vertex layout at compile time has to come through the global module fragment.
#include <cstddef>

export module mdux.draw;

import std;
import mdux.core.result;
import mdux.core.units;

export namespace mdux::draw {

/// Which fragment path a primitive takes. The values are the shader's, not an internal
/// enumeration: they are written into the vertex and switched on in `ui.frag`.
enum class DrawMode : std::uint32_t {
    Solid = 0,        ///< the vertex colour, untextured
    CoverageR8 = 1,   ///< the vertex colour masked by an R8 coverage atlas (glyphs, #14)
    SampledRgba = 2,  ///< the vertex colour modulating an RGBA atlas (images, #17)
};

/**
 * @brief One vertex, in the 24-byte layout the MduX UI shaders declare.
 *
 * Positions are in pixels with a top-left origin; the vertex shader converts them once from a
 * push constant, so nothing here depends on the surface the frame will be drawn to.
 */
/**
 * @brief Normalised texture coordinates, in [0, 1].
 *
 * Distinct from `core::Rect` because that is integer pixels and the shader samples with
 * normalised floats - an integer rectangle can express only 0 and 1, so a glyph's atlas slot
 * cannot be written as one. Keeping the two types apart means a caller who reaches for the wrong
 * one gets a compile error rather than a quad sampling far outside the sheet.
 */
struct UvRect {
    float u0{0.0F};
    float v0{0.0F};
    float u1{0.0F};
    float v1{0.0F};

    constexpr bool operator==(const UvRect&) const = default;
};

struct UiVertex {
    float x{0.0F};              ///< offset 0
    float y{0.0F};              ///< offset 4
    float u{0.0F};              ///< offset 8
    float v{0.0F};              ///< offset 12
    std::uint32_t color{0};     ///< offset 16, packed R8G8B8A8 in memory order
    std::uint32_t mode{0};      ///< offset 20, a DrawMode value

    constexpr bool operator==(const UiVertex&) const = default;
};

// The shader's vertex input layout, pinned. A change here that is not matched in
// recipes/shader/mdux-ui/ui.vert produces geometry that is silently wrong rather than a
// diagnostic, which is why these are assertions rather than documentation.
static_assert(sizeof(UiVertex) == 24, "UiVertex must match the shader's 24-byte vertex");
static_assert(alignof(UiVertex) == 4, "UiVertex must be 4-byte aligned, as the shader expects");
static_assert(std::is_trivially_copyable_v<UiVertex>, "UiVertex is memcpy'd into a mapped buffer");
static_assert(offsetof(UiVertex, x) == 0, "position must be at offset 0");
static_assert(offsetof(UiVertex, u) == 8, "uv must be at offset 8");
static_assert(offsetof(UiVertex, color) == 16, "colour must be at offset 16");
static_assert(offsetof(UiVertex, mode) == 20, "mode must be at offset 20");

/// Indices are 16-bit, which caps a list at 65536 vertices. That is a deliberate ceiling rather
/// than an oversight: it halves the index buffer, and a governed screen that needs more than
/// sixteen thousand rectangles in one frame has a budget problem the index width would only hide.
using Index = std::uint16_t;

inline constexpr std::uint32_t maxIndexableVertices = 65536;

/// One recorded draw: a contiguous run of indices, under one clip rectangle.
struct DrawCommand {
    std::uint32_t firstIndex{0};
    std::uint32_t indexCount{0};
    mdux::core::Rect clip{};

    constexpr bool operator==(const DrawCommand&) const = default;
};

/**
 * @brief The ceiling a `DrawList` is built against.
 *
 * Computed once by the `.medui` compiler for a screen (#15) and baked, so the storage a device
 * needs is known before the device runs. Until that compiler exists, a caller states it directly.
 */
struct DrawBudget {
    std::uint32_t maxVertices{0};
    std::uint32_t maxIndices{0};
    std::uint32_t maxCommands{0};

    constexpr bool operator==(const DrawBudget&) const = default;
};

enum class DrawError : std::uint8_t {
    EmptyBudget,             ///< a budget with no room for even one primitive
    BudgetExceedsIndexWidth, ///< maxVertices > maxIndexableVertices, which a 16-bit index cannot address
    StorageTooSmall,         ///< the spans supplied are smaller than the budget claims
    VertexBudgetExceeded,
    IndexBudgetExceeded,
    CommandBudgetExceeded,
    DegenerateRect,          ///< zero or negative width or height
    WrongList,               ///< a rollback marker names a position in a different list, or none
};

[[nodiscard]] std::string_view describe(DrawError error) noexcept;

/// Packs a colour into the 32-bit form the vertex carries, in memory order R, G, B, A.
///
/// Written byte by byte rather than by shifting into a `std::uint32_t`, because the shader reads
/// these four bytes as `R8G8B8A8_UNORM` in *memory* order - a shift-based packing would produce
/// different bytes on a big-endian host, and the whole point of a governed type is that it means
/// the same thing everywhere.
[[nodiscard]] constexpr std::uint32_t packColor(mdux::core::ColorRgba8 color) noexcept {
    // `std::bit_cast` from a four-byte array is what makes the paragraph above true. The obvious
    // `r | g << 8 | b << 16 | a << 24` produces the intended bytes only on a little-endian host;
    // on a big-endian one it reverses them, and the shader would read alpha where it expects red.
    // Constructing the array and reinterpreting it states the memory order directly, and is
    // constexpr, so the packing is fixed at compile time exactly as the shift form was.
    const std::array<std::uint8_t, 4> bytes{color.r, color.g, color.b, color.a};
    return std::bit_cast<std::uint32_t>(bytes);
}

/**
 * @brief A bounded, non-allocating description of one frame.
 *
 * Construct over caller-owned storage, add primitives, hand the spans to a renderer, `reset()`,
 * repeat. Every `add*` either records the primitive completely or records nothing and returns an
 * error - there is no partial state to unwind.
 *
 * Consecutive primitives sharing a clip rectangle extend the current command rather than starting
 * a new one. That keeps the command count proportional to the number of clip changes rather than
 * to the number of rectangles, which is what makes a command budget a meaningful number.
 */
class DrawList {
public:
    /// Builds a list over `vertices`, `indices` and `commands`, bounded by `budget`.
    ///
    /// The spans must each be at least as large as the budget claims. Passing storage smaller
    /// than the budget is rejected here rather than discovered on the frame that overflows it.
    [[nodiscard]] static mdux::core::Result<DrawList, DrawError> create(
        std::span<UiVertex> vertices, std::span<Index> indices, std::span<DrawCommand> commands,
        const DrawBudget& budget) noexcept;

    /// Records an axis-aligned rectangle in `mode`, sampling `uv` when the mode uses the atlas.
    ///
    /// `uv` is in **normalised** texture coordinates, because the fragment shader samples with
    /// `texture(uAtlas, fragUv)` on a `sampler2D`. See `UvRect` for why this overload exists
    /// alongside the `core::Rect` one.
    [[nodiscard]] mdux::core::ResultVoid<DrawError> addRect(const mdux::core::Rect& rect,
                                                            mdux::core::ColorRgba8 color,
                                                            DrawMode mode,
                                                            const UvRect& uv) noexcept;

    /// Records an axis-aligned rectangle in `mode`, sampling `uv` when the mode uses the atlas.
    ///
    /// Retained for callers whose uv is already whole - a full-sheet quad, or the unit rect. It
    /// converts to `UvRect` unchanged, so it cannot express a fractional coordinate: a glyph's
    /// slot has to go through `mdux.text.draw`, which normalises it against the atlas extent.
    [[nodiscard]] mdux::core::ResultVoid<DrawError> addRect(const mdux::core::Rect& rect,
                                                            mdux::core::ColorRgba8 color,
                                                            DrawMode mode,
                                                            const mdux::core::Rect& uv) noexcept;

    /// Records an untextured rectangle. The common case, and the only one #124 needs.
    [[nodiscard]] mdux::core::ResultVoid<DrawError> addSolidRect(
        const mdux::core::Rect& rect, mdux::core::ColorRgba8 color) noexcept;

    /// Sets the clip rectangle applied to subsequent primitives. A change starts a new command.
    void setClip(const mdux::core::Rect& clip) noexcept;

    /// Empties the list without touching the storage or the budget.
    void reset() noexcept;

    /**
     * @brief A position in the list, for undoing a composite record that fails partway.
     *
     * Opaque, and only its own list can read it. The counters were public in the first version of
     * this, which meant a caller could assemble one by hand or hand back a marker from a different
     * list - and `rollback()` writes through `commandCount`, so a marker claiming more commands
     * than the storage holds is an out-of-bounds write rather than a wrong picture. Carrying the
     * owning list and keeping the fields private makes both mistakes unspellable.
     *
     * Copyable and cheap. The owner is compared, never dereferenced, so a marker outliving its
     * list is a mismatch rather than a dangling read.
     */
    class Marker {
    public:
        Marker() noexcept = default;

    private:
        friend class DrawList;

        const DrawList* owner{nullptr};
        std::uint32_t vertexCount{0};
        std::uint32_t indexCount{0};
        std::uint32_t commandCount{0};
        std::uint32_t lastCommandIndexCount{0};
        mdux::core::Rect clip{};
    };

    /// The list's current position, for a later `rollback()`.
    [[nodiscard]] Marker mark() const noexcept;

    /**
     * @brief Discards everything recorded since `marker`, restoring the clip that was in force.
     *
     * For the caller that records several primitives as one unit and must not leave half of it in
     * the frame - a glyph run whose fourth record names a glyph the package does not have should
     * draw no glyphs, not three. `reset()` cannot do this: it empties the whole list, including
     * whatever was recorded before the unit began.
     *
     * Restoring `commandCount` alone is not enough, which is why the marker carries a fourth
     * number: `addRect()` extends the last command's `indexCount` when the clip has not changed
     * rather than starting a new command, so a rolled-back list would otherwise keep a command
     * claiming indices that are no longer there.
     *
     * A marker from another list, or a default-constructed one, is refused - it names a position
     * in something else. So is one whose counters exceed this list's, which can only mean the list
     * was already rolled back past it. Both return `WrongList`; neither can move a counter forward
     * or write outside the storage, which is the property that matters, since `rollback()` writes
     * through the command count.
     */
    [[nodiscard]] mdux::core::ResultVoid<DrawError> rollback(const Marker& marker) noexcept;

    [[nodiscard]] std::span<const UiVertex> vertices() const noexcept {
        return vertices_.subspan(0, vertexCount_);
    }
    [[nodiscard]] std::span<const Index> indices() const noexcept {
        return indices_.subspan(0, indexCount_);
    }
    [[nodiscard]] std::span<const DrawCommand> commands() const noexcept {
        return commands_.subspan(0, commandCount_);
    }
    [[nodiscard]] const DrawBudget& budget() const noexcept { return budget_; }
    [[nodiscard]] bool empty() const noexcept { return indexCount_ == 0; }

private:
    DrawList() noexcept = default;

    std::span<UiVertex> vertices_;
    std::span<Index> indices_;
    std::span<DrawCommand> commands_;
    DrawBudget budget_{};
    std::uint32_t vertexCount_{0};
    std::uint32_t indexCount_{0};
    std::uint32_t commandCount_{0};
    /// The clip every subsequent primitive is recorded under. A default-constructed Rect means
    /// "no clip", which is also what `reset()` restores - so no separate "is one set?" flag is
    /// needed, and one did exist here without ever being read.
    mdux::core::Rect clip_{};
};

}  // namespace mdux::draw
