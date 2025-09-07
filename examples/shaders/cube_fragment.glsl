#version 450

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    // Sample the texture
    vec3 texColor = texture(texSampler, fragTexCoord).rgb;
    
    // Add some subtle shading based on texture coordinates for depth perception
    float shading = 0.8 + 0.2 * (fragTexCoord.x + fragTexCoord.y) * 0.5;
    
    outColor = vec4(texColor * shading, 1.0);
}