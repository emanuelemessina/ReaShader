// Pixelates the video into square blocks.

//@param blockSize 'Block size (px)' 16 1 128
uniform Params
{
    float blockSize;
};

void main()
{
    vec2 block = vec2(max(blockSize, 1.0)) / iResolution;
    vec2 center = (floor(uv / block) + 0.5) * block;
    fragColor = texture(iChannel0, center);
}
