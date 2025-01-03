#version 460
#extension GL_KHR_vulkan_glsl : enable // MUST

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vTexCoord;

layout(location = 0) out vec3 fragColor;
layout (location = 1) out vec2 texCoord;

// uniform buffer
layout(set = 0, binding = 0) uniform  CameraBuffer{
	mat4 view;
	mat4 proj;
	mat4 viewproj;
} cameraData;

struct ObjectData{
	mat4 finalModelMatrix;
};

layout( push_constant ) uniform constants
{
	int objectId;
	float videoParam;
} pushConstants;

//all object matrices
layout(std140,set = 1, binding = 0) readonly buffer ObjectBuffer{
	ObjectData objects[];
} objectBuffer;

void main() {
	mat4 modelMatrix = objectBuffer.objects[pushConstants.objectId].finalModelMatrix;
	mat4 transformMatrix = (cameraData.viewproj * modelMatrix);
	gl_Position = transformMatrix * vec4(vPosition, 1.0f);
	fragColor = vColor;
	texCoord = vTexCoord;
}