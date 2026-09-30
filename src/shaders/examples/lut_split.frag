// Compares the LUT with the original: graded left of a movable line, untouched right of it.
// Set the LUT mode to "In the shader (iLut)": in the other modes iLut() changes nothing.

//@param split 'Split' 0.5 0 1
uniform Params
{
    float split;
};

void main()
{
    vec4 video = texture(iChannel0, uv);
    vec3 graded = iLut(video.rgb);
    fragColor = vec4(uv.x < split ? graded : video.rgb, video.a);

    // a one-pixel line at the split
    if (abs(uv.x - split) < 1.0 / iResolution.x)
        fragColor.rgb = vec3(1.0);
}
