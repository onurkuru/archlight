/* Bright pass: isolate what the city is emitting, not what it is reflecting.
 * Neon is saturated as well as bright, so saturation biases the threshold -
 * this is what stops white concrete from blooming like a sign. */
varying vec2 v_uv;

uniform sampler2D u_tex;
uniform vec4 u_param;   /* x = threshold, y = knee, z = saturation bias */

void main() {
    vec3 c = TEX2D(u_tex, v_uv).rgb;

    float lo  = min(c.r, min(c.g, c.b));
    float hi  = max(c.r, max(c.g, c.b));
    float sat = (hi - lo) / max(hi, 0.0001);

    float thresh = u_param.x - sat * u_param.z;
    float w = clamp((hi - thresh) / max(u_param.y, 0.0001), 0.0, 1.0);

    FRAGCOLOR = vec4(c * w * w, 1.0);
}
