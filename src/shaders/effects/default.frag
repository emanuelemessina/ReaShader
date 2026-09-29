// ReaShader's default effect: brightens the video by Video Param.
//
// Every shader gets these, declared automatically:
//   in vec2 uv;               0..1 over the frame, (0, 0) = top left
//   out vec4 fragColor;
//   sampler2D iChannel0;      the input video frame
//   vec2 iResolution;         frame size in pixels
//   float iTime;              project time in seconds
//   float iFrameRate;
//   int iFrame;               frames rendered since the shader was loaded
//   float videoParam;         the plugin's Video Param, in [0, 1]
//
// Sliders: members of a `uniform Params { ... };` block (float, vec2, vec3, vec4).
// Each can be annotated with its label, default value and range (else 'name', 0.5, 0..1):
//   //@param member 'Label' default min max
// See the other effects in this folder for examples.

void main()
{
    fragColor = texture(iChannel0, uv) + videoParam * vec4(0.5, 0.5, 0.5, 0.0);
}
