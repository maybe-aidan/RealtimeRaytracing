#version 430 core
out vec4 fragColor;
in vec2 fragUV;

uniform sampler2D hdrTex;
uniform float threshold;

void main(){
	vec3 color = texture(hdrTex, fragUV).rgb;
	float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722)); // luminance
	if (brightness > threshold)
		fragColor = vec4(color, 1.0);
	else
		fragColor = vec4(0.0);
}