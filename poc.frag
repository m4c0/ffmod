#version 450

layout(set = 0, binding = 0) uniform sampler2D smp_movie;

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 frag_colour;

void main() {
  vec4 rgba = texture(smp_movie, uv);
  vec3 rgb = pow(rgba.rgb, vec3(2.2));
  frag_colour = vec4(rgb, 1);
}
