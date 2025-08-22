#version 430 core
out vec4 fragColor;
in vec2 fragUV;

// final_composite.frag
uniform sampler2D hdrTex;
uniform sampler2D bloomTex;
uniform float exposure;


vec3 linearToGamma(vec3 linear){
    vec3 gamma = vec3(0.0,0.0,0.0);
    if (linear.r > 0) gamma.r = sqrt(linear.r);
    if (linear.g > 0) gamma.g = sqrt(linear.g);
    if (linear.b > 0) gamma.b = sqrt(linear.b);

    return gamma;
}

void main() {
    vec3 hdr = texture(hdrTex, fragUV).rgb;
    vec3 bloom = texture(bloomTex, fragUV).rgb;
    vec3 color = hdr + bloom; // additive blending

    // Tonemapping (e.g., Reinhard)
    color = vec3(1.0) - exp(-color * exposure);

    // Gamma correction
    color = linearToGamma(color);

    fragColor = vec4(color, 1.0);
}