#version 450
#extension GL_KHR_vulkan_glsl : enable // MUST

layout(location = 0) in vec3 fragColor;
layout (location = 1) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform  SceneData{
    vec4 fogColor; // w is for exponent
	vec4 fogDistances; //x for min, y for max, zw unused.
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
} sceneData;

layout(set = 2, binding = 0) uniform sampler2D tex1;

void main()
{
	//outColor = vec4(fragColor + sceneData.ambientColor.xyz, 1.0);
	//outColor = vec4(texCoord.x,texCoord.y,0.5f,1.0f);
	//vec4 color = texture(tex1,texCoord).xyzw;
	vec4 color = vec4(texture(tex1,texCoord).xyz, 1.f);
	outColor = color + vec4(.2f, .2f, .2f, 0.f);
}