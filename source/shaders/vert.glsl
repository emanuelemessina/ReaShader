#version 460
#extension GL_KHR_vulkan_glsl : enable // MUST

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;

layout(location = 0) out vec3 fragColor;

// uniform buffer
layout(set = 0, binding = 0) uniform  CameraBuffer{
	mat4 view;
	mat4 proj;
	mat4 viewproj;
} cameraData;

//push constants block
layout( push_constant ) uniform constants
{
	vec4 data;
	mat4 render_matrix;
} PushConstants;

struct ObjectData{
	mat4 model;
};

//all object matrices
layout(std140,set = 1, binding = 0) readonly buffer ObjectBuffer{
	ObjectData objects[];
} objectBuffer;

void main() {
	mat4 modelMatrix = objectBuffer.objects[gl_BaseInstance].model;
	mat4 transformMatrix = (cameraData.viewproj * modelMatrix);
	gl_Position = transformMatrix * vec4(vPosition, 1.0f);
	fragColor = vColor;
}