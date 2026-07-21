/* Final pass: scene + bloom, graded, vignetted, scanlined, upscaled to 960x544.
 * Everything cheap that sells the look lives here, in one texture-read budget. */
varying vec2 v_uv;

uniform sampler2D u_tex;    /* scene, 480x272 */
uniform sampler2D u_tex2;   /* bloom, 240x136 */
uniform vec4 u_texel;       /* xy = 1/scene size */
uniform vec4 u_param;       /* x = bloom amount, y = vignette, z = scanline, w = aberration */

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
