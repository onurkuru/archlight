varying vec2 v_uv;
varying vec4 v_color;

uniform sampler2D u_tex;

void main() {
    vec4 c = TEX2D(u_tex, v_uv) * v_color;
    if (c.a < 0.01) discard;
    FRAGCOLOR = c;
}
