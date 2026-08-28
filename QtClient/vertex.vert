attribute vec4 attr_position;
attribute vec2 attr_uv;

uniform mat4 uni_mat;
varying vec2 out_uv;

void main(void)
{
    out_uv = attr_uv;
    gl_Position = uni_mat * attr_position;
}
