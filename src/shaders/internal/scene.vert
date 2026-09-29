#version 450

// Scene objects: textured meshes, positioned by one matrix each
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

layout(push_constant) uniform Object
{
    mat4 modelViewProjection;
};

layout(location = 0) out vec2 uv;

void main()
{
    gl_Position = modelViewProjection * vec4(position, 1.0);
    uv = texCoord;
}
