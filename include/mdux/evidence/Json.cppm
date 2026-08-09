/**
 * @file Json.cppm
 * @brief Governed-zone canonical JSON: the writer byte-identity depends on, and a reader
 *        strict enough that a hand-edited artifact cannot pass verification.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-007 Evidence pipeline doctrine (canonical form, floats as bit patterns)
 *
 * Part of MduXCore. This is the component that makes byte-identity across MSVC, glibc and
 * libc++ possible - or, if it gets one rule wrong, impossible.
 *
 * ## Canonical form
 *
 * - object keys sorted lexicographically by UTF-8 code unit
 * - two-space indent, LF line endings, UTF-8 without a byte-order mark, trailing newline
 * - no timestamps, no absolute paths, no environment-dependent values anywhere
 * - `f32` encoded as its `u32` bit pattern, `{"bits": 1065353216}`, never as decimal text
 *
 * That last rule is the whole ballgame. `printf("%.9g")` and `std::format("{}", f)` are not
 * guaranteed byte-identical across the three standard libraries this project builds on, and
 * baked font metrics, layout bounds and ML golden vectors are all floats. A CI lint bans float
 * format specifiers under `src/evidence/` and `tools/` so a stray one cannot reintroduce the
 * problem on someone else's operating system only.
 *
 * ## Deliberate strictness
 *
 * The reader rejects duplicate keys, trailing commas, comments, `NaN`/`Infinity`, byte-order
 * marks, unescaped control characters, invalid UTF-8, trailing content after the top-level
 * value, and nesting past kMaxDepth.
 *
 * It also rejects **any** number carrying a fraction or exponent. Canonical MduX JSON never
 * contains one - a real number is always `{"bits": N}` - so a decimal float in a `generated/`
 * file means the file was hand-edited or written by something other than this writer. Accepting
 * it would let exactly the artifact this pipeline exists to catch pass verification. The
 * consequence is that this is a reader for canonical MduX JSON, not a general-purpose JSON
 * parser, which is the intended scope.
 *
 * ## Allocation and noexcept
 *
 * Building and parsing JSON allocates, so these functions are not allocation-free the way
 * mdux.evidence.digest is. They are still `noexcept`: under the `-fno-exceptions` builds
 * ADR-005 requires the governed zone to support, an allocation failure terminates regardless,
 * and terminating is the fail-closed outcome for a device that cannot record its own evidence.
 * `noexcept` here states that actual behaviour rather than papering over it. Every *logical*
 * failure is a `Result`, never a termination.
 */
module;

export module mdux.evidence.json;

import std;
import mdux.core.result;

export namespace mdux::evidence::json {

/// Maximum object/array nesting the reader accepts. Evidence artifacts are shallow - the
/// deepest shape any baker produces is a handful of levels - so this is a generous bound whose
/// only job is to stop a malformed or hostile file from exhausting the stack.
inline constexpr std::size_t kMaxDepth = 64;

enum class ErrorCode : std::uint8_t {
    UnexpectedEnd,
    UnexpectedCharacter,
    InvalidNumber,
    FractionalNumberRejected,   ///< a fraction or exponent, which canonical form never emits
    NumberOutOfRange,
    NonFiniteLiteralRejected,   ///< NaN, Infinity, -Infinity
    DuplicateKey,
    TrailingComma,
    CommentRejected,
    InvalidEscape,
    UnescapedControlCharacter,
    InvalidUtf8,
    ByteOrderMarkRejected,
    TrailingContent,
    DepthExceeded,
    WrongKind,                  ///< accessor asked for a type the value does not hold
    MissingMember,
    NotExactlyRepresentable,    ///< e.g. asUInt() on a negative integer
};

/// A parse or serialization failure. `offset` is a byte offset into the input for reader
/// errors and is 0 for writer and accessor errors, where there is no input to point at.
struct Error {
    ErrorCode code{};
    std::size_t offset{0};
    std::string detail;
};

/// Human-readable name for an ErrorCode, for diagnostics. Stable across platforms.
[[nodiscard]] std::string_view describe(ErrorCode code) noexcept;

class Value;

/// One object member. Objects hold these sorted by `key`.
struct Member;

/**
 * @brief A JSON value in canonical MduX form.
 *
 * Deliberately not a `std::variant`: the alternatives would include `std::vector<Value>` at a
 * point where `Value` is still incomplete, and while vector tolerates that, variant's
 * completeness requirements are murkier than is worth relying on in governed code. Explicit
 * members cost roughly a hundred bytes per value, which is irrelevant at evidence-artifact
 * sizes and buys a type whose validity nobody has to argue about.
 */
class Value {
public:
    enum class Kind : std::uint8_t { Null, Bool, Int, UInt, Float32, String, Array, Object };

    Value() noexcept = default;  ///< null

    [[nodiscard]] static Value null() noexcept;
    [[nodiscard]] static Value boolean(bool value) noexcept;
    [[nodiscard]] static Value integer(std::int64_t value) noexcept;
    [[nodiscard]] static Value unsignedInteger(std::uint64_t value) noexcept;
    [[nodiscard]] static Value string(std::string value) noexcept;
    [[nodiscard]] static Value array(std::vector<Value> elements) noexcept;
    [[nodiscard]] static Value emptyObject() noexcept;

    /**
     * @brief A 32-bit float, stored and emitted as its bit pattern.
     *
     * Every float in an evidence artifact goes through here. There is no overload taking a
     * `double`, on purpose: a `double` written as bits would be a `u64`, and no baker has a
     * reason to emit one. Adding that overload later is a schema decision, not a convenience.
     */
    [[nodiscard]] static Value float32(float value) noexcept;

    [[nodiscard]] Kind kind() const noexcept { return kind_; }

    [[nodiscard]] mdux::core::Result<bool, Error> asBool() const noexcept;
    [[nodiscard]] mdux::core::Result<std::int64_t, Error> asInt() const noexcept;
    [[nodiscard]] mdux::core::Result<std::uint64_t, Error> asUInt() const noexcept;
    [[nodiscard]] mdux::core::Result<std::string_view, Error> asString() const noexcept;

    /**
     * @brief Decodes a float from the canonical `{"bits": N}` encoding.
     *
     * Accepts either a Float32 value built by float32(), or - which is the case after parsing -
     * an Object with exactly one member named `bits` holding an integer in `[0, 2^32)`.
     *
     * Float-ness is decided by the caller asking for a float, never by the parser guessing from
     * shape. An object that happens to have a `bits` member stays an object until something
     * asks it to be a float, which keeps the reader free of a heuristic that could
     * misinterpret a future schema.
     */
    [[nodiscard]] mdux::core::Result<float, Error> asFloat32() const noexcept;

    /// Elements of an Array. Empty span for any other kind - check kind() first if that matters.
    [[nodiscard]] std::span<const Value> elements() const noexcept;

    /// Members of an Object, sorted by key. Empty span for any other kind.
    [[nodiscard]] std::span<const Member> members() const noexcept;

    /// The member named `key`, or nullptr if absent or if this is not an Object.
    [[nodiscard]] const Value* find(std::string_view key) const noexcept;

    /// find() with a MissingMember error instead of a null pointer, for chained access.
    [[nodiscard]] mdux::core::Result<const Value*, Error> require(std::string_view key) const noexcept;

    /**
     * @brief Inserts a member, keeping members sorted by key.
     *
     * Fails with DuplicateKey rather than overwriting, and with WrongKind if this is not an
     * Object. Rejecting duplicates here rather than at write time means a baker that builds a
     * malformed object learns about it at the call site that caused it.
     */
    [[nodiscard]] mdux::core::ResultVoid<Error> set(std::string key, Value value) noexcept;

    /// Appends to an Array. Fails with WrongKind if this is not an Array.
    [[nodiscard]] mdux::core::ResultVoid<Error> push(Value value) noexcept;

private:
    Kind kind_{Kind::Null};
    bool boolean_{false};
    std::int64_t int_{0};
    std::uint64_t uint_{0};
    std::uint32_t floatBits_{0};
    std::string string_;
    std::vector<Value> elements_;
    std::vector<Member> members_;
};

struct Member {
    std::string key;
    Value value;
};

/**
 * @brief Serializes `value` in canonical form, trailing newline included.
 *
 * Object members are sorted by key regardless of insertion order, so the output depends only
 * on the value's content. Fails with InvalidUtf8 on a malformed string or key, and with
 * DuplicateKey if an object somehow holds two members with the same key.
 */
[[nodiscard]] mdux::core::Result<std::string, Error> write(const Value& value) noexcept;

/**
 * @brief Parses canonical MduX JSON strictly. See the module comment for what it rejects.
 *
 * `text` must be UTF-8 without a byte-order mark. A trailing newline is permitted (the writer
 * emits one); any other trailing content is TrailingContent.
 */
[[nodiscard]] mdux::core::Result<Value, Error> parse(std::string_view text) noexcept;

}  // namespace mdux::evidence::json
