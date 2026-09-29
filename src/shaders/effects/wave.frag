// Ripples the video with a moving sine wave (animated by project time).

//@param amplitude 'Amplitude' 0.01 0 0.05
//@param frequency 'Frequency' 20 1 100
//@param speed 'Speed' 2 0 10
uniform Params
{
    float amplitude;
    float frequency;
    float speed;
};

void main()
{
    vec2 offset = vec2(sin(uv.y * frequency + iTime * speed), cos(uv.x * frequency + iTime * speed)) * amplitude;
    fragColor = texture(iChannel0, uv + offset);
}
