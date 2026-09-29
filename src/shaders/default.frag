// ReaShader's built-in effect: brightens the video by Video Param.
//
// Available to every shader (declared automatically):
//   in vec2 uv;               0..1 over the frame, (0, 0) = top left
//   out vec4 fragColor;
//   sampler2D iChannel0;      the input video frame
//   vec2 iResolution;         frame size in pixels
//   float iTime;              project time in seconds
//   float iFrameRate;
//   int iFrame;               frames rendered since the shader was loaded
//   float videoParam;         the plugin's Video Param, in [0, 1]
//
// Declare your own sliders (values in [0, 1]) in a uniform block:
//   uniform Params { float amount; vec3 tint; };

void main()
{
    fragColor = texture(iChannel0, uv) + videoParam * vec4(0.5, 0.5, 0.5, 0.0);
}
