#version 450

// The LUT pass: the frame through a 3D LUT, blended with the original by `amount` (LUT Mix)

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D frame;
layout(set = 0, binding = 1) uniform sampler3D lut;

layout(push_constant) uniform LutInputs
{
    float amount;
};

void main()
{
    vec4 color = texture(frame, uv);
    // texel centers: 0 and 1 land on the LUT's first and last entries
    float size = float(textureSize(lut, 0).x);
    vec3 graded = texture(lut, (clamp(color.rgb, 0.0, 1.0) * (size - 1.0) + 0.5) / size).rgb;
    fragColor = vec4(mix(color.rgb, graded, amount), color.a);
}
