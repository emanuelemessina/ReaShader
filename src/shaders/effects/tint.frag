// Tints the video towards a color.

//@param amount 'Amount' 0.5 0 1
//@param color 'Color' 1 0 1
uniform Params
{
    float amount;
    vec3 color;
};

void main()
{
    vec4 video = texture(iChannel0, uv);
    float luma = dot(video.rgb, vec3(0.299, 0.587, 0.114));
    fragColor = vec4(mix(video.rgb, luma * color, amount), video.a);
}
