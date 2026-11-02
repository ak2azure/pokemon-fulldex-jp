#version 460
// The Vulkan stand-in for SDL's renderer (frontend/vkrender.cpp): SDL's own vertices (a position
// in pixels, a colour, a texture coordinate), placed with the picture's size.

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aUV;

layout (push_constant) uniform Picture
{
    vec2 uScale; // 2 / width, 2 / height
};

layout (location = 0) out vec4 vColor;
layout (location = 1) out vec2 vUV;

void main()
{
    gl_Position = vec4(aPos * uScale - 1.0, 0.0, 1.0);
    vColor = aColor;
    vUV = aUV;
}
