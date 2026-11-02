#version 460
// See render_vsh.glsl: the texture (a white texel for SDL's filled rectangles) times the colour.

layout (binding = 0) uniform sampler2D uTex;

layout (location = 0) in vec4 vColor;
layout (location = 1) in vec2 vUV;

layout (location = 0) out vec4 oColor;

void main()
{
    oColor = texture(uTex, vUV) * vColor;
}
