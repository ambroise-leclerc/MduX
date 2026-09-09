/**
 * @file ScenarioScript.cppm
 * @brief The `.scenario` line-oriented DSL: parse one to an AST and validate it against a screen.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone: parses untrusted input, may throw)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw)
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 *
 * One directive per line, `#` to end of line is a comment, blank lines are ignored. The grammar is
 * closed and deliberately small - it matches TrustSC's scenario shape and the `.medui`
 * component-body parser's "one property per line" convention, so a scenario is a portable
 * statement about a shared model rather than about one implementation.
 *
 * ```
 * scenario <id>
 * screen <screen-id>
 * version <N>
 * clock <YYYY-MM-DD> <HH:MM:SS>
 * sample <beatPeriod> [<warmupFrames>]        # optional
 * requirement <REQ-ID> [<REQ-ID> ...]         # optional, repeatable
 *
 * pointer <down|up|move|cancel> <node>        # <node> resolved to its rectangle's centre pixel
 * key <KeyCode>                               # a KeyCode wire spelling, e.g. Commit, Cancel
 * text <scalar-or-"run">                      # one Unicode scalar, or a quoted ASCII run
 * focus <enter|leave> <node>
 * advance [<N>]                               # consume the queued batch; run N updates (default 1)
 * expect <kind> <args...>                     # check one fact about the state the advance settled
 * capture <name>                              # the replay must hand this frame to its capture fn
 * ```
 *
 * `expect` kinds and their arguments:
 *
 * ```
 * expect clock <HH:MM:SS>
 * expect field "<value>" [caret <N>]
 * expect refused <N>
 * expect action <node> <NoOp|TriggerHalt> <REQ-ID>
 * expect button <node> <source>
 * expect reading <node> <int>                 # in the NumericDisplay template's fixed-point units
 * expect state <node> <index>
 * expect latch <node|->                       # '-' means disarmed
 * expect frame <nodes|rects|deferred|traces|readings|states|fields> <N>
 * expect overflow <true|false>
 * ```
 */
module;

export module mdux.tools.scenario.script;

import std;
import mdux.core.units;
import mdux.medui.scenario;
import mdux.medui.input;
import mdux.medui.reading;
import mdux.medui.schema;
import mdux.tools.cli;

export namespace mdux::tools::scenario {

namespace ms = mdux::medui;

/// One node of the screen the scenario names, as the compiler needs it: its id, its component
/// kind (the dictionary name, e.g. `TextInput`), and the centre of its resolved rectangle - the
/// pixel a `pointer <node>` directive resolves to.
struct ScreenNode {
    std::string      id;
    std::string      kind;
    mdux::core::Px   centreX{0};
    mdux::core::Px   centreY{0};
};

/// The owned mirror of `mdux::medui::Expectation` - `std::string` where the compiled form has a
/// `string_view`, a `std::u32string` where it has a span.
struct ScriptExpect {
    ms::ExpectKind      kind{ms::ExpectKind::Unspecified};
    ms::CivilTime       clock{};
    std::u32string      fieldValue{};
    bool                fieldHasCaret{false};
    std::uint32_t       caret{0};
    std::uint32_t       count{0};
    std::string         nodeId{};
    ms::SystemEvent     event{ms::SystemEvent::Unspecified};
    std::string         requirement{};
    std::string         source{};
    std::int64_t        value{0};
    ms::FrameStatField  statField{ms::FrameStatField::Unspecified};
    bool                flag{false};
};

/// One parsed step, plus the source line it came from for diagnostics.
struct ScriptStep {
    ms::StepKind          kind{ms::StepKind::Advance};
    ms::PointerEvent      pointer{};
    ms::KeyEvent          key{};
    ms::TextEvent         text{};
    ms::FocusKind         focusKind{ms::FocusKind::Unspecified};
    std::string           focusNode{};
    std::uint32_t         frames{1};
    ScriptExpect          expect{};
    std::string           capture{};
    std::size_t           line{0};
};

/// A fully parsed and validated scenario script.
struct Script {
    std::string                  id{};
    std::string                  screenId{};
    std::uint32_t                version{0};
    ms::CivilTime                clock{};
    ms::ScenarioSampleSeed       sampleSeed{};
    std::vector<std::string>     requirements{};
    std::vector<std::string>     captureNames{};  ///< distinct capture-step names, first-seen order
    std::vector<ScriptStep>      steps{};
};

/**
 * @brief Parses and validates `.scenario` text.
 *
 * @param text           the script's bytes as UTF-8
 * @param scriptPath     repository-relative, for diagnostics
 * @param expectedScreen the screen id the recipe declared - a `screen` line naming another is `SCN005`
 * @param nodes          the screen's nodes, for resolving `pointer <node>` and validating targets
 * @param diagnostics    appended to; `std::nullopt` means it did not parse or validate
 *
 * Stable `SCN0NN` codes (see `docs/governance/schemas/diagnostic.schema.json`):
 * - `SCN001` an unknown directive, event kind, `KeyCode`, `expect` kind or `SystemEvent`
 * - `SCN002` `scenario`, `screen`, `version` or `clock` missing, repeated, or out of order
 * - `SCN003` more steps / expectations / captures / requirements than the bound allows
 * - `SCN004` a `version` this compiler does not implement
 * - `SCN005` a `pointer`/`focus`/`expect` naming a node the screen lacks, or a wrong-kind target,
 *            or a `screen` line disagreeing with the recipe
 * - `SCN006` a malformed clock, requirement id, scalar or number
 * - `SCN007` a `capture` name reused, or an `expect` kind's arguments are wrong in arity
 * - `SCN008` events after the last `advance` (nothing consumes them), or no `advance` at all
 */
[[nodiscard]] std::optional<Script> parseScript(std::string_view              text,
                                                std::string_view              scriptPath,
                                                std::string_view              expectedScreen,
                                                std::span<const ScreenNode>   nodes,
                                                std::vector<cli::Diagnostic>& diagnostics);

}  // namespace mdux::tools::scenario
