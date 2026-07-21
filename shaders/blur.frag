/* Separable gaussian, 9 taps folded into 5 samples using linear filtering.
 * Run twice: u_dir = (1,0) then (0,1). */
varying vec2 v_uv;

uniform sampler2D u_tex;
uniform vec4 u_texel;   /* xy = 1/size */
uniform vec4 u_dir;     /* xy = axis */

void main() {
    vec2 step = u_dir.xy * u_texel.xy;

    vec3 sum = TEX2D(u_tex, v_uv).rgb * 0.2270270270;
    sum += TEX2D(u_tex, v_uv + step * 1.3846153846).rgb * 0.3162162162;
    sum += TEX2D(u_tex, v_uv - step * 1.3846153846).rgb * 0.3162162162;
    sum += TEX2D(u_tex, v_uv + step * 3.2307692308).rgb * 0.0702702703;
    sum += TEX2D(u_tex, v_uv - step * 3.2307692308).rgb * 0.0702702703;

    FRAGCOLOR = vec4(sum, 1.0);
}
