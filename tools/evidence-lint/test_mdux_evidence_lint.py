"""Self-tests for mdux-evidence-lint.

A lint that never fires is indistinguishable from no lint at all, so the cases below are
weighted towards proving it *does* fire on each banned construct - and, just as importantly,
that it stays quiet on the prose that explains why the construct is banned.
"""
import tempfile
import unittest
from pathlib import Path

import mdux_evidence_lint as lint


class CheckFileTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def check(self, source, name="Baker.cpp"):
        path = self.root / name
        path.write_text(source, encoding="utf-8")
        findings = []
        lint.check_file(path, self.root, findings)
        return findings

    def codes(self, source, name="Baker.cpp"):
        return sorted(finding.code for finding in self.check(source, name))

    # --- printf-family conversions (EVL001) ---------------------------------

    def test_flags_printf_float_conversions(self):
        for specifier in ("%f", "%e", "%g", "%a", "%F", "%E", "%G", "%A"):
            with self.subTest(specifier=specifier):
                self.assertEqual(
                    ["EVL001"],
                    self.codes(f'void f() {{ std::printf("value {specifier}", x); }}'),
                )

    def test_flags_printf_float_with_precision_and_flags(self):
        for specifier in ("%.9g", "%-12.4f", "%+.3e", "%0*.*f", "%'.2f", "% .1f", "%#.2g"):
            with self.subTest(specifier=specifier):
                self.assertEqual(
                    ["EVL001"],
                    self.codes(f'void f() {{ std::printf("{specifier}", x); }}'),
                )

    def test_ignores_non_float_conversions(self):
        source = 'void f() { std::printf("%d %s %u %zu %x %p %c %%", a, b, c, d, e, g, h); }'
        self.assertEqual([], self.codes(source))

    def test_escaped_percent_before_float_letter_is_not_a_conversion(self):
        # "%%f" is a literal percent followed by the letter f, not a float conversion.
        self.assertEqual([], self.codes('void f() { std::printf("100%%free"); }'))
        self.assertEqual([], self.codes('void f() { std::printf("%%f"); }'))

    # --- std::format presentation types (EVL002) ----------------------------

    def test_flags_format_float_presentation_types(self):
        for field in ("{:f}", "{:e}", "{:g}", "{:a}", "{:.3f}", "{:>10.2e}", "{:+.6g}", "{:E}"):
            with self.subTest(field=field):
                self.assertEqual(
                    ["EVL002"],
                    self.codes(f'void f() {{ auto s = std::format("{field}", x); }}'),
                )

    def test_ignores_format_fields_without_a_float_type(self):
        source = 'void f() { auto s = std::format("{} {:d} {:>8} {:#x} {:s}", a, b, c, d, e); }'
        self.assertEqual([], self.codes(source))

    def test_ignores_plain_braces(self):
        self.assertEqual([], self.codes("struct S { float f; };"))
        self.assertEqual([], self.codes("void f() { if (a) { b(); } }"))

    # --- literals only, not whole lines -------------------------------------

    def test_prose_explaining_the_rule_does_not_fire(self):
        # This is the exact shape of the comment in Json.cppm and ADR-007. Comments are not
        # literals, so the module documenting the rule is never scanned.
        source = """
// printf("%.9g") is not byte-identical across MSVC, glibc and libc++.
/* Nor is std::format("{:.9g}", value) - hence the {"bits": N} encoding. */
void f() { }
"""
        self.assertEqual([], self.codes(source))

    def test_brace_initializer_is_not_a_format_field(self):
        # Regression: an earlier whole-line version of this lint flagged these, and no rewording
        # could have fixed them - they are ordinary C++ that happens to look like "{:...e}".
        for source in (
            "struct S { Mode mode{Mode::Bake}; };",
            "struct S { Format format{Format::Text}; };",
            "auto x = Value{Kind::String};",
            "std::map<int, float> m{{1, 2}};",
        ):
            with self.subTest(source=source):
                self.assertEqual([], self.codes(source))

    def test_modulo_expression_is_not_a_printf_conversion(self):
        # Same class of regression: "a %factor" is a modulo, not a "%f".
        for source in (
            "int r = a %factor;",
            "int r = total %gridWidth;",
            "int r = i %entries;",
        ):
            with self.subTest(source=source):
                self.assertEqual([], self.codes(source))

    def test_violation_inside_a_string_still_fires(self):
        self.assertEqual(["EVL001"], self.codes('const char* k = "%.9g";'))

    def test_double_slash_inside_a_string_is_not_a_comment(self):
        # If "//" inside this literal were treated as a comment start, the real violation later
        # on the line would be missed.
        source = 'void f() { log("http://x"); std::printf("%f", v); }'
        self.assertEqual(["EVL001"], self.codes(source))

    def test_raw_string_literal_contents_are_scanned(self):
        self.assertEqual(["EVL001"], self.codes('const char* k = R"(%.3f)";'))

    def test_line_numbers_survive_comments_and_multi_line_literals(self):
        source = "\n".join(
            [
                "/* line one",
                "   line two",
                "   line three */",
                'void f() { std::printf("%f", v); }',
            ]
        )
        findings = self.check(source)
        self.assertEqual(1, len(findings))
        self.assertEqual(4, findings[0].line)

    def test_line_numbers_survive_a_multi_line_raw_string(self):
        source = "\n".join(
            [
                "const char* doc = R\"(",
                "  line one",
                "  line two",
                ")\";",
                'void f() { std::printf("%g", v); }',
            ]
        )
        findings = self.check(source)
        self.assertEqual(1, len(findings))
        self.assertEqual(5, findings[0].line)

    def test_a_violation_in_a_multi_line_raw_string_reports_its_start_line(self):
        source = "\n".join(
            [
                "void f() {",
                "  const char* fmt = R\"(",
                "    %.3f",
                "  )\";",
                "}",
            ]
        )
        findings = self.check(source)
        self.assertEqual(1, len(findings))
        self.assertEqual(2, findings[0].line)

    # --- suppression --------------------------------------------------------

    def test_suppression_marker_exempts_a_line(self):
        source = 'void f() { std::printf("%.3f", v); }  // mdux-evidence-lint:allow'
        self.assertEqual([], self.codes(source))

    def test_suppression_marker_only_exempts_its_own_line(self):
        source = "\n".join(
            [
                'void a() { std::printf("%.3f", v); }  // mdux-evidence-lint:allow',
                'void b() { std::printf("%.3f", v); }',
            ]
        )
        findings = self.check(source)
        self.assertEqual(1, len(findings))
        self.assertEqual(2, findings[0].line)

    # --- reporting ----------------------------------------------------------

    def test_finding_names_the_offending_text_and_points_at_the_fix(self):
        findings = self.check('void f() { std::printf("%.9g", v); }')
        self.assertEqual(1, len(findings))
        self.assertIn("%.9g", findings[0].message)
        self.assertIn("float32", findings[0].fix_hint)
        self.assertIn("ADR-007", findings[0].fix_hint)
        self.assertEqual("error", findings[0].severity)

    def test_reports_every_violation_on_a_line(self):
        self.assertEqual(
            ["EVL001", "EVL001"], self.codes('void f() { std::printf("%f %e", a, b); }')
        )


class CollectSourcesTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_collects_cpp_sources_recursively_and_skips_other_files(self):
        (self.root / "nested").mkdir()
        for name in ("a.cpp", "b.cppm", "c.hpp", "nested/d.cpp", "notes.md", "data.json"):
            (self.root / name).write_text("", encoding="utf-8")

        collected = {p.name for p in lint.collect_sources([self.root])}
        self.assertEqual({"a.cpp", "b.cppm", "c.hpp", "d.cpp"}, collected)

    def test_accepts_an_explicit_file_path(self):
        target = self.root / "one.cpp"
        target.write_text("", encoding="utf-8")
        self.assertEqual([target], lint.collect_sources([target]))


if __name__ == "__main__":
    unittest.main()
