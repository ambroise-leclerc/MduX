/**
 * @file Ir.cppm
 * @brief The compiler's resolved intermediate representation, as canonical JSON.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine (canonical JSON, float bit patterns)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * Host-only, and #265's third acceptance says so in as many words: nothing here enters a device
 * build. It is in `MduXMeduiLib` beside the stages it reads, which is a host-tools target no device
 * target links.
 *
 * ## What the IR is, and why it is worth dumping
 *
 * The compiler's stages each answer one question and hand the next stage a value. `package.json`
 * records the *conclusion* - where every node ended up and which validated names it carries - and
 * throws the working away. That is right for an artifact a device holds, and it leaves an author
 * with no way to see why a box came out where it did.
 *
 * This is that working: the bounded box tree `resolveLayout()` produced, each node's authored fields
 * with their values rendered, the colour every `Theme.Colors.<Token>` resolves to, and the text
 * measurement each budgeted node was checked against. An agent that has just been handed
 * `MEDUI-E050` - "this box is too small for its widest approved translation" - can read the extent
 * that failed and the locale that produced it rather than inferring them.
 *
 * ## Floats are bit patterns, for the same reason baked artifacts are
 *
 * A resolved colour is four `float`s, and ADR-007 decision 2 is why they are emitted as `u32` bit
 * patterns through `json::Value::float32()` rather than as decimal text: a decimal rendering has a
 * rounding rule this project would then have to pin across three toolchains, and the whole point of
 * a dump is that two runs of the same commit can be diffed. The encoding is the one every baked
 * artifact already uses, so a consumer needs no second reader.
 *
 * ## Built on every compile, not only when asked
 *
 * `run()` fills `CompileOutputs::irJson` unconditionally. That costs one JSON document per compile
 * of a screen with tens of nodes, which is nothing on a host, and it buys the property that matters:
 * `--dump-ir` shows what *this* compile resolved rather than what a second compile would. A dump
 * produced by re-running the stages could disagree with the artifact beside it, and the one thing
 * an intermediate representation must not do is describe a different compile from the one whose
 * output you are holding.
 */
module;

export module mdux.tools.medui.ir;

import std;
import mdux.evidence.json;
import mdux.tools.medui.ast;
import mdux.tools.medui.layout;
import mdux.tools.medui.textbudget;

export namespace mdux::tools::medui {

/// The schema version of the emitted IR, bumped when a section is added, removed or reshaped.
inline constexpr std::uint64_t irSchemaVersion = 1;

/**
 * @brief The resolved intermediate representation of one compiled screen.
 *
 * @param screenId    the artifact id, so a dump names the screen it describes
 * @param screen      the parsed source, for the layout block and the surface it declared
 * @param layout      the bounded box tree, which is the IR's spine
 * @param measurements the text budget results, empty for a screen that carries no text
 *
 * Pure: it reads values the stages already produced and touches no file, no clock and no
 * environment, so two calls over one compile produce the same document.
 */
[[nodiscard]] mdux::evidence::json::Value
screenIr(std::string_view screenId, const ast::Screen& screen, const LayoutResult& layout, std::span<const TextMeasurement> measurements);

/**
 * @brief `screenIr()` serialised as canonical JSON text.
 *
 * Canonical form ends with a newline, so the returned string is exactly what a file would hold and
 * `mdux-meduic --dump-ir <recipe> > screen.ir.json` is the whole of writing one.
 *
 * @throws std::logic_error if the document cannot be serialised, which would mean this module built
 *         a value `mdux.evidence.json` refuses - a defect here rather than anything a caller did.
 */
[[nodiscard]] std::string
screenIrJson(std::string_view screenId, const ast::Screen& screen, const LayoutResult& layout, std::span<const TextMeasurement> measurements);

}  // namespace mdux::tools::medui
