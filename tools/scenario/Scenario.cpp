/**
 * @file Scenario.cpp
 * @brief Implementation of the scenario baker's recipe model and bake/verify core.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-020 Bounded interaction scenarios and their replay
 */
module;

module mdux.tools.scenario;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.medui.scenario;
import mdux.medui.input;
import mdux.medui.reading;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.tools.scenario.script;
import mdux.tools.toml;

namespace mdux::tools::scenario {

namespace json = mdux::evidence::json;
namespace ms   = mdux::medui;

namespace {

// Stable diagnostic codes for this stage. The script parser owns SCN001-SCN008; the baker's own
// are SCN020+.
constexpr std::string_view recipeUnparsed   = "SCN020";
constexpr std::string_view recipeMissing     = "SCN021";
constexpr std::string_view sourceUnreadable  = "SCN022";
constexpr std::string_view screenUnreadable  = "SCN023";
constexpr std::string_view screenMalformed   = "SCN024";
constexpr std::string_view outputUnwritable  = "SCN025";
constexpr std::string_view artifactMissing   = "SCN026";
constexpr std::string_view artifactDiffers   = "SCN027";
constexpr std::string_view internalError     = "SCN028";

void report(std::vector<cli::Diagnostic>& diagnostics, std::string file, std::size_t line,
            std::string_view code, std::string message, std::string fixHint = {}) {
    diagnostics.push_back(cli::Diagnostic{.file     = std::move(file),
                                          .line     = line,
                                          .code     = std::string{code},
                                          .severity = cli::Severity::Error,
                                          .message  = std::move(message),
                                          .fixHint  = std::move(fixHint)});
}

[[nodiscard]] std::string reportPath(const std::filesystem::path& path) { return path.generic_string(); }

[[nodiscard]] std::span<const std::byte> asBytes(const std::string& text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

[[nodiscard]] evidence::FileRecord fileRecord(std::string path, std::span<const std::byte> bytes) {
    return evidence::FileRecord{.path = std::move(path), .sha256 = evidence::sha256(bytes)};
}

/// A `CivilTime` as the canonical `{ year, month, ... }` object.
[[nodiscard]] json::Value clockObject(const ms::CivilTime& c) {
    json::Value o = json::Value::emptyObject();
    static_cast<void>(o.set("year", json::Value::integer(c.year)));
    static_cast<void>(o.set("month", json::Value::unsignedInteger(c.month)));
    static_cast<void>(o.set("day", json::Value::unsignedInteger(c.day)));
    static_cast<void>(o.set("hour", json::Value::unsignedInteger(c.hour)));
    static_cast<void>(o.set("minute", json::Value::unsignedInteger(c.minute)));
    static_cast<void>(o.set("second", json::Value::unsignedInteger(c.second)));
    return o;
}

/// A `std::u32string` as a canonical array of code-point integers - a scalar list, never text, so
/// the reader never has to decode UTF-8 out of an evidence artifact.
[[nodiscard]] json::Value scalarArray(const std::u32string& text) {
    json::Value a = json::Value::array({});
    for (const char32_t cp : text) {
        static_cast<void>(a.push(json::Value::unsignedInteger(static_cast<std::uint64_t>(cp))));
    }
    return a;
}

[[nodiscard]] json::Value expectObject(const ScriptExpect& e) {
    json::Value o = json::Value::emptyObject();
    static_cast<void>(o.set("kind", json::Value::string(std::string{ms::toWire(e.kind)})));
    switch (e.kind) {
        case ms::ExpectKind::Clock:
            static_cast<void>(o.set("clock", clockObject(e.clock)));
            break;
        case ms::ExpectKind::Field:
            static_cast<void>(o.set("value", scalarArray(e.fieldValue)));
            if (e.fieldHasCaret) {
                static_cast<void>(o.set("caret", json::Value::unsignedInteger(e.caret)));
            }
            break;
        case ms::ExpectKind::RefusedEdits:
            static_cast<void>(o.set("count", json::Value::unsignedInteger(e.count)));
            break;
        case ms::ExpectKind::Action:
            static_cast<void>(o.set("node", json::Value::string(e.nodeId)));
            static_cast<void>(o.set("event", json::Value::string(std::string{ms::toWire(e.event)})));
            static_cast<void>(o.set("requirement", json::Value::string(e.requirement)));
            break;
        case ms::ExpectKind::ButtonSource:
            static_cast<void>(o.set("node", json::Value::string(e.nodeId)));
            static_cast<void>(o.set("source", json::Value::string(e.source)));
            break;
        case ms::ExpectKind::Reading:
            static_cast<void>(o.set("node", json::Value::string(e.nodeId)));
            static_cast<void>(o.set("value", json::Value::integer(e.value)));
            break;
        case ms::ExpectKind::State:
            static_cast<void>(o.set("node", json::Value::string(e.nodeId)));
            static_cast<void>(o.set("index", json::Value::unsignedInteger(e.count)));
            break;
        case ms::ExpectKind::LatchArmed:
            static_cast<void>(o.set("node", json::Value::string(e.nodeId)));  // empty = disarmed
            break;
        case ms::ExpectKind::FrameStat:
            static_cast<void>(o.set("field", json::Value::string(std::string{ms::toWire(e.statField)})));
            static_cast<void>(o.set("count", json::Value::unsignedInteger(e.count)));
            break;
        case ms::ExpectKind::Overflow:
            static_cast<void>(o.set("value", json::Value::boolean(e.flag)));
            break;
        case ms::ExpectKind::Unspecified:
            break;
    }
    return o;
}

[[nodiscard]] json::Value stepObject(const ScriptStep& s) {
    json::Value o = json::Value::emptyObject();
    static_cast<void>(o.set("kind", json::Value::string(std::string{ms::toWire(s.kind)})));
    switch (s.kind) {
        case ms::StepKind::Advance:
            static_cast<void>(o.set("frames", json::Value::unsignedInteger(s.frames)));
            break;
        case ms::StepKind::Pointer: {
            json::Value p = json::Value::emptyObject();
            static_cast<void>(p.set("transition", json::Value::string(std::string{ms::toWire(s.pointer.kind)})));
            static_cast<void>(p.set("x", json::Value::integer(s.pointer.x)));
            static_cast<void>(p.set("y", json::Value::integer(s.pointer.y)));
            static_cast<void>(o.set("pointer", std::move(p)));
            break;
        }
        case ms::StepKind::Key: {
            json::Value k = json::Value::emptyObject();
            static_cast<void>(k.set("transition", json::Value::string(std::string{ms::toWire(s.key.kind)})));
            static_cast<void>(k.set("code", json::Value::string(std::string{ms::toWire(s.key.key)})));
            static_cast<void>(o.set("key", std::move(k)));
            break;
        }
        case ms::StepKind::Text:
            static_cast<void>(o.set("scalar", json::Value::unsignedInteger(static_cast<std::uint64_t>(s.text.scalar))));
            break;
        case ms::StepKind::Focus: {
            json::Value fo = json::Value::emptyObject();
            static_cast<void>(fo.set("transition", json::Value::string(std::string{ms::toWire(s.focusKind)})));
            static_cast<void>(fo.set("node", json::Value::string(s.focusNode)));
            static_cast<void>(o.set("focus", std::move(fo)));
            break;
        }
        case ms::StepKind::Expect:
            static_cast<void>(o.set("expect", expectObject(s.expect)));
            break;
        case ms::StepKind::Capture:
            static_cast<void>(o.set("name", json::Value::string(s.capture)));
            break;
    }
    return o;
}

}  // namespace

// ---------------------------------------------------------------------------
// readFile / parseRecipe
// ---------------------------------------------------------------------------

std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    const std::streamoff size = file.tellg();
    if (size < 0 || !file.seekg(0)) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!file) {
            return std::nullopt;
        }
    }
    return bytes;
}

std::optional<Recipe> parseRecipe(std::string_view text, std::string_view recipePath,
                                  std::vector<cli::Diagnostic>& diagnostics) {
    toml::Document document{};
    try {
        document = toml::parse(text);
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeUnparsed, error.what(),
               "See tools/scenario/Scenario.cppm for the recipe format.");
        return std::nullopt;
    }

    const toml::Table* package = document.table("package");
    if (package == nullptr) {
        report(diagnostics, std::string{recipePath}, 0, recipeMissing, "recipe has no [package] table",
               "Add [package] with id, source and screen keys.");
        return std::nullopt;
    }

    Recipe recipe;
    try {
        recipe.id     = package->require("id").asString();
        recipe.source = package->require("source").asString();
        recipe.screen = package->require("screen").asString();
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeMissing, error.what(),
               "[package] needs id, source and screen keys, all strings.");
        return std::nullopt;
    }
    return recipe;
}

// ---------------------------------------------------------------------------
// readScreenNodes
// ---------------------------------------------------------------------------

std::optional<std::vector<ScreenNode>> readScreenNodes(const std::filesystem::path& packagePath,
                                                       std::vector<cli::Diagnostic>& diagnostics) {
    auto bytes = readFile(packagePath);
    if (!bytes.has_value()) {
        report(diagnostics, reportPath(packagePath), 0, screenUnreadable, "cannot read the screen package",
               "Run `cmake --build <dir> --target mdux-bake-update` to stage the screen it names.");
        return std::nullopt;
    }
    const std::string_view textView{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
    auto parsed = json::parse(textView);
    if (!parsed.has_value()) {
        report(diagnostics, reportPath(packagePath), 0, screenMalformed,
               std::string{"the screen package is not canonical JSON: "} + std::string{json::describe(parsed.error().code)});
        return std::nullopt;
    }

    const json::Value* nodes = parsed->find("nodes");
    if (nodes == nullptr || nodes->kind() != json::Value::Kind::Array) {
        report(diagnostics, reportPath(packagePath), 0, screenMalformed, "the screen package has no `nodes` array");
        return std::nullopt;
    }

    std::vector<ScreenNode> out;
    for (const json::Value& node : nodes->elements()) {
        const json::Value* id     = node.find("id");
        const json::Value* kind   = node.find("kind");
        const json::Value* bounds = node.find("bounds");
        if (id == nullptr || kind == nullptr || bounds == nullptr) {
            report(diagnostics, reportPath(packagePath), 0, screenMalformed, "a screen node is missing id, kind or bounds");
            return std::nullopt;
        }
        const auto idStr   = id->asString();
        const auto kindStr = kind->asString();
        const json::Value* x = bounds->find("x");
        const json::Value* y = bounds->find("y");
        const json::Value* w = bounds->find("width");
        const json::Value* h = bounds->find("height");
        if (!idStr.has_value() || !kindStr.has_value() || x == nullptr || y == nullptr || w == nullptr || h == nullptr) {
            report(diagnostics, reportPath(packagePath), 0, screenMalformed, "a screen node's fields are malformed");
            return std::nullopt;
        }
        const auto xi = x->asInt();
        const auto yi = y->asInt();
        const auto wi = w->asInt();
        const auto hi = h->asInt();
        if (!xi || !yi || !wi || !hi) {
            report(diagnostics, reportPath(packagePath), 0, screenMalformed, "a screen node's bounds are not integers");
            return std::nullopt;
        }
        out.push_back(ScreenNode{.id      = std::string{*idStr},
                                 .kind    = std::string{*kindStr},
                                 .centreX = static_cast<mdux::core::Px>(*xi + *wi / 2),
                                 .centreY = static_cast<mdux::core::Px>(*yi + *hi / 2)});
    }
    return out;
}

// ---------------------------------------------------------------------------
// renderScenarioJson
// ---------------------------------------------------------------------------

mdux::core::Result<std::string, json::Error> renderScenarioJson(const Script& script) {
    json::Value root = json::Value::emptyObject();
    static_cast<void>(root.set("schemaVersion", json::Value::unsignedInteger(ms::currentScenarioSchemaVersion)));
    static_cast<void>(root.set("kind", json::Value::string("scenario")));
    static_cast<void>(root.set("id", json::Value::string(script.id)));
    static_cast<void>(root.set("screenId", json::Value::string(script.screenId)));
    static_cast<void>(root.set("pinnedClock", clockObject(script.clock)));

    json::Value seed = json::Value::emptyObject();
    static_cast<void>(seed.set("beatPeriod", json::Value::unsignedInteger(script.sampleSeed.beatPeriod)));
    static_cast<void>(seed.set("warmupFrames", json::Value::unsignedInteger(script.sampleSeed.warmupFrames)));
    static_cast<void>(root.set("sampleSeed", std::move(seed)));

    json::Value reqs = json::Value::array({});
    for (const std::string& r : script.requirements) {
        static_cast<void>(reqs.push(json::Value::string(r)));
    }
    static_cast<void>(root.set("requirements", std::move(reqs)));

    json::Value caps = json::Value::array({});
    for (const std::string& c : script.captureNames) {
        static_cast<void>(caps.push(json::Value::string(c)));
    }
    static_cast<void>(root.set("captureNames", std::move(caps)));

    json::Value steps = json::Value::array({});
    for (const ScriptStep& s : script.steps) {
        static_cast<void>(steps.push(stepObject(s)));
    }
    static_cast<void>(root.set("steps", std::move(steps)));

    return json::write(root);
}

// ---------------------------------------------------------------------------
// readScenarioDoc
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::optional<ms::CivilTime> clockFrom(const json::Value& o) {
    const auto y  = o.find("year");
    const auto mo = o.find("month");
    const auto d  = o.find("day");
    const auto h  = o.find("hour");
    const auto mi = o.find("minute");
    const auto s  = o.find("second");
    if (y == nullptr || mo == nullptr || d == nullptr || h == nullptr || mi == nullptr || s == nullptr) {
        return std::nullopt;
    }
    const auto yi = y->asInt();
    const auto moi = mo->asUInt();
    const auto di = d->asUInt();
    const auto hi = h->asUInt();
    const auto mii = mi->asUInt();
    const auto si = s->asUInt();
    if (!yi || !moi || !di || !hi || !mii || !si) {
        return std::nullopt;
    }
    return ms::CivilTime{.year   = static_cast<std::int32_t>(*yi),
                         .month  = static_cast<std::uint8_t>(*moi),
                         .day    = static_cast<std::uint8_t>(*di),
                         .hour   = static_cast<std::uint8_t>(*hi),
                         .minute = static_cast<std::uint8_t>(*mii),
                         .second = static_cast<std::uint8_t>(*si)};
}

}  // namespace

std::optional<Script> readScenarioDoc(std::span<const std::byte> scenarioJson, std::string_view scenarioPath,
                                      std::vector<cli::Diagnostic>& diagnostics) {
    const std::string_view textView{reinterpret_cast<const char*>(scenarioJson.data()), scenarioJson.size()};
    auto parsed = json::parse(textView);
    if (!parsed.has_value()) {
        report(diagnostics, std::string{scenarioPath}, 0, screenMalformed,
               std::string{"scenario.json is not canonical JSON: "} + std::string{json::describe(parsed.error().code)});
        return std::nullopt;
    }
    const json::Value& root = *parsed;

    const auto badShape = [&](std::string what) {
        report(diagnostics, std::string{scenarioPath}, 0, screenMalformed, "scenario.json is malformed: " + std::move(what));
        return std::nullopt;
    };

    Script script;
    const auto* schemaVersion = root.find("schemaVersion");
    const auto* id            = root.find("id");
    const auto* screenId      = root.find("screenId");
    const auto* clock         = root.find("pinnedClock");
    const auto* seed          = root.find("sampleSeed");
    const auto* reqs          = root.find("requirements");
    const auto* caps          = root.find("captureNames");
    const auto* steps         = root.find("steps");
    if (schemaVersion == nullptr || id == nullptr || screenId == nullptr || clock == nullptr || seed == nullptr
        || reqs == nullptr || caps == nullptr || steps == nullptr) {
        return badShape("a top-level member is missing");
    }
    const auto sv = schemaVersion->asUInt();
    if (!sv) {
        return badShape("schemaVersion is not an integer");
    }
    script.version = static_cast<std::uint32_t>(*sv);
    if (const auto s = id->asString(); s) {
        script.id = std::string{*s};
    } else {
        return badShape("id is not a string");
    }
    if (const auto s = screenId->asString(); s) {
        script.screenId = std::string{*s};
    } else {
        return badShape("screenId is not a string");
    }
    if (auto c = clockFrom(*clock); c) {
        script.clock = *c;
    } else {
        return badShape("pinnedClock is malformed");
    }
    if (const auto* bp = seed->find("beatPeriod"); bp != nullptr) {
        script.sampleSeed.beatPeriod = static_cast<std::uint32_t>(bp->asUInt().value_or(60));
    }
    if (const auto* wf = seed->find("warmupFrames"); wf != nullptr) {
        script.sampleSeed.warmupFrames = static_cast<std::uint32_t>(wf->asUInt().value_or(0));
    }
    for (const json::Value& r : reqs->elements()) {
        if (const auto s = r.asString(); s) {
            script.requirements.emplace_back(*s);
        }
    }
    for (const json::Value& c : caps->elements()) {
        if (const auto s = c.asString(); s) {
            script.captureNames.emplace_back(*s);
        }
    }

    for (const json::Value& stepValue : steps->elements()) {
        const auto* kindV = stepValue.find("kind");
        if (kindV == nullptr || !kindV->asString().has_value()) {
            return badShape("a step has no kind");
        }
        const auto kind = ms::stepKindFromWire(*kindV->asString());
        if (!kind) {
            return badShape("a step has an unknown kind");
        }
        ScriptStep step;
        step.kind = *kind;
        switch (*kind) {
            case ms::StepKind::Advance: {
                const auto* fr = stepValue.find("frames");
                step.frames    = fr != nullptr ? static_cast<std::uint32_t>(fr->asUInt().value_or(1)) : 1;
                break;
            }
            case ms::StepKind::Pointer: {
                const auto* p = stepValue.find("pointer");
                if (p == nullptr) {
                    return badShape("a pointer step has no pointer object");
                }
                const auto trans = p->find("transition");
                const auto x     = p->find("x");
                const auto y     = p->find("y");
                if (trans == nullptr || !trans->asString() || x == nullptr || y == nullptr) {
                    return badShape("a pointer step is malformed");
                }
                const auto pk = ms::pointerKindFromWire(*trans->asString());
                if (!pk) {
                    return badShape("a pointer step has an unknown transition");
                }
                step.pointer.kind = *pk;
                step.pointer.x    = static_cast<mdux::core::Px>(x->asInt().value_or(0));
                step.pointer.y    = static_cast<mdux::core::Px>(y->asInt().value_or(0));
                break;
            }
            case ms::StepKind::Key: {
                const auto* k = stepValue.find("key");
                if (k == nullptr) {
                    return badShape("a key step has no key object");
                }
                const auto trans = k->find("transition");
                const auto code  = k->find("code");
                if (trans == nullptr || !trans->asString() || code == nullptr || !code->asString()) {
                    return badShape("a key step is malformed");
                }
                const auto kk = ms::keyKindFromWire(*trans->asString());
                const auto kc = ms::keyCodeFromWire(*code->asString());
                if (!kk || !kc) {
                    return badShape("a key step has an unknown transition or code");
                }
                step.key.kind = *kk;
                step.key.key  = *kc;
                break;
            }
            case ms::StepKind::Text: {
                const auto* sc = stepValue.find("scalar");
                if (sc == nullptr || !sc->asUInt()) {
                    return badShape("a text step has no scalar");
                }
                step.text.scalar = static_cast<char32_t>(*sc->asUInt());
                break;
            }
            case ms::StepKind::Focus: {
                const auto* fo = stepValue.find("focus");
                if (fo == nullptr) {
                    return badShape("a focus step has no focus object");
                }
                const auto trans = fo->find("transition");
                const auto node  = fo->find("node");
                if (trans == nullptr || !trans->asString() || node == nullptr || !node->asString()) {
                    return badShape("a focus step is malformed");
                }
                const auto fk = ms::focusKindFromWire(*trans->asString());
                if (!fk) {
                    return badShape("a focus step has an unknown transition");
                }
                step.focusKind      = *fk;
                
                step.focusNode      = std::string{*node->asString()};
                break;
            }
            case ms::StepKind::Expect: {
                const auto* ex = stepValue.find("expect");
                if (ex == nullptr) {
                    return badShape("an expect step has no expect object");
                }
                const auto kindStr = ex->find("kind");
                if (kindStr == nullptr || !kindStr->asString()) {
                    return badShape("an expect step has no kind");
                }
                const auto ek = ms::expectKindFromWire(*kindStr->asString());
                if (!ek) {
                    return badShape("an expect step has an unknown kind");
                }
                step.expect.kind = *ek;
                const auto num = [&](const char* key) -> std::int64_t {
                    const auto* v = ex->find(key);
                    return v != nullptr ? v->asInt().value_or(0) : 0;
                };
                const auto str = [&](const char* key) -> std::string {
                    const auto* v = ex->find(key);
                    return (v != nullptr && v->asString()) ? std::string{*v->asString()} : std::string{};
                };
                switch (*ek) {
                    case ms::ExpectKind::Clock:
                        if (const auto* c = ex->find("clock"); c != nullptr) {
                            if (auto cv = clockFrom(*c); cv) {
                                step.expect.clock = *cv;
                            }
                        }
                        break;
                    case ms::ExpectKind::Field: {
                        if (const auto* v = ex->find("value"); v != nullptr) {
                            for (const json::Value& cp : v->elements()) {
                                step.expect.fieldValue.push_back(static_cast<char32_t>(cp.asUInt().value_or(0)));
                            }
                        }
                        if (const auto* c = ex->find("caret"); c != nullptr) {
                            step.expect.fieldHasCaret = true;
                            step.expect.caret         = static_cast<std::uint32_t>(c->asUInt().value_or(0));
                        }
                        break;
                    }
                    case ms::ExpectKind::RefusedEdits:
                        step.expect.count = static_cast<std::uint32_t>(num("count"));
                        break;
                    case ms::ExpectKind::Action:
                        step.expect.nodeId      = str("node");
                        step.expect.event       = ms::systemEventFromWire(str("event")).value_or(ms::SystemEvent::Unspecified);
                        step.expect.requirement = str("requirement");
                        break;
                    case ms::ExpectKind::ButtonSource:
                        step.expect.nodeId = str("node");
                        step.expect.source = str("source");
                        break;
                    case ms::ExpectKind::Reading:
                        step.expect.nodeId = str("node");
                        step.expect.value  = num("value");
                        break;
                    case ms::ExpectKind::State:
                        step.expect.nodeId = str("node");
                        step.expect.count  = static_cast<std::uint32_t>(num("index"));
                        break;
                    case ms::ExpectKind::LatchArmed:
                        step.expect.nodeId = str("node");
                        break;
                    case ms::ExpectKind::FrameStat:
                        step.expect.statField = ms::frameStatFieldFromWire(str("field")).value_or(ms::FrameStatField::Unspecified);
                        step.expect.count     = static_cast<std::uint32_t>(num("count"));
                        break;
                    case ms::ExpectKind::Overflow:
                        if (const auto* v = ex->find("value"); v != nullptr) {
                            step.expect.flag = v->asBool().value_or(false);
                        }
                        break;
                    case ms::ExpectKind::Unspecified:
                        return badShape("an expect step has no kind");
                }
                break;
            }
            case ms::StepKind::Capture: {
                const auto* name = stepValue.find("name");
                if (name == nullptr || !name->asString()) {
                    return badShape("a capture step has no name");
                }
                step.capture = std::string{*name->asString()};
                break;
            }
        }
        script.steps.push_back(std::move(step));
    }

    return script;
}

// ---------------------------------------------------------------------------
// run()
// ---------------------------------------------------------------------------

std::optional<BakeOutputs> run(const Recipe& recipe, std::string_view recipePath,
                               std::span<const std::byte> recipeBytes, const std::filesystem::path& root,
                               std::vector<cli::Diagnostic>& diagnostics) {
    const std::filesystem::path scriptFile = root / recipe.source;
    auto scriptBytes = readFile(scriptFile);
    if (!scriptBytes.has_value()) {
        report(diagnostics, recipe.source, 0, sourceUnreadable, "cannot read the .scenario source",
               "Check the recipe's `source` path.");
        return std::nullopt;
    }

    const std::filesystem::path screenPackage = root / "generated" / "screen" / recipe.screen / "package.json";
    const std::string           screenRelative =
        (std::filesystem::path{"generated"} / "screen" / recipe.screen / "package.json").generic_string();
    auto screenBytes = readFile(screenPackage);
    if (!screenBytes.has_value()) {
        report(diagnostics, screenRelative, 0, screenUnreadable, "cannot read the screen package",
               "Run `cmake --build <dir> --target mdux-bake-update` to stage the screen it names.");
        return std::nullopt;
    }
    auto nodes = readScreenNodes(screenPackage, diagnostics);
    if (!nodes.has_value()) {
        return std::nullopt;
    }

    const std::string_view scriptText{reinterpret_cast<const char*>(scriptBytes->data()), scriptBytes->size()};
    auto script = parseScript(scriptText, recipe.source, recipe.screen, *nodes, diagnostics);
    if (!script.has_value()) {
        return std::nullopt;
    }
    if (script->id != recipe.id) {
        report(diagnostics, recipe.source, 0, screenMalformed,
               std::format("the script's `scenario` id is '{}' but the recipe declares '{}'", script->id, recipe.id),
               "Make the `scenario` line match the recipe's `id` key.");
        return std::nullopt;
    }

    auto scenarioJson = renderScenarioJson(*script);
    if (!scenarioJson.has_value()) {
        report(diagnostics, recipe.source, 0, internalError,
               std::string{"scenario.json did not serialize: "} + std::string{json::describe(scenarioJson.error().code)});
        return std::nullopt;
    }

    BakeOutputs outputs;
    outputs.scenarioJson = std::move(*scenarioJson);
    outputs.scenarioId   = script->id;
    outputs.stepCount    = script->steps.size();
    outputs.captureCount = script->captureNames.size();

    evidence::BakeReport bakeReport;
    bakeReport.tool        = std::string{toolName};
    bakeReport.toolVersion = MDUX_TOOL_VERSION;
    bakeReport.recipe      = fileRecord(std::string{recipePath}, recipeBytes);
    bakeReport.inputs      = {fileRecord(recipe.source, *scriptBytes),
                              fileRecord(screenRelative, *screenBytes)};
    json::Value options = json::Value::emptyObject();
    static_cast<void>(options.set("id", json::Value::string(recipe.id)));
    static_cast<void>(options.set("source", json::Value::string(recipe.source)));
    static_cast<void>(options.set("screen", json::Value::string(recipe.screen)));
    bakeReport.options = std::move(options);
    bakeReport.outputs = {fileRecord("scenario.json", asBytes(outputs.scenarioJson))};

    auto reportText = bakeReport.write();
    if (!reportText.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, internalError,
               std::string{"bake report did not serialize: "} + std::string{evidence::describe(reportText.error())});
        return std::nullopt;
    }
    outputs.reportJson = std::move(*reportText);
    return outputs;
}

// ---------------------------------------------------------------------------
// write() / verify()
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] bool writeBytes(const std::filesystem::path& path, std::span<const std::byte> bytes,
                              std::vector<cli::Diagnostic>& diagnostics) {
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file) {
        report(diagnostics, reportPath(path), 0, outputUnwritable, "cannot open for writing");
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        report(diagnostics, reportPath(path), 0, outputUnwritable, "write failed");
        return false;
    }
    return true;
}

[[nodiscard]] bool compareArtifact(std::string_view label, std::span<const std::byte> produced,
                                   const std::filesystem::path& committedPath, std::vector<cli::Diagnostic>& diagnostics) {
    auto committed = readFile(committedPath);
    if (!committed.has_value()) {
        report(diagnostics, reportPath(committedPath), 0, artifactMissing, std::string{label} + " is missing or unreadable",
               "Run `cmake --build <dir> --target mdux-bake-update` to stage it.");
        return false;
    }
    const std::size_t common = std::min(produced.size(), committed->size());
    for (std::size_t i = 0; i < common; ++i) {
        if (produced[i] != (*committed)[i]) {
            report(diagnostics, reportPath(committedPath), 0, artifactDiffers,
                   std::string{label} + " differs at byte " + std::to_string(i),
                   "Run `cmake --build <dir> --target mdux-bake-update` and review the diff.");
            return false;
        }
    }
    if (produced.size() != committed->size()) {
        report(diagnostics, reportPath(committedPath), 0, artifactDiffers, std::string{label} + " length differs",
               "Run `cmake --build <dir> --target mdux-bake-update` and review the diff.");
        return false;
    }
    return true;
}

}  // namespace

bool write(const BakeOutputs& outputs, const std::filesystem::path& outputDir, std::vector<cli::Diagnostic>& diagnostics) {
    std::error_code code;
    std::filesystem::create_directories(outputDir, code);
    if (code) {
        report(diagnostics, reportPath(outputDir), 0, outputUnwritable, "cannot create output directory: " + code.message());
        return false;
    }
    bool ok = writeBytes(outputDir / "scenario.json", asBytes(outputs.scenarioJson), diagnostics);
    ok = writeBytes(outputDir / "report.json", asBytes(outputs.reportJson), diagnostics) && ok;
    return ok;
}

bool verify(const BakeOutputs& outputs, const std::filesystem::path& scenarioPath, const std::filesystem::path& reportPath,
            std::vector<cli::Diagnostic>& diagnostics) {
    bool ok = compareArtifact("scenario.json", asBytes(outputs.scenarioJson), scenarioPath, diagnostics);
    ok = compareArtifact("report.json", asBytes(outputs.reportJson), reportPath, diagnostics) && ok;
    return ok;
}

}  // namespace mdux::tools::scenario
