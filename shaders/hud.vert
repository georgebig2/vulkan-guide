#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(push_constant) uniform uPushConstant { vec2 uScale; vec2 uTranslate; vec2 uRotate; } pc;

out gl_PerVertex { vec4 gl_Position; };
layout(location = 0) out struct { vec4 Color; vec2 UV; } Out;

void main()
{
    Out.Color = aColor;
    Out.UV = aUV;
    vec2 p = aPos * pc.uScale + pc.uTranslate;
    vec2 pos;
    pos.x = p.x * pc.uRotate.x -  p.y * pc.uRotate.y;
    pos.y = p.x * pc.uRotate.y +  p.y * pc.uRotate.x;
    gl_Position = vec4(pos, 0, 1);
}