/**
 * @file Parser.cpp
 * @brief The `.medui` recursive-descent parser.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 */
module;

module mdux.tools.medui.parser;

import std;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.lexer;

namespace mdux::tools::medui {

namespace {

/// Words that must never appear. Not a style rule - see ADR-011 decision 5 and the module header.
///
/// Matched on identifiers only, so a *field* called `for` would also be rejected. That is intended:
/// the point is that the language has no control flow, and a field named after a construct the
/// language does not have is at best confusing.
constexpr std::array<std::string_view, 11> forbiddenWords{
    "if", "else", "for", "while", "loop", "match", "fn", "let", "return", "import", "while_let",
};

[[nodiscard]] bool isForbidden(std::string_view word) noexcept {
    return std::ranges::find(forbiddenWords, word) != forbiddenWords.end();
}

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string file)
        : tokens_{std::move(tokens)}, file_{std::move(file)} {}

    [[nodiscard]] std::vector<cli::Diagnostic> take() { return std::move(diagnostics_); }

    [[nodiscard]] std::optional<ast::Screen> parseScreen() {
        // Captured before expectWord() consumes it: Ast.cppm says a node carries the position of
        // the token that introduced it, and parseLayout/parseSurface both do this. This one read
        // the position *after* advancing, so it pointed at the screen name.
        const ast::Position screenKeyword = position();
        if (!expectWord("Screen")) {
            return std::nullopt;
        }
        ast::Screen screen;
        screen.position = screenKeyword;

        const Token* name = expect(TokenKind::Identifier, "a screen name");
        if (name == nullptr) {
            return std::nullopt;
        }
        screen.name = name->text;

        if (expect(TokenKind::LBrace, "'{' after the screen name") == nullptr) {
            return std::nullopt;
        }

        while (!at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
            if (!parseScreenMember(screen)) {
                recover();
            }
        }
        static_cast<void>(expect(TokenKind::RBrace, "'}' closing the screen"));

        // A source declares exactly one screen. Without this, `Screen A { } Screen B { }` and
        // `Screen A { } garbage` both parsed clean and returned a screen - the trailing content was
        // simply never looked at. #200's file checker would have reported such a file as valid.
        if (!at(TokenKind::EndOfFile)) {
            report(Code::UnexpectedToken,
                   std::format("unexpected {} after the screen's closing brace",
                               describe(current().kind)),
                   "a .medui source declares exactly one Screen, and nothing follows it");
        }

        checkDuplicateIds(screen);
        return screen;
    }

private:
    // -- token access -------------------------------------------------------

    [[nodiscard]] const Token& current() const noexcept { return tokens_[index_]; }
    [[nodiscard]] bool at(TokenKind kind) const noexcept { return current().kind == kind; }
    [[nodiscard]] ast::Position position() const noexcept {
        return ast::Position{.line = current().line, .column = current().column};
    }

    const Token& advance() noexcept {
        const Token& t = tokens_[index_];
        if (index_ + 1 < tokens_.size()) {
            ++index_;
        }
        return t;
    }

    void report(Code code, std::string message, std::string fixHint = {}) {
        diagnostics_.push_back(
            diagnose(code, file_, current().line, current().column, std::move(message),
                     std::move(fixHint)));
    }

    const Token* expect(TokenKind kind, std::string_view what) {
        if (at(kind)) {
            return &advance();
        }
        report(Code::UnexpectedToken,
               std::format("expected {}, found {}", what, describe(current().kind)));
        return nullptr;
    }

    bool expectWord(std::string_view word) {
        if (at(TokenKind::Identifier) && current().text == word) {
            static_cast<void>(advance());
            return true;
        }
        report(Code::UnexpectedToken,
               std::format("expected '{}', found {}", word, describe(current().kind)));
        return false;
    }

    /// Skips to just past the next `;`, or to a `}` which the caller's loop will see.
    void recover() {
        while (!at(TokenKind::EndOfFile)) {
            if (at(TokenKind::Semicolon)) {
                static_cast<void>(advance());
                return;
            }
            if (at(TokenKind::RBrace)) {
                return;
            }
            static_cast<void>(advance());
        }
    }

    /// Rejects a control-flow word wherever an identifier may appear. Returns true if it fired.
    bool rejectIfForbidden() {
        if (at(TokenKind::Identifier) && isForbidden(current().text)) {
            report(Code::ForbiddenConstruct,
                   std::format("'{}' is not part of the .medui language", current().text));
            return true;
        }
        return false;
    }

    // -- grammar ------------------------------------------------------------

    bool parseScreenMember(ast::Screen& screen) {
        if (rejectIfForbidden()) {
            return false;
        }
        if (at(TokenKind::At)) {
            std::vector<ast::Annotation> annotations;
            while (at(TokenKind::At)) {
                std::optional<ast::Annotation> annotation = parseAnnotation();
                if (!annotation) {
                    return false;
                }
                annotations.push_back(std::move(*annotation));
            }
            return parseNodeInto(screen.nodes, std::move(annotations), /*insideRow=*/false);
        }
        if (!at(TokenKind::Identifier)) {
            report(Code::UnexpectedToken,
                   std::format("expected a component, 'layout' or 'surface', found {}",
                               describe(current().kind)));
            return false;
        }

        const std::string word = current().text;
        if (word == "layout") {
            return parseLayout(screen);
        }
        if (word == "surface") {
            return parseSurface(screen);
        }
        return parseNodeInto(screen.nodes, {}, /*insideRow=*/false);
    }

    bool parseLayout(ast::Screen& screen) {
        screen.layoutPosition = position();
        static_cast<void>(advance());  // 'layout'
        if (expect(TokenKind::Colon, "':' after 'layout'") == nullptr) {
            return false;
        }
        // Before expect(): a forbidden word is an Identifier, so without this `layout: if { }`
        // parsed clean. "Wherever an identifier may appear" has to include this one.
        if (rejectIfForbidden()) {
            return false;
        }
        const Token* kind = expect(TokenKind::Identifier, "a layout kind such as 'Vertical'");
        if (kind == nullptr) {
            return false;
        }
        screen.layoutKind = kind->text;

        if (at(TokenKind::LBrace)) {
            static_cast<void>(advance());
            while (!at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
                std::optional<ast::Field> field = parseField();
                if (!field) {
                    recover();
                    continue;
                }
                screen.layout.push_back(std::move(*field));
            }
            static_cast<void>(expect(TokenKind::RBrace, "'}' closing the layout block"));
        }
        if (at(TokenKind::Semicolon)) {
            static_cast<void>(advance());
        }
        return true;
    }

    bool parseSurface(ast::Screen& screen) {
        const ast::Position where = position();
        static_cast<void>(advance());  // 'surface'
        if (expect(TokenKind::Colon, "':' after 'surface'") == nullptr) {
            return false;
        }
        std::optional<std::int64_t> width = parsePixels();
        if (!width || expect(TokenKind::Comma, "',' between the surface dimensions") == nullptr) {
            return false;
        }
        std::optional<std::int64_t> height = parsePixels();
        if (!height) {
            return false;
        }
        screen.surface = ast::Point{.x = *width, .y = *height, .position = where};
        static_cast<void>(expect(TokenKind::Semicolon, "';' after the surface"));
        return true;
    }

    std::optional<ast::Annotation> parseAnnotation() {
        ast::Annotation annotation;
        annotation.position = position();
        static_cast<void>(advance());  // '@'

        const Token* name = expect(TokenKind::Identifier, "an annotation name");
        if (name == nullptr) {
            return std::nullopt;
        }
        annotation.name = name->text;

        if (at(TokenKind::LParen)) {
            static_cast<void>(advance());
            while (!at(TokenKind::RParen) && !at(TokenKind::EndOfFile)) {
                std::optional<ast::Field> argument = parseField(/*terminated=*/false);
                if (!argument) {
                    return std::nullopt;
                }
                annotation.arguments.push_back(std::move(*argument));
                if (at(TokenKind::Comma)) {
                    static_cast<void>(advance());
                }
            }
            if (expect(TokenKind::RParen, "')' closing the annotation") == nullptr) {
                return std::nullopt;
            }
        }
        return annotation;
    }

    bool parseNodeInto(std::vector<ast::Node>& into, std::vector<ast::Annotation> annotations,
                       bool insideRow) {
        if (rejectIfForbidden()) {
            return false;
        }
        const Token* component = expect(TokenKind::Identifier, "a component name");
        if (component == nullptr) {
            return false;
        }

        ast::Node node;
        node.component = component->text;
        node.position = ast::Position{.line = component->line, .column = component->column};
        node.annotations = std::move(annotations);

        const bool isRow = node.component == "Row";
        if (isRow && insideRow) {
            // ADR-011 decision 5: a solver restriction, not a budget one. The hint says so, and
            // deliberately does not borrow the budget argument, which does not apply here.
            diagnostics_.push_back(diagnose(
                Code::NestedRow, file_, node.position.line, node.position.column,
                "a Row cannot contain another Row"));
        }

        if (expect(TokenKind::LBrace, "'{' after the component name") == nullptr) {
            return false;
        }

        while (!at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
            if (rejectIfForbidden()) {
                recover();
                continue;
            }
            // A nested component inside a Row is `Identifier {`; a field is `Identifier :`.
            if (isRow && at(TokenKind::Identifier) && peekIsBrace()) {
                if (!parseNodeInto(node.children, {}, /*insideRow=*/true)) {
                    recover();
                }
                continue;
            }
            if (at(TokenKind::At)) {
                // Only a Row has children. The unannotated branch above already enforced that;
                // this one did not, so `Card { @x Label { } }` was accepted and - worse -
                // `Card { @x Row { } }` placed a Row at depth two with no MDX-E015, because the
                // annotated path also passed insideRow=false. Reported independently by three
                // reviewers on #209, and the AST contract in Ast.cppm says children is non-empty
                // only for Row, which #194's single-pass solver relies on.
                if (!isRow) {
                    report(Code::UnexpectedToken,
                           std::format("'{}' cannot contain another component", node.component),
                           "only a Row holds child components; move this out, or make the parent "
                           "a Row");
                    recover();
                    continue;
                }
                std::vector<ast::Annotation> childAnnotations;
                bool bad = false;
                while (at(TokenKind::At)) {
                    std::optional<ast::Annotation> annotation = parseAnnotation();
                    if (!annotation) { bad = true; break; }
                    childAnnotations.push_back(std::move(*annotation));
                }
                if (bad || !parseNodeInto(node.children, std::move(childAnnotations),
                                          /*insideRow=*/true)) {
                    recover();
                }
                continue;
            }
            std::optional<ast::Field> field = parseField();
            if (!field) {
                recover();
                continue;
            }
            node.fields.push_back(std::move(*field));
        }
        static_cast<void>(expect(TokenKind::RBrace, "'}' closing the component"));

        into.push_back(std::move(node));
        return true;
    }

    [[nodiscard]] bool peekIsBrace() const noexcept {
        return index_ + 1 < tokens_.size() && tokens_[index_ + 1].kind == TokenKind::LBrace;
    }

    std::optional<ast::Field> parseField(bool terminated = true) {
        if (rejectIfForbidden()) {
            return std::nullopt;
        }
        const Token* name = expect(TokenKind::Identifier, "a field name");
        if (name == nullptr) {
            return std::nullopt;
        }
        ast::Field field;
        field.name = name->text;
        field.namePosition = ast::Position{.line = name->line, .column = name->column};

        if (expect(TokenKind::Colon, "':' after the field name") == nullptr) {
            return std::nullopt;
        }
        std::shared_ptr<ast::Value> value = parseValue();
        if (value == nullptr) {
            return std::nullopt;
        }
        field.value = std::move(value);

        if (terminated) {
            static_cast<void>(expect(TokenKind::Semicolon, "';' after the field"));
        }
        return field;
    }

    std::optional<std::int64_t> parsePixels() {
        const Token* number = expect(TokenKind::Number, "a pixel count");
        if (number == nullptr) {
            return std::nullopt;
        }
        if (!at(TokenKind::Identifier) || current().text != "px") {
            report(Code::UnexpectedToken,
                   std::format("expected 'px' after {}, found {}", number->text,
                               describe(current().kind)),
                   "lengths are written as Npx, e.g. 512px; there are no other units");
            return std::nullopt;
        }
        static_cast<void>(advance());  // 'px'

        std::int64_t pixels = 0;
        const auto [ptr, ec] = std::from_chars(
            number->text.data(), number->text.data() + number->text.size(), pixels);
        if (ec != std::errc{}) {
            report(Code::UnexpectedToken,
                   std::format("'{}' is not a pixel count this compiler can represent",
                               number->text));
            return std::nullopt;
        }
        static_cast<void>(ptr);
        return pixels;
    }

    std::shared_ptr<ast::Value> parseValue() {
        auto value = std::make_shared<ast::Value>();
        value->position = position();

        if (at(TokenKind::String)) {
            value->kind = ast::ValueKind::String;
            value->text = advance().text;
            return value;
        }
        if (at(TokenKind::LBracket)) {
            static_cast<void>(advance());
            value->kind = ast::ValueKind::List;
            while (!at(TokenKind::RBracket) && !at(TokenKind::EndOfFile)) {
                std::shared_ptr<ast::Value> element = parseValue();
                if (element == nullptr) {
                    return nullptr;
                }
                value->list.push_back(std::move(element));
                if (at(TokenKind::Comma)) {
                    static_cast<void>(advance());
                }
            }
            if (expect(TokenKind::RBracket, "']' closing the list") == nullptr) {
                return nullptr;
            }
            return value;
        }
        if (at(TokenKind::Number)) {
            const Token& number = current();
            std::optional<std::int64_t> pixels = parsePixels();
            if (pixels) {
                // `Xpx, Ypx` is a point; a lone `Xpx` is a size. The comma decides.
                if (at(TokenKind::Comma)) {
                    static_cast<void>(advance());
                    std::optional<std::int64_t> y = parsePixels();
                    if (!y) {
                        return nullptr;
                    }
                    value->kind = ast::ValueKind::Point;
                    value->point = ast::Point{.x = *pixels, .y = *y, .position = value->position};
                    return value;
                }
                value->kind = ast::ValueKind::Size;
                value->size = ast::Size{.fill = false, .pixels = *pixels,
                                        .position = value->position};
                return value;
            }
            // parsePixels() already reported. Fall back to a bare number so the AST stays usable.
            value->kind = ast::ValueKind::Number;
            static_cast<void>(std::from_chars(number.text.data(),
                                              number.text.data() + number.text.size(),
                                              value->number));
            return value;
        }
        if (at(TokenKind::Identifier)) {
            if (rejectIfForbidden()) {
                return nullptr;
            }
            const std::string word = current().text;

            if (word == "t" && peekIs(TokenKind::LParen)) {
                static_cast<void>(advance());  // 't'
                static_cast<void>(advance());  // '('
                const Token* key = expect(TokenKind::String, "a text key, as t(\"STR-KEY\")");
                if (key == nullptr || expect(TokenKind::RParen, "')' closing t(") == nullptr) {
                    return nullptr;
                }
                value->kind = ast::ValueKind::TextKey;
                value->text = key->text;
                return value;
            }
            if (word == "Fill") {
                static_cast<void>(advance());
                value->kind = ast::ValueKind::Size;
                value->size = ast::Size{.fill = true, .pixels = 0, .position = value->position};
                return value;
            }

            // A dotted path: `Theme.Colors.ScoreDigits`. Kept whole and unresolved (#193).
            std::string path = advance().text;
            bool dotted = false;
            while (at(TokenKind::Dot)) {
                static_cast<void>(advance());
                const Token* part = expect(TokenKind::Identifier, "a name after '.'");
                if (part == nullptr) {
                    return nullptr;
                }
                path += '.';
                path += part->text;
                dotted = true;
            }
            // A dotted path is only a *colour token* when it is one. `Foo.Bar` and a truncated
            // `Theme.Colors` were both classified as ColorToken, which would have sent #193 to the
            // theme table for a value that never named a colour - and produced MDX-E030 "not in the
            // governed table" for what is really a malformed value.
            const bool isThemeColor = path.starts_with("Theme.Colors.") &&
                                      path.size() > std::string_view{"Theme.Colors."}.size();
            // `dotted` no longer decides anything: an unqualified dotted path and a bare word are
            // both just identifiers for #193 to interpret. Kept as a named variable only for the
            // diagnostic below, so the reader is told which shape was seen.
            value->kind = isThemeColor ? ast::ValueKind::ColorToken : ast::ValueKind::Identifier;
            if (dotted && !isThemeColor) {
                // Not an error here - #193 owns what a field may hold - but worth saying, because
                // a dotted path that is not a theme colour is almost always a mistyped one.
                report(Code::UnexpectedToken,
                       std::format("'{}' is not a Theme.Colors token", path),
                       "colours are written Theme.Colors.<Token>; other dotted paths have no "
                       "meaning in this grammar");
            }
            value->text = std::move(path);
            return value;
        }

        report(Code::UnexpectedToken,
               std::format("expected a value, found {}", describe(current().kind)));
        return nullptr;
    }

    [[nodiscard]] bool peekIs(TokenKind kind) const noexcept {
        return index_ + 1 < tokens_.size() && tokens_[index_ + 1].kind == kind;
    }

    // -- post-parse structural checks ---------------------------------------

    /// Ids must be unique across the whole screen, not merely within a parent: golden references
    /// (#196) and requirement traces address a node by id alone.
    void checkDuplicateIds(const ast::Screen& screen) {
        std::map<std::string, ast::Position> seen;
        const auto visit = [&](auto&& self, const ast::Node& node) -> void {
            for (const ast::Field& field : node.fields) {
                if (field.name != "id" || field.value == nullptr) {
                    continue;
                }
                const std::string& id = field.value->text;
                if (id.empty()) {
                    continue;
                }
                const auto [it, inserted] = seen.emplace(id, field.namePosition);
                if (!inserted) {
                    diagnostics_.push_back(diagnose(
                        Code::DuplicateNodeId, file_, field.namePosition.line,
                        field.namePosition.column,
                        std::format("node id '{}' is already used at line {}, column {}", id,
                                    it->second.line, it->second.column)));
                }
            }
            for (const ast::Node& child : node.children) {
                self(self, child);
            }
        };
        for (const ast::Node& node : screen.nodes) {
            visit(visit, node);
        }
    }

    std::vector<Token> tokens_;
    std::string file_;
    std::size_t index_{0};
    std::vector<cli::Diagnostic> diagnostics_;
};

}  // namespace

ParseResult parse(std::string_view source, std::string file) {
    ParseResult result;

    LexResult lexed = lex(source, file);
    result.diagnostics = std::move(lexed.diagnostics);
    if (lexed.tokens.empty()) {
        return result;  // only when the source was rejected whole, e.g. bad UTF-8
    }

    Parser parser{std::move(lexed.tokens), std::move(file)};
    result.screen = parser.parseScreen();
    std::vector<cli::Diagnostic> parsed = parser.take();
    result.diagnostics.insert(result.diagnostics.end(), std::make_move_iterator(parsed.begin()),
                              std::make_move_iterator(parsed.end()));
    return result;
}

}  // namespace mdux::tools::medui
