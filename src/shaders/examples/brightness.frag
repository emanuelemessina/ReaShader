// Brightens the video.

//@param brightness 'Brightness' 0 0 1
uniform Params
{
    float brightness;
};

void main()
{
    fragColor = texture(iChannel0, uv) + brightness * vec4(0.5, 0.5, 0.5, 0.0);
}
