attribute vec2 a_pos;
attribute vec2 a_uv;
attribute vec4 a_color;

uniform vec4 u_proj;   /* xy = scale, zw = offset */

varying vec2 v_uv;
varying vec4 v_color;

void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = vec4(a_pos * u_proj.xy + u_proj.zw, 0.0, 1.0);
}
