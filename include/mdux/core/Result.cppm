/**
 * @file Result.cppm
 * @brief Governed-zone error handling: a Result<T, E> alias over std::expected.
 *
 * @compliance ADR-005 Error handling and exceptions policy
 *
 * Part of MduXCore (see ADR-004, ADR-005). Governed code uses std::expected and
 * noexcept throughout, never throws. `mdux::core::Result` is a naming convenience
 * over std::expected, not a reimplementation - ADR-005 rejected writing a bespoke
 * Result type since std::expected already exists and is available on every
 * compiler this project supports.
 */
module;

export module mdux.core.result;

import std;

export namespace mdux::core {

template <typename T, typename E>
using Result = std::expected<T, E>;

template <typename E>
using ResultVoid = std::expected<void, E>;

/// Shorthand matching std::unexpected, so call sites read `err(SomeError{...})`
/// instead of `std::unexpected(SomeError{...})`.
template <typename E>
[[nodiscard]] constexpr std::unexpected<std::decay_t<E>> err(E&& error) {
    return std::unexpected<std::decay_t<E>>(std::forward<E>(error));
}

}  // namespace mdux::core
