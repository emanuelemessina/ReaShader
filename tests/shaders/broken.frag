// Deliberately broken: uploading it must show a compile error for line 7 and keep the previous
// shader running.

void main()
{
    vec4 video = texture(iChannel0, uv);
    fragColor = video * undefinedValue;
}
