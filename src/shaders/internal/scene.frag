#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

layout(set = 0, binding = 0) uniform sampler2D albedo;

void main()
{
    color = vec4(texture(albedo, uv).rgb + 0.2, 1.0);
}
