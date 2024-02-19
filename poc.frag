#version 450

layout(set = 0, binding = 0) uniform sampler2D smp_movie;

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 frag_colour;

void main() {
  frag_colour = texture(smp_movie, uv);
}
