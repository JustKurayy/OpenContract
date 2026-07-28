$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
uniform vec4 u_material;

void main()
{
    vec4 color = texture2D(s_texColor, v_texcoord0);
    color.a *= u_material.x;
    if (color.a < u_material.y)
    {
        discard;
    }
    gl_FragColor = color;
}
