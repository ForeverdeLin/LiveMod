attribute vec4 attr_position;//4个顶点位置信息
attribute vec2 attr_uv;//每个顶点的UV

uniform mat4 uni_mat;//MVP矩阵4*4
varying vec2 out_uv;//传给片段注册器

void main(void)
{
    out_uv = attr_uv;
    gl_Position = uni_mat * attr_position;//顶点乘以mvp矩阵后才能从2D到3D空间里,模型变换，视图变换，投影变换
}
