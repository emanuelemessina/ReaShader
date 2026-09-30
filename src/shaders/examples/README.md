# ReaShader example shaders

Upload any of these from the plugin window, with the folder button next to "Add a shader". The plugin compiles the shader once and keeps the compiled version, so after that it appears in the shader list, and it's added to the chain.

## Writing a shader

A shader is a GLSL fragment shader that writes only `main()`. It runs once per pixel of the video frame and writes that pixel's color.

Every shader gets these without declaring them:

| Name | Type | What it is |
|---|---|---|
| `uv` | `vec2` | position in the frame, 0..1, (0, 0) = top left |
| `fragColor` | `vec4` | the output color of this pixel |
| `iChannel0` | `sampler2D` | the input video frame: `texture(iChannel0, uv)` |
| `iChannel1` | `sampler3D` | the shader's LUT (see [Using a LUT](#using-a-lut)) |
| `iLut(color)` | `vec3 → vec3` | `color` through the shader's LUT |
| `iResolution` | `vec2` | frame size in pixels |
| `iTime` | `float` | project time in seconds |
| `iFrameRate` | `float` | frames per second |
| `iFrame` | `int` | frames this shader rendered since it was loaded (see [Time and frames](#time-and-frames)) |

## Time and frames

- **`iTime`** is the project's time. It follows the timeline: seeking, looping or exporting shows the same picture at the same position. Use it for anything that should line up with the project, like `wave.frag`.
- **`iFrame`** counts the frames this shader rendered, from 0 when it was loaded. It isn't tied to the timeline: seeking back doesn't lower it, and REAPER may render the same position more than once (while paused, or when a slider moves). It pauses while the shader is bypassed, and starts over when the shader is swapped. Use it where every frame should just be different, like the noise in `grain.frag`, not for anything that must look the same on every export.

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

## Using a LUT

A LUT (a `.cube` file) can be a step of the chain on its own, before or after your shader, blended by its Mix. To use one inside your shader instead, pick it in the shader card's "LUT (iLut)" drop-down. Then:

- `iLut(color)` returns `color` (0..1 RGB) through that LUT, and blending is up to you;
- `iChannel1` is the LUT as a 3D texture, if you want to sample it yourself.

With no LUT picked, `iLut` returns its input unchanged, so a shader that uses it still works.

`lut_split.frag` shows the LUT on one side of a movable line:

```glsl
vec4 video = texture(iChannel0, uv);
fragColor = vec4(uv.x < split ? iLut(video.rgb) : video.rgb, video.a);
```
