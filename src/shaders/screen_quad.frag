#version 430 core
out vec4 FragColor;
in vec2 fragUV;
uniform sampler2D u_texture;

void main() {
    FragColor = texture(u_texture, fragUV);
}