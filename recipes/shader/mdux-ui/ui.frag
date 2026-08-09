// MduX UI fragment shader.
//
// Three modes on one pipeline, selected per-vertex rather than by binding a different pipeline:
//
//   0  solid         the vertex colour, untextured - rectangles, rules, backgrounds
//   1  coverageR8    the vertex colour masked by an R8 coverage atlas - glyphs (issue #14)
//   2  sampledRgba   the vertex colour modulating an RGBA atlas - images (issue #17)
//
// One pipeline for all three is what makes a fixed-budget renderer possible: a frame is one
// bind and one draw per budget, with no pipeline switching whose cost depends on what the screen
// happens to contain. The branch is uniform across a primitive because `mode` is flat-qualified.
//
// The atlas is bound even for a draw that is entirely solid. A descriptor set that changed shape
// with the content would put a conditional into the renderer's hot path and into its budget,
// which is the opposite of fixed.
#version 450

layout(set = 0, binding = 0) uniform sampler2D uAtlas;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUv;
layout(location = 2) flat in uint fragMode;

layout(location = 0) out vec4 outColor;

const uint modeSolid = 0u;
const uint modeCoverageR8 = 1u;

void main() {
    if (fragMode == modeSolid) {
        outColor = fragColor;
    } else if (fragMode == modeCoverageR8) {
        // Coverage modulates alpha only, so a glyph takes its colour from the vertex and its
        // shape from the atlas. Storing coverage in R8 rather than RGBA8 is a four-fold saving
        // on the atlas, which on a device is the difference that matters.
        outColor = vec4(fragColor.rgb, fragColor.a * texture(uAtlas, fragUv).r);
    } else {
        outColor = fragColor * texture(uAtlas, fragUv);
    }
}
