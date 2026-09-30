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
| `iChannel1` | `sampler3D` | the LUT (see [Using the LUT](#using-the-lut)) |
| `iLut(color)` | `vec3 → vec3` | `color` through the LUT |
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

## Using the LUT

The plugin applies a LUT (a `.cube` file, uploaded under "LUT") before or after your shader, on its own. To use it inside your shader instead, pick the LUT mode "In the shader (iLut)". Then:

- the plugin doesn't apply the LUT itself, and LUT Mix has no effect: blending is up to you;
- `iLut(color)` returns `color` (0..1 RGB) through the LUT;
- `iChannel1` is the LUT as a 3D texture, if you want to sample it yourself.

In the other modes, `iLut` returns its input unchanged, so a shader that uses it still works.

`lut_split.frag` shows the LUT on one side of a movable line:

```glsl
vec4 video = texture(iChannel0, uv);
fragColor = vec4(uv.x < split ? iLut(video.rgb) : video.rgb, video.a);
```
