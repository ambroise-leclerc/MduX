/**
 * @file GrammarTests.cpp
 * @brief BDD scenarios for the published `.medui` contract and `--explain` (#263).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine (canonical JSON, stable output)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * #263's own words are what these scenarios are for: "a hand-maintained copy would drift, and a
 * grammar that disagrees with the parser is worse than none". A document nothing checks is exactly
 * the artefact that sentence warns about, so every section is held to something.
 *
 * The derived sections are held to the tables they are read from - components to
 * `componentDictionary()`, diagnostics to `registry()`, theme tokens to `themeColors` - which makes
 * drift impossible rather than detectable. The one written section, `productions`, is held to the
 * **implemented front end**: `medui-grammar-examples-match-the-parser` runs `checkScreen()` over
 * every example the document publishes, so a rule that stops describing the parser fails here.
 *
 * The limit is worth stating rather than leaving to be discovered. What that scenario proves is that
 * every published example behaves as published. It does not prove the EBNF text is a *complete*
 * description of the parser, and no test in this file claims to: a production whose rule quietly
 * omitted an alternative would still pass, because nothing exercises the alternative it omitted.
 */

import std;
import speclab;
import mdux.evidence.json;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.tools.medui.check;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.grammar;
import mdux.tools.medui.lexer;
import mdux.tools.medui.semantic;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace md   = mdux::tools::medui;
namespace cli  = mdux::tools::cli;
namespace json = mdux::evidence::json;

/// The document, built once: every scenario reads the same value a consumer would.
const json::Value& document() {
    static const json::Value built = md::grammar();
    return built;
}

[[nodiscard]] std::string_view stringAt(const json::Value& object, std::string_view key) {
    const json::Value* found = object.find(key);
    if (found == nullptr) {
        return {};
    }
    const auto text = found->asString();
    return text.has_value() ? *text : std::string_view{};
}

[[nodiscard]] std::span<const json::Value> arrayAt(const json::Value& object, std::string_view key) {
    const json::Value* found = object.find(key);
    return found == nullptr ? std::span<const json::Value>{} : found->elements();
}

/// Whether `result` carries a diagnostic with this published code, at any severity.
[[nodiscard]] bool carries(const md::CheckResult& result, std::string_view code) {
    return std::ranges::any_of(result.diagnostics, [code](const cli::Diagnostic& diagnostic) {
        return diagnostic.code == code;
    });
}

[[nodiscard]] std::string codesOf(const md::CheckResult& result) {
    std::string joined;
    for (const cli::Diagnostic& diagnostic : result.diagnostics) {
        joined += joined.empty() ? diagnostic.code : ", " + diagnostic.code;
    }
    return joined.empty() ? "nothing" : joined;
}

}  // namespace

const mdux::spec::Register examplesMatchTheParser{
    "Every example the published grammar carries behaves the way it is published to behave",
    "evidence-unit",
    [] {
        return speclab::Test("medui-grammar-examples-match-the-parser")
            .Given("the productions section, with its accepted and rejected sources", [] {})
            .When("the implemented front end is run over every one of them", [] {})
            .Then("each accepted source checks clean and each rejected one reports the code it names",
                  [] {
                      // This is the clause #263 asks for - "derived from or verified against the
                      // implemented parser, and a test fails if they diverge" - for the one section
                      // that cannot be derived. `checkScreen()` is the same front end
                      // `mdux-medui-check` exposes, so what is verified is the pipeline an agent
                      // actually meets rather than a stage assembled for the occasion.
                      mdux::spec::Checks checks;

                      const std::span<const json::Value> productions = arrayAt(document(), "productions");
                      checks.expect(!productions.empty(), "the document publishes productions at all");

                      std::size_t accepted = 0;
                      std::size_t rejected = 0;
                      for (const json::Value& production : productions) {
                          const std::string_view name = stringAt(production, "name");
                          checks.expect(!name.empty(), "every production is named");
                          checks.expect(!stringAt(production, "rule").empty(), std::format("production '{}' publishes a rule", name));

                          for (const json::Value& source : arrayAt(production, "accepts")) {
                              const auto text = source.asString();
                              if (!text.has_value()) {
                                  checks.expect(false, std::format("production '{}' publishes an accepted source as a string", name));
                                  continue;
                              }
                              const md::CheckResult result = md::checkScreen(*text, std::format("{}.medui", name));
                              checks.expect(result.ok(), std::format("production '{}' accepts its own example, got {}", name, codesOf(result)));
                              ++accepted;
                          }

                          for (const json::Value& rejection : arrayAt(production, "rejects")) {
                              const std::string_view source = stringAt(rejection, "source");
                              const std::string_view code   = stringAt(rejection, "code");
                              checks.expect(!code.empty(), std::format("production '{}' names the code its rejection carries", name));

                              const md::CheckResult result = md::checkScreen(source, std::format("{}.medui", name));
                              checks.expect(!result.ok(), std::format("production '{}' rejects its own counter-example", name));
                              // The code, not merely a failure. A counter-example refused for some
                              // other reason would document one rule and exercise another, which is
                              // the way a grammar of this kind rots without anything going red.
                              checks.expect(carries(result, code), std::format("production '{}' rejects with {}, got {}", name, code, codesOf(result)));
                              ++rejected;
                          }
                      }

                      // Per production rather than in total, which is what the first revision got
                      // wrong: an aggregate count is satisfied by one production carrying every
                      // example and the rest carrying none, and a rule with no example is a rule
                      // this scenario does not check at all.
                      for (const json::Value& production : productions) {
                          const std::size_t examples = arrayAt(production, "accepts").size() + arrayAt(production, "rejects").size();
                          checks.expect(examples > 0, std::format("production '{}' carries at least one example", stringAt(production, "name")));
                      }
                      checks.expect(accepted + rejected > 0, "the document verifies something");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register everyNonterminalIsDefined{
    "No published rule refers to a form the document does not define",
    "evidence-unit",
    [] {
        return speclab::Test("medui-grammar-has-no-dangling-nonterminal")
            .Given("every production's rule text", [] {})
            .When("the names it refers to are extracted and looked up", [] {})
            .Then("each resolves to another production or to a token the lexicon publishes",
                  [] {
                      // A contract a consumer cannot implement from is not a contract. The first
                      // revision published `screen = ... { screen-member } ...` and never defined
                      // `screen-member`, along with five other dangling names - which no test could
                      // have noticed, because every *example* still behaved as published.
                      //
                      // Nonterminals are the unquoted lowercase words; quoted text is a literal and
                      // `(* ... *)` is a comment. `identifier`, `number` and `string` are the three
                      // forms the lexer produces directly, so they resolve against the tokens
                      // section rather than against a production.
                      mdux::spec::Checks checks;

                      std::vector<std::string_view> defined;
                      for (const json::Value& production : arrayAt(document(), "productions")) {
                          defined.push_back(stringAt(production, "name"));
                      }
                      for (const std::string_view lexical : {"identifier", "number", "string"}) {
                          defined.push_back(lexical);
                      }

                      const auto referenced = [](std::string_view rule) {
                          std::vector<std::string> names;
                          std::string              word;
                          bool                     inQuote   = false;
                          bool                     inComment = false;
                          for (std::size_t index = 0; index < rule.size(); ++index) {
                              const char character = rule[index];
                              if (!inQuote && !inComment && character == '(' && index + 1 < rule.size() && rule[index + 1] == '*') {
                                  inComment = true;
                              }
                              if (inComment) {
                                  if (character == ')' && index > 0 && rule[index - 1] == '*') {
                                      inComment = false;
                                  }
                                  continue;
                              }
                              if (character == '"') {
                                  inQuote = !inQuote;
                                  word.clear();
                                  continue;
                              }
                              if (!inQuote && ((character >= 'a' && character <= 'z') || character == '-')) {
                                  word.push_back(character);
                                  continue;
                              }
                              if (!word.empty()) {
                                  names.push_back(word);
                                  word.clear();
                              }
                          }
                          if (!word.empty()) {
                              names.push_back(word);
                          }
                          return names;
                      };

                      std::size_t checked = 0;
                      for (const json::Value& production : arrayAt(document(), "productions")) {
                          const std::string_view name = stringAt(production, "name");
                          const std::string_view rule = stringAt(production, "rule");
                          for (const std::string& reference : referenced(rule)) {
                              // The rule opens by naming itself, which is a definition rather than a
                              // reference - but it resolves either way, so nothing special is needed.
                              checks.expect(std::ranges::find(defined, reference) != defined.end(),
                                            std::format("'{}' refers to '{}', which the document defines", name, reference));
                              ++checked;
                          }
                      }
                      checks.expect(checked > 0, "the extraction found references at all, rather than passing on an empty set");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register everyRejectedCodeIsRegistered{"Every code a published counter-example names is one the registry carries", "evidence-unit", [] {
                                                             return speclab::Test("medui-grammar-rejections-are-registered-codes")
                                                                 .Given("the codes the productions section publishes", [] {})
                                                                 .When("each is looked up in the diagnostics section and the registry", [] {})
                                                                 .Then("both carry it, so no example can name a code nothing publishes",
                                                                       [] {
                                                                           mdux::spec::Checks checks;

                                                                           std::vector<std::string_view> published;
                                                                           for (const json::Value& entry : arrayAt(document(), "diagnostics")) {
                                                                               published.push_back(stringAt(entry, "code"));
                                                                           }

                                                                           for (const json::Value& production : arrayAt(document(), "productions")) {
                                                                               for (const json::Value& rejection : arrayAt(production, "rejects")) {
                                                                                   const std::string_view code = stringAt(rejection, "code");
                                                                                   checks.expect(
                                                                                       std::ranges::find(published, code) != published.end(),
                                                                                       std::format("the document's own diagnostics section carries {}", code));
                                                                               }
                                                                           }
                                                                           checks.raise();
                                                                       })
                                                                 .Execute();
                                                         }};

const mdux::spec::Register derivedSectionsMatchTheirTables{
    "The derived sections are the compiler's own tables rather than a second copy of them",
    "evidence-unit",
    [] {
        return speclab::Test("medui-grammar-is-derived-not-restated")
            .Given("the component dictionary, the diagnostic registry and the governed colour table", [] {})
            .When("each is compared with the section of the document that publishes it", [] {})
            .Then("they agree entry for entry, so adding to a table publishes it without an edit here",
                  [] {
                      mdux::spec::Checks checks;

                      // Components, field for field. A dictionary entry gaining an optional field
                      // moves this document with it; a document that had to be edited by hand is
                      // the drift #263 exists to prevent.
                      const std::span<const json::Value>       components = arrayAt(document(), "components");
                      const std::span<const md::ComponentRule> dictionary = md::componentDictionary();
                      checks.expect(components.size() == dictionary.size(),
                                    std::format("one row per dictionary entry, got {} against {}", components.size(), dictionary.size()));

                      for (const md::ComponentRule& rule : dictionary) {
                          const auto row = std::ranges::find_if(components, [&rule](const json::Value& candidate) {
                              return stringAt(candidate, "name") == rule.name;
                          });
                          if (row == components.end()) {
                              checks.expect(false, std::format("the document publishes component '{}'", rule.name));
                              continue;
                          }
                          const std::span<const json::Value> fields = arrayAt(*row, "fields");
                          checks.expect(fields.size() == rule.fields.size(),
                                        std::format("'{}' publishes {} fields, the dictionary has {}", rule.name, fields.size(), rule.fields.size()));
                          for (const md::FieldRule& field : rule.fields) {
                              const auto entry = std::ranges::find_if(fields, [&field](const json::Value& candidate) {
                                  return stringAt(candidate, "name") == field.name;
                              });
                              if (entry == fields.end()) {
                                  checks.expect(false, std::format("'{}' publishes its field '{}'", rule.name, field.name));
                                  continue;
                              }
                              const json::Value* required = entry->find("required");
                              checks.expect(required != nullptr && required->asBool().value_or(!field.required) == field.required,
                                            std::format("'{}.{}' publishes the requiredness the dictionary gives it", rule.name, field.name));
                          }
                      }

                      // Diagnostics, one row per registered code, in the registry's own order.
                      const std::span<const json::Value>  diagnostics = arrayAt(document(), "diagnostics");
                      const std::span<const md::CodeInfo> registry    = md::registry();
                      checks.expect(diagnostics.size() == registry.size(),
                                    std::format("one row per registered code, got {} against {}", diagnostics.size(), registry.size()));
                      for (std::size_t index = 0; index < registry.size() && index < diagnostics.size(); ++index) {
                          checks.expect(stringAt(diagnostics[index], "code") == registry[index].id, std::format("row {} is {}", index, registry[index].id));
                          checks.expect(stringAt(diagnostics[index], "summary") == registry[index].summary, "and carries the registry's own summary");
                          checks.expect(stringAt(diagnostics[index], "fixHint") == registry[index].fixHint, "and its fix hint");
                      }

                      // Theme tokens, which are what an agent needs to write a colour at all.
                      const std::span<const json::Value> tokens = arrayAt(document(), "themeTokens");
                      checks.expect(tokens.size() == mdux::medui::themeColors.size(),
                                    std::format("one entry per governed colour, got {} against {}", tokens.size(), mdux::medui::themeColors.size()));
                      for (std::size_t index = 0; index < mdux::medui::themeColors.size() && index < tokens.size(); ++index) {
                          checks.expect(tokens[index].asString().value_or("") == mdux::medui::themeColors[index].token,
                                        std::format("entry {} is {}", index, mdux::medui::themeColors[index].token));
                      }

                      // The two closed sets, without their `Unspecified` sentinel, which is the
                      // aggregate default a compiled screen may never carry rather than a spelling.
                      const json::Value* named = document().find("namedValues");
                      checks.expect(named != nullptr, "the document publishes the closed named-value sets");
                      if (named != nullptr) {
                          const std::span<const json::Value> events = arrayAt(*named, "on_press");
                          checks.expect(events.size() == 2, std::format("on_press has two members, got {}", events.size()));
                          checks.expect(std::ranges::none_of(events,
                                                             [](const json::Value& value) {
                                                                 return value.asString().value_or("") == "Unspecified";
                                                             }),
                                        "and the sentinel is not one of them");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register everyTokenAndDomainIsPublished{
    "A token kind or field domain added to the compiler cannot go unpublished",
    "evidence-unit",
    [] {
        return speclab::Test("medui-grammar-lexicon-is-complete")
            .Given("the counts the lexer and the analyzer fix", [] {})
            .When("the tokens and fieldDomains sections are counted", [] {})
            .Then("each matches, and every entry is named and described",
                  [] {
                      // The counts are pinned here rather than derived, deliberately: C++ has no
                      // enumerator range, so the emitter lists its enumerators by hand and this is
                      // what makes that list falsifiable. Adding a `TokenKind` without adding it
                      // there fails here, which is the whole reason the number is written down.
                      mdux::spec::Checks checks;

                      const std::span<const json::Value> tokens = arrayAt(document(), "tokens");
                      checks.expect(tokens.size() == 15, std::format("fifteen token kinds are published, got {}", tokens.size()));
                      for (const json::Value& token : tokens) {
                          checks.expect(!stringAt(token, "name").empty(), "every token publishes its enumerator name");
                          checks.expect(!stringAt(token, "description").empty(), "and the lexer's own description of it");
                      }

                      const std::span<const json::Value> domains = arrayAt(document(), "fieldDomains");
                      checks.expect(domains.size() == 12, std::format("twelve field domains are published, got {}", domains.size()));
                      for (const json::Value& domain : domains) {
                          checks.expect(!stringAt(domain, "name").empty(), "every domain publishes its enumerator name");
                          checks.expect(!stringAt(domain, "form").empty(), "and the written form an author has to produce");
                      }

                      // Every domain a published field names must itself be published, so a consumer
                      // reading `components[].fields[].domain` always has an entry to resolve it
                      // against. Without this the two sections could drift apart while each stayed
                      // internally consistent, which is the drift a reader would meet last.
                      std::vector<std::string_view> names;
                      names.reserve(domains.size());
                      for (const json::Value& domain : domains) {
                          names.push_back(stringAt(domain, "name"));
                      }
                      for (const json::Value& component : arrayAt(document(), "components")) {
                          for (const json::Value& field : arrayAt(component, "fields")) {
                              const std::string_view domain = stringAt(field, "domain");
                              checks.expect(
                                  std::ranges::find(names, domain) != names.end(),
                                  std::format("'{}.{}' names a published domain, got '{}'", stringAt(component, "name"), stringAt(field, "name"), domain));
                          }
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theDocumentIsStableAndPathFree{
    "The document is byte-stable and carries nothing that changes between runs or machines",
    "evidence-unit",
    [] {
        return speclab::Test("medui-grammar-output-is-stable")
            .Given("two emissions from one process", [] {})
            .When("their bytes are compared, and searched for a timestamp or an absolute path", [] {})
            .Then("they are identical and carry neither",
                  [] {
                      // #263's third acceptance, and ADR-007 decision 5's reasoning applied to an
                      // artifact no baker produces: a contract document that changed byte for byte
                      // between two runs of one commit could not be diffed by the agent it exists
                      // to serve.
                      mdux::spec::Checks checks;

                      const std::string first  = md::grammarJson();
                      const std::string second = md::grammarJson();
                      checks.expect(first == second, "two emissions agree byte for byte");
                      checks.expect(first.ends_with("}\n"), "the document ends with the newline a file wants");

                      // The repository root is the one absolute path this process certainly knows,
                      // so it is the one a leak would most likely carry.
                      const std::string root{MDUX_REPO_ROOT};
                      checks.expect(first.find(root) == std::string::npos, "no absolute path from this machine reaches the document");
                      checks.expect(first.find("build-") == std::string::npos, "and no build directory name");

                      // Canonical JSON sorts members, which is what makes a diff of two versions
                      // read as the change rather than as a reordering.
                      checks.expect(first.find("\"components\"") < first.find("\"diagnostics\""), "members are in canonical order");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register explainResolvesEveryRegisteredCode{
    "--explain answers for every registered code and refuses one that is not registered",
    "evidence-unit",
    [] {
        return speclab::Test("medui-explain-resolves-the-registry")
            .Given("every row of the diagnostic registry, and codes no row names", [] {})
            .When("each is passed to explain()", [] {})
            .Then("a registered code resolves with its summary and fix, and an unregistered one resolves to nothing",
                  [] {
                      mdux::spec::Checks checks;

                      for (const md::CodeInfo& entry : md::registry()) {
                          const std::optional<std::string> answer = md::explain(entry.id);
                          if (!answer.has_value()) {
                              checks.expect(false, std::format("{} resolves", entry.id));
                              continue;
                          }
                          checks.expect(answer->contains(entry.id), std::format("{}'s answer names the code", entry.id));
                          checks.expect(answer->contains(entry.summary), std::format("{}'s answer carries the registered summary", entry.id));
                          if (!entry.fixHint.empty()) {
                              checks.expect(answer->contains(entry.fixHint), std::format("{}'s answer carries the registered fix hint", entry.id));
                          }

                          // The one-release alias resolves too, and says which spelling is canonical
                          // so a consumer pinned to the old one can move the pin.
                          const std::string                alias    = md::legacyId(entry.code);
                          const std::optional<std::string> viaAlias = md::explain(alias);
                          checks.expect(viaAlias.has_value(), std::format("the alias {} resolves as well", alias));
                          if (viaAlias.has_value() && alias != entry.id) {
                              checks.expect(viaAlias->contains(entry.id), std::format("and names {} as the canonical spelling", entry.id));
                          }
                      }

                      // The acceptance's other half: nothing, rather than an empty explanation. A
                      // tool that answered every input would tell an agent that MEDUI-E999 is a real
                      // code with nothing to say about it, which is worse than saying it does not
                      // exist.
                      for (const std::string_view unregistered : {"MEDUI-E999", "MEDUI-E", "", "not-a-code", "medui-e030"}) {
                          checks.expect(!md::explain(unregistered).has_value(), std::format("'{}' does not resolve", unregistered));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theCommittedDocumentIsCurrent{
    "The committed grammar is the one this compiler emits",
    "evidence-unit",
    [] {
        return speclab::Test("medui-grammar-committed-copy-is-current")
            .Given("docs/medui/grammar.json as it stands in the tree", [] {})
            .When("the compiler emits the document afresh", [] {})
            .Then("the two are byte-identical",
                  [] {
                      // The published file is the artefact an agent reads without building anything,
                      // so a stale one is worse than none: it looks authoritative and describes a
                      // compiler that no longer exists. This is the same gate `ctest -L evidence`
                      // applies to a baked artifact, applied to a document the compiler emits from
                      // its own tables.
                      //
                      // Regenerate with:  mdux-meduic --grammar > docs/medui/grammar.json
                      mdux::spec::Checks          checks;
                      const std::filesystem::path path = std::filesystem::path{MDUX_REPO_ROOT} / "docs" / "medui" / "grammar.json";

                      std::ifstream file{path, std::ios::binary};
                      if (!file.is_open()) {
                          checks.expect(false, std::format("{} is committed", path.generic_string()));
                          checks.raise();
                          return;
                      }
                      std::ostringstream buffer;
                      buffer << file.rdbuf();

                      const std::string committed = buffer.str();
                      const std::string emitted   = md::grammarJson();
                      checks.expect(committed == emitted,
                                    std::format("the committed document is current; regenerate with "
                                                "`mdux-meduic --grammar > docs/medui/grammar.json` ({} bytes committed, {} emitted)",
                                                committed.size(),
                                                emitted.size()));
                      checks.raise();
                  })
            .Execute();
    }};
