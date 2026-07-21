/* Final pass: scene + bloom, graded, vignetted, scanlined, upscaled to 960x544.
 * Everything cheap that sells the look lives here, in one texture-read budget. */
varying vec2 v_uv;

uniform sampler2D u_tex;    /* scene, 480x272 */
uniform sampler2D u_tex2;   /* bloom, 240x136 */
uniform vec4 u_texel;       /* xy = 1/scene size */
uniform vec4 u_param;       /* x = bloom amount, y = vignette, z = scanline, w = aberration */
uniform vec4 u_dir;         /* x = time (s), y = rain intensity */

/* One layer of screen-space rain: slanted columns, per-column phase, most
 * columns empty. Two calls at different scales fake depth. */
float rain(vec2 uv, float t, float cols, float speed, float slant, float len)
{
    vec2 p = vec2(uv.x * cols + uv.y * slant, uv.y);
    float col = floor(p.x);
    float h = fract(sin(col * 127.1) * 43758.5453);
    if (h < 0.72) return 0.0;                    /* most columns carry no drop */

    float y = fract(p.y * 1.5 + t * speed + h * 7.0);
    float streak = smoothstep(1.0 - len, 1.0, y);
    float fx = abs(fract(p.x) - 0.5);
    return streak * (1.0 - smoothstep(0.03, 0.12, fx));
}

void main() {
    vec2 uv = v_uv;
    vec2 off = (uv - 0.5);

    /* Chromatic aberration, radial, sub-pixel at the centre and only visible
       at the frame edge where the eye reads it as a lens, not as an artefact. */
    float ca = u_param.w * dot(off, off);
    vec3 scene;
    scene.r = TEX2D(u_tex, uv - off * ca).r;
    scene.g = TEX2D(u_tex, uv).g;
    scene.b = TEX2D(u_tex, uv + off * ca).b;

    vec3 bloom = TEX2D(u_tex2, uv).rgb;
    vec3 c = scene + bloom * u_param.x;

    /* Rain: a far sheet and a near sheet, tinted with the moonlight. */
    float rf = rain(uv, u_dir.x, 90.0, 0.9, 14.0, 0.55)
             + rain(uv, u_dir.x, 42.0, 1.4, 10.0, 0.40) * 1.6;
    c += vec3(0.45, 0.55, 0.80) * rf * 0.16 * u_dir.y;

    /* Grade: lift the shadows toward the district's ambient hue, cool the
       midtones, keep the highlights where the artist put them. */
    vec3 lift = vec3(0.05, 0.02, 0.10);
    c = c * (1.0 - lift) + lift;
    c = mix(vec3(dot(c, vec3(0.299, 0.587, 0.114))), c, 1.18);   /* saturation */

    /* Vignette */
    float vig = 1.0 - u_param.y * dot(off, off) * 2.2;
    c *= clamp(vig, 0.0, 1.0);

    /* Scanlines, keyed to the 272-line internal buffer so they land on pixel rows */
    float line = sin(v_uv.y / u_texel.y * 3.14159265);
    c *= 1.0 - u_param.z * line * line;

    FRAGCOLOR = vec4(c, 1.0);
}
