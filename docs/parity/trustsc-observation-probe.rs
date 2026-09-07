//! Pinned sibling observation probe for MduX #312; see behavior-matrix.md for reproduction.
//! Calls TrustSC's real parser and verifier; contains no replacement checking algorithm.
//! Deliberately outside the build/CI; issue #314 will implement the executable shared corpus gate.

use trustsc_ui::{
    CompiledScreenPackage, CvCheckKind, GoldenReferenceEntry, LayoutKind, LayoutSpec, Rect,
};
use trustsc_ui_verify::{CheckKind, CheckOutcome, FrameExpectations, FramePixels, verify_frame};

/// Checks the supplied nested-Row fixture and isolated golden outcomes against pinned TrustSC.
fn main() {
    let source = std::fs::read_to_string(std::env::args().nth(1).expect("nested-row fixture path"))
        .expect("read fixture");
    let diagnostics = trustsc_ui_dsl_authoring::parse_medui_source(&source).unwrap_err();
    assert_eq!(diagnostics.len(), 1);
    let diagnostic = &diagnostics[0];
    assert_eq!(diagnostic.code, "MEDUI-E015");
    assert_eq!(diagnostic.line, Some(6));
    assert_eq!(diagnostic.column, None);
    println!("nested-row: MEDUI-E015 line=6 column=absent");

    static GOLDENS: [GoldenReferenceEntry; 1] = [GoldenReferenceEntry {
        node_id: "probe",
        bounds: Rect {
            x: 4,
            y: 4,
            width: 8,
            height: 6,
        },
        text_key: None,
        color_token: None,
        cv_checks: &[CvCheckKind::Bounds, CvCheckKind::ColorHash],
    }];
    // No nodes: isolate the golden checks from independent chrome/text/containment checks.
    let screen = CompiledScreenPackage {
        screen_id: "synthetic-probe",
        layout: LayoutSpec {
            kind: LayoutKind::Vertical,
            spacing: 0,
            padding: 0,
        },
        nodes: &[],
        golden_references: &GOLDENS,
    };
    let background = [10, 10, 10, 255];
    let pixels = background.repeat(16 * 20);
    let frame = FramePixels {
        width: 16,
        height: 20,
        rgba: &pixels,
    };
    let results = verify_frame(&screen, frame, &FrameExpectations::new(background));
    assert_eq!(results.len(), 2);
    assert!(
        results
            .iter()
            .any(|r| r.kind == CheckKind::GoldenBounds && r.outcome == CheckOutcome::Pass)
    );
    assert!(
        results
            .iter()
            .any(|r| r.kind == CheckKind::ColorHash && r.outcome == CheckOutcome::NoBaseline)
    );
    println!("empty golden: GoldenBounds=pass ColorHash=no_baseline");
    println!("These isolated check outcomes are not whole-screen conformance.");
}
