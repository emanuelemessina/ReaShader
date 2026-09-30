// Film grain: new noise on every rendered frame (seeded by iFrame, not by project time).

//@param amount 'Amount' 0.1 0 0.5
//@param size 'Grain size' 1 1 8
uniform Params
{
    float amount;
    float size;
};

// pcg3d: a well-mixed integer hash, three random uints from three inputs
uvec3 pcg3d(uvec3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

void main()
{
    vec4 video = texture(iChannel0, uv);
    uvec2 cell = uvec2(uv * iResolution / size); // grains of size x size pixels
    float noise = float(pcg3d(uvec3(cell, uint(iFrame))).x) / 4294967295.0; // 0..1
    fragColor = vec4(clamp(video.rgb + (noise - 0.5) * amount, 0.0, 1.0), video.a);
}
