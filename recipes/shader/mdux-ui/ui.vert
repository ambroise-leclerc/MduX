// MduX UI vertex shader.
//
// One fixed pipeline contract for every UI draw. The vertex is 24 bytes and its layout is the
// one `mdux.draw`'s UiVertex declares (issue #123):
//
//   offset  0   vec2  position   R32G32_SFLOAT   pixels, origin top-left
//   offset  8   vec2  uv         R32G32_SFLOAT   atlas coordinates, 0..1
//   offset 16   vec4  color      R8G8B8A8_UNORM  4 bytes, normalised in the shader
//   offset 20   uint  mode       R32_UINT        selects the fragment path
//
// Positions arrive in pixels rather than clip space so a caller never has to know the viewport
// size, and the conversion happens once here from a push constant. A governed draw list therefore
// contains no projection maths and no dependency on the surface it will eventually be drawn to.
#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
layout(location = 3) in uint inMode;

layout(push_constant) uniform Push {
    vec2 viewportSize;  // pixels; offset 0, size 8
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUv;
layout(location = 2) flat out uint fragMode;

void main() {
    // Pixels to normalised device coordinates. Y is not flipped: Vulkan's NDC already has +Y
    // downwards, which matches a top-left pixel origin.
    const vec2 ndc = (inPosition / push.viewportSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);

    fragColor = inColor;
    fragUv = inUv;
    // flat, not interpolated: the mode is a per-primitive selector and interpolating it would
    // produce fractional values that match no branch.
    fragMode = inMode;
}
