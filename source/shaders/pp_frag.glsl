#version 450
#extension GL_KHR_vulkan_glsl : enable // MUST

layout(location = 0) in vec3 fragColor;
layout (location = 1) in vec2 texCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D sampledFrame;

layout( push_constant ) uniform constants
{
	int objectId;
	float videoParam;
} pushConstants;

void main()
{
	//outColor = vec4(fragColor + sceneData.ambientColor.xyz, 1.0);
	//outColor = vec4(texCoord.x,texCoord.y,0.5f,1.0f);
	vec4 color = texture(sampledFrame,texCoord).xyzw;
	outColor = color+pushConstants.videoParam*vec4(0.5f,0.5f,0.5f, 0);
}