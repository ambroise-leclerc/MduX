/**
 * @file SchemaTests.cpp
 * @brief BDD scenarios for mdux.text.schema (issue #157).
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping
 *
 * The rejections are the point. A schema whose `validate()` only ever succeeds is a comment
 * claiming there are invariants, so every `SchemaError` blow has a case that produces exactly it -
 * and the round-trip scenario asserts that a package survives `write()` and `parse()` unchanged,
 * which is the property the whole evidence pipeline rests on.
 */

import std;
import speclab;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.text.schema;

#include "../framework/SpecLabBridge.hpp"

namespace {

using namespace mdux::text;
namespace evidence = mdux::evidence;

evidence::Digest digestOf(std::string_view text) {
    return evidence::sha256(std::as_bytes(std::span{text.data(), text.size()}));
}

/// A minimal package that validates: one locale, atlas reference, a 12-byte sidecar with two
/// adjacent runs (one record each, `recordSize` = 6).
TextPackage validPackage() {
    TextPackage package;
    package.header.id = "label-welcome";
    package.header.kind = "text";
    package.atlasId = "roboto-ui";
    package.locale = "en-US";
    package.sidecarPath = "runs.bin";
    package.sidecarByteLength = 12;
    package.sidecarSha256 = digestOf("sidecar");
    package.runs.push_back(TextRun{.id = "title",
                                    .byteOffset = 0,
                                    .byteLength = 6,
                                    .sha256 = digestOf("title-run")});
    package.runs.push_back(TextRun{.id = "subtitle",
                                    .byteOffset = 6,
                                    .byteLength = 6,
                                    .sha256 = digestOf("subtitle-run")});
    return package;
}

/// The error a package validates to, or nullopt when it is valid. Keeps each case to one line.
std::optional<SchemaError> errorOf(const TextPackage& package) {
    auto result = package.validate();
    if (result.has_value()) {
        return std::nullopt;
    }
    return result.error();
}

}  // namespace

// ---------------------------------------------------------------------------
// Reference package and core identity
// ---------------------------------------------------------------------------

const mdux::spec::Register referencePackageValidates{
    "The reference package validates", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-reference-package-validates")
            .Given("the reference package", [state] { state->error = errorOf(validPackage()); })
            .When("it is validated", [] {})
            .Then("no error is reported",
                   [state] {
                        // Guards every rejection below: if this failed, they could all pass for the
                        // wrong reason.
                        mdux::spec::Checks checks;
                        checks.expect(!state->error.has_value(), "the reference package validates");
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register kindAndRecordSizeConstants{
    "The package kind and record size are the published constants", "evidence-unit", [] {
        return speclab::Test("text-published-constants")
            .Given("the published package format", [] {})
            .When("each constant is read", [] {})
            .Then("the values are the published ones",
                   [] {
                        mdux::spec::Checks checks;
                        // These strings and sizes are the published package format; renaming one
                        // silently invalidates every committed artifact that carries it.
                        checks.expect(packageKind == "text", "packageKind = \"text\"");
                        checks.expect(recordSize == 6, "recordSize = 6");
                        checks.expect(recordSizeWire == "6", "recordSizeWire = \"6\"");
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register wrongKindRejected{
    "A package of the wrong kind is rejected", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-wrong-kind-rejected")
            .Given("a package whose kind is not text", [state] {
                TextPackage package = validPackage();
                package.header.kind = "font";
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is rejected as WrongKind",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->error == SchemaError::WrongKind, "WrongKind");
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register unsupportedSchemaVersionRejected{
    "A package with the wrong schemaVersion is rejected", "evidence-unit", [] {
        // Note: the schema rejects this as ReportRejected rather than UnsupportedSchemaVersion,
        // because evidence::PackageHeader::validate() inspects schemaVersion before this module
        // gets to its own UnsupportedSchemaVersion enum. The enumerator stays for diagnostic
        // parity with the shader schema and for the description scenario; it is not a path that
        // TextPackage::validate() reaches, and asserting otherwise would test behaviour the
        // header provides rather than this module.
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-unsupported-schema-version-rejected")
            .Given("a package whose schemaVersion is not the current one", [state] {
                TextPackage package = validPackage();
                package.header.schemaVersion = evidence::kSchemaVersion + 1;
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is rejected (as ReportRejected, via the header)",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->error.has_value(), "some error is reported");
                        checks.expect(state->error == SchemaError::ReportRejected,
                                       "the header rejected it as ReportRejected");
                        checks.raise();
                   })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Atlas and locale
// ---------------------------------------------------------------------------

const mdux::spec::Register atlasIdRequired{
    "An empty atlas id is rejected", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-empty-atlas-rejected")
            .Given("a package with an empty atlas", [state] {
                TextPackage package = validPackage();
                package.atlasId.clear();
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is rejected as EmptyAtlasId",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->error == SchemaError::EmptyAtlasId, "EmptyAtlasId");
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register localeRequired{
    "An empty locale is rejected", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-empty-locale-rejected")
            .Given("a package with an empty locale", [state] {
                TextPackage package = validPackage();
                package.locale.clear();
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is rejected as EmptyLocale",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->error == SchemaError::EmptyLocale, "EmptyLocale");
                        checks.raise();
                   })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Sidecar
// ---------------------------------------------------------------------------

const mdux::spec::Register sidecarPathBareFilename{
    "A sidecar path must be a bare filename", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> empty;
            std::optional<SchemaError> forward;
            std::optional<SchemaError> backward;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-sidecar-path-bare-filename")
            .Given("sidecar paths that are not bare filenames", [state] {
                TextPackage package = validPackage();
                package.sidecarPath.clear();
                state->empty = errorOf(package);

                package = validPackage();
                package.sidecarPath = "nested/runs.bin";
                state->forward = errorOf(package);

                package = validPackage();
                package.sidecarPath = "nested\\runs.bin";
                state->backward = errorOf(package);
            })
            .When("each is validated", [] {})
            .Then("each is rejected with SidecarPathHasSeparator or EmptySidecarPath",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->empty == SchemaError::EmptySidecarPath,
                                       "an empty path is EmptySidecarPath");
                        checks.expect(state->forward == SchemaError::SidecarPathHasSeparator,
                                       "a '/' separator is SidecarPathHasSeparator");
                        checks.expect(state->backward == SchemaError::SidecarPathHasSeparator,
                                       "a '\\' separator is SidecarPathHasSeparator");
                        checks.raise();
                   })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Runs: identity, alignment, bounds, overlap
// ---------------------------------------------------------------------------

const mdux::spec::Register runsIdentityRequired{
    "A run with an empty id is rejected", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-run-id-required")
            .Given("a package with an empty run id", [state] {
                TextPackage package = validPackage();
                package.runs[0].id.clear();
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is rejected as EmptyRunId",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->error == SchemaError::EmptyRunId, "EmptyRunId");
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register duplicateRunIdRejected{
    "Two runs sharing an id are rejected", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-duplicate-run-id-rejected")
            .Given("a package where two runs share an id", [state] {
                TextPackage package = validPackage();
                package.runs[1].id = "title";  // same as runs[0]
                // Make the ranges non-overlapping so only the duplicate id is the failure.
                package.runs[1].byteOffset = 100;
                package.sidecarByteLength = 106;
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is rejected as DuplicateRunId",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->error == SchemaError::DuplicateRunId, "DuplicateRunId");
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register runAlignmentRequired{
    "A run whose byteLength is not a multiple of recordSize is rejected", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-run-alignment-required")
            .Given("a package with a misaligned run length", [state] {
                TextPackage package = validPackage();
                // 7 is not a multiple of 6.
                package.runs[0].byteLength = 7;
                // Keep bounds consistent so only the alignment is the failure.
                package.sidecarByteLength = 13;
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is rejected as UnalignedRun",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->error == SchemaError::UnalignedRun, "UnalignedRun");
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register runOutOfBoundsRejected{
    "A run whose range extends past the sidecar is rejected", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-run-out-of-bounds-rejected")
            .Given("a package with a run past the end of its sidecar", [state] {
                TextPackage package = validPackage();
                // Offset 12 with a 6-byte length, but the sidecar is only 12 bytes long.
                package.runs[1].byteOffset = 12;
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is rejected as RunOutOfBounds",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->error == SchemaError::RunOutOfBounds, "RunOutOfBounds");
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register overlappingRunsRejected{
    "Two overlapping run ranges are rejected", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-overlapping-runs-rejected")
            .Given("a package where two runs share bytes", [state] {
                TextPackage package = validPackage();
                // runs[0] = [0, 6), runs[1] starts at 4 -> overlap.
                package.runs[1].byteOffset = 4;
                package.runs[1].byteLength = 6;
                package.sidecarByteLength = 16;
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("it is rejected as OverlappingRuns",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->error == SchemaError::OverlappingRuns, "OverlappingRuns");
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register emptyRunListAllowed{
    "An empty run list is valid", "evidence-unit", [] {
        // ADR-010's empty-screen case: a text package with zero runs is valid. Enforcing "at least
        // one run" would force every locale to produce a sidecar even for screens with no static
        // text, which is pointless.
        struct State {
            std::optional<SchemaError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-empty-run-list-allowed")
            .Given("a package with no runs and a zero-byte sidecar", [state] {
                TextPackage package = validPackage();
                package.runs.clear();
                package.sidecarByteLength = 0;
                package.sidecarSha256 = digestOf("");
                state->error = errorOf(package);
            })
            .When("it is validated", [] {})
            .Then("no error is reported",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(!state->error.has_value(), "the empty package validates");
                        checks.raise();
                   })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

const mdux::spec::Register packageRoundTrip{
    "A text package survives write() and parse() unchanged", "evidence-unit", [] {
        struct State {
            std::optional<std::string> text;
            std::optional<TextPackage> parsed;
            bool bytesSurvive{false};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-package-round-trip")
            .Given("the reference package", [state] {
                TextPackage package = validPackage();
                auto text = package.write();
                if (!text.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "write() rejected a valid package",
                        std::source_location::current());
                }
                state->text = *text;

                auto parsed = TextPackage::parse(*state->text);
                if (!parsed.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "parse() rejected its own output",
                        std::source_location::current());
                }
                state->parsed = *parsed;

                // Re-writing the parsed package must reproduce the same bytes. This is the
                // property the evidence pipeline's byte comparison depends on; a round trip
                // that merely preserved the fields would not be enough.
                auto rewritten = state->parsed->write();
                if (!rewritten.has_value()) {
                    throw speclab::core::AssertionFailure(
                        "re-writing the parsed package failed",
                        std::source_location::current());
                }
                state->bytesSurvive = (*rewritten == *state->text);
            })
            .When("the parsed package is compared field by field and re-serialized", [] {})
            .Then("every field survives and the bytes are reproduced exactly",
                   [state] {
                        const TextPackage& package = *state->parsed;
                        mdux::spec::Checks checks;
                        checks.expect(package.header.id == "label-welcome", "header id");
                        checks.expect(package.header.kind == "text", "header kind");
                        checks.expect(package.atlasId == "roboto-ui", "atlas id");
                        checks.expect(package.locale == "en-US", "locale");
                        checks.expect(package.sidecarPath == "runs.bin", "sidecar path");
                        checks.expect(package.sidecarByteLength == 12, "sidecar byte length");
                        checks.expect(package.sidecarSha256 == digestOf("sidecar"),
                                       "sidecar digest");
                        if (package.runs.size() != 2) {
                            throw speclab::core::AssertionFailure(
                                std::format("expected 2 runs, got {}", package.runs.size()),
                                std::source_location::current());
                        }
                        for (std::size_t i = 0; i < package.runs.size(); ++i) {
                            checks.expect(package.runs[i].id ==
                                              (i == 0 ? "title" : "subtitle"),
                                           std::format("run {} id", i));
                            checks.expect(package.runs[i].byteOffset == (i == 0 ? 0 : 6),
                                           std::format("run {} offset", i));
                            checks.expect(package.runs[i].byteLength == 6,
                                           std::format("run {} length", i));
                            checks.expect(package.runs[i].sha256 ==
                                              (i == 0 ? digestOf("title-run")
                                                      : digestOf("subtitle-run")),
                                           std::format("run {} digest", i));
                        }
                        checks.expect(state->bytesSurvive,
                                       "re-serializing reproduces the same bytes");
                        checks.raise();
                   })
            .Execute();
    }};

const mdux::spec::Register malformedPackageRejected{
    "Parsing rejects malformed text", "evidence-unit", [] {
        struct State {
            std::optional<SchemaError> notJson;
            std::optional<SchemaError> notAnObject;
            std::optional<SchemaError> noSidecar;
            std::optional<SchemaError> noRunsArray;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-malformed-package-rejected")
            .Given("several malformed package texts", [state] {
                // Not JSON at all.
                state->notJson = TextPackage::parse("not json at all").error();

                // JSON but not an object.
                state->notAnObject = TextPackage::parse("[]").error();

                // An object missing the "sidecar" member.
                state->noSidecar = TextPackage::parse(R"({
                    "schemaVersion": 1, "id": "x", "kind": "text",
                    "atlas": "roboto-ui", "locale": "en-US",
                    "runs": []
                })").error();

                // An object missing the "runs" array.
                state->noRunsArray = TextPackage::parse(R"({
                    "schemaVersion": 1, "id": "x", "kind": "text",
                    "atlas": "roboto-ui", "locale": "en-US",
                    "sidecar": { "path": "runs.bin", "byteLength": 0, "sha256": "0000000000000000000000000000000000000000000000000000000000000000" }
                })").error();
            })
            .When("each is parsed", [] {})
            .Then("each is rejected as MalformedPackage",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->notJson == SchemaError::MalformedPackage, "not-json");
                        checks.expect(state->notAnObject == SchemaError::MalformedPackage, "not-object");
                        checks.expect(state->noSidecar == SchemaError::MalformedPackage, "no-sidecar");
                        checks.expect(state->noRunsArray == SchemaError::MalformedPackage, "no-runs");
                        checks.raise();
                   })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// find()
// ---------------------------------------------------------------------------

const mdux::spec::Register findLocatesRun{
    "find() locates a run by id and returns null for absent ids", "evidence-unit", [] {
        // The package is held in State, not as a Given-local: find() returns a pointer *into*
        // the package, and a Then that reads through that pointer would be a use-after-free if
        // the package were a Given-local. The shader schema's find test happens to read only a
        // trivially-destructible member through a dangling pointer, which is silent UB; this
        // version holds the package so the readthrough is well-defined.
        struct State {
            TextPackage package;
            const TextRun* present{nullptr};
            const TextRun* absent{nullptr};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-find-locates-run")
            .Given("the reference package", [state] {
                state->package = validPackage();
                state->present = state->package.find("title");
                state->absent = state->package.find("missing-id");
            })
            .When("find() is called with a present and an absent id", [] {})
            .Then("the present id yields a pointer and the absent id yields null",
                   [state] {
                        mdux::spec::Checks checks;
                        checks.expect(state->present != nullptr, "present run located");
                        checks.expect(state->present != nullptr && state->present->id == "title",
                                       "present run is the right one");
                        checks.expect(state->absent == nullptr, "absent run is null");
                        checks.raise();
                   })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// SchemaError descriptions
// ---------------------------------------------------------------------------

const mdux::spec::Register schemaErrorDescriptions{
    "Every SchemaError has a non-empty description", "evidence-unit", [] {
        return speclab::Test("text-schema-error-descriptions")
            .Given("every SchemaError enumerator", [] {})
            .When("describe() is called on each", [] {})
            .Then("no description is empty",
                   [] {
                        mdux::spec::Checks checks;
                        const SchemaError all[] = {
                            SchemaError::WrongKind,
                            SchemaError::EmptyAtlasId,
                            SchemaError::EmptyLocale,
                            SchemaError::EmptySidecarPath,
                            SchemaError::SidecarPathHasSeparator,
                            SchemaError::EmptyRunId,
                            SchemaError::DuplicateRunId,
                            SchemaError::UnalignedRun,
                            SchemaError::RunOutOfBounds,
                            SchemaError::OverlappingRuns,
                            SchemaError::UnsupportedSchemaVersion,
                            SchemaError::MalformedPackage,
                            SchemaError::ReportRejected,
                        };
                        for (SchemaError error : all) {
                            checks.expect(!describe(error).empty(),
                                           std::format("describe({}) is non-empty",
                                                        static_cast<unsigned>(error)));
                        }
                        checks.raise();
                   })
            .Execute();
    }};