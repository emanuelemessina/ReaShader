# ReaShader example shaders

Upload any of these from the plugin window, under "Shader" → "Upload". The plugin compiles the shader once and keeps the compiled version, so after that it appears in the shader list.

## Writing a shader

A shader is a GLSL fragment shader that writes only `main()`. It runs once per pixel of the video frame and writes that pixel's color.

Every shader gets these without declaring them:

| Name | Type | What it is |
|---|---|---|
| `uv` | `vec2` | position in the frame, 0..1, (0, 0) = top left |
| `fragColor` | `vec4` | the output color of this pixel |
| `iChannel0` | `sampler2D` | the input video frame: `texture(iChannel0, uv)` |
| `iResolution` | `vec2` | frame size in pixels |
| `iTime` | `float` | project time in seconds |
| `iFrameRate` | `float` | frames per second |
| `iFrame` | `int` | frames rendered since the shader was loaded |

## Sliders

Put the values you want to control in a `Params` uniform block. Allowed types are `float`, `vec2`, `vec3` and `vec4`. Each value (or vector component) gets a slider in the plugin window, and is also a host parameter that you can automate in REAPER.

Describe each value with an annotation: its label, default and range.

```glsl
//@param amount 'Amount' 0.5 0 1
uniform Params
{
    float amount;
};
```

The format is `//@param <member> '<Label>' <default> <min> <max>`. The label and the numbers are optional, in that order. Without an annotation a slider is labelled with the member's name, starts at 0.5 and ranges 0..1.
