// ReaShader's default effect: brightens the video.
//
// Every shader gets these, declared automatically:
//   in vec2 uv;               0..1 over the frame, (0, 0) = top left
//   out vec4 fragColor;
//   sampler2D iChannel0;      the input video frame
//   vec2 iResolution;         frame size in pixels
//   float iTime;              project time in seconds
//   float iFrameRate;
//   int iFrame;               frames rendered since the shader was loaded
//
// Sliders: members of a `uniform Params { ... };` block (float, vec2, vec3, vec4).
// Each can be annotated with its label, default value and range (else 'name', 0.5, 0..1):
//   //@param member 'Label' default min max
// Sliders are also host parameters: they can be automated.
// See the other effects in this folder for examples.

//@param brightness 'Brightness' 0 0 1
uniform Params
{
    float brightness;
};

void main()
{
    fragColor = texture(iChannel0, uv) + brightness * vec4(0.5, 0.5, 0.5, 0.0);
}
