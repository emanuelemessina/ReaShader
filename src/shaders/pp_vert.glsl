#version 460
#extension GL_KHR_vulkan_glsl : enable // MUST

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vTexCoord;

layout(location = 0) out vec3 fragColor;
layout (location = 1) out vec2 texCoord;

void main() {
	gl_Position = vec4(vPosition, 1.0f);
	fragColor = vColor;
	texCoord = vTexCoord;
}