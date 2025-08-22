#version 430 core
out vec4 fragColor;
in vec2 fragUV;

uniform sampler2D image;
uniform bool horizontal;
const float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
	vec2 tex_offset = 1.0 / textureSize(image, 0);
	vec3 result = texture(image, fragUV).rgb * weight[0];
	for (int i = 1; i < 5; ++i) {
        if (horizontal) {
            result += texture(image, fragUV + vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
            result += texture(image, fragUV - vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
        } else {
            result += texture(image, fragUV + vec2(0.0, tex_offset.y * i)).rgb * weight[i];
            result += texture(image, fragUV - vec2(0.0, tex_offset.y * i)).rgb * weight[i];
        }
    }
    fragColor = vec4(result, 1.0);
}