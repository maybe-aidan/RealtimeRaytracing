#version 430 core
out vec4 FragColor;

in vec3 worldPos;
uniform sampler2D equirectangularMap;

vec2 SampleSphericalMap(vec3 v) {
    // Normalize the direction vector
    vec3 dir = normalize(v);
    
    // Convert to spherical coordinates
    // theta: horizontal angle (longitude)
    // phi: vertical angle (latitude)
    float theta = atan(dir.x, dir.z);  // Note: atan(y,x) gives correct quadrant
    float phi = acos(dir.y);           // acos gives [0, pi] range
    
    // Convert to texture coordinates [0, 1]
    vec2 uv;
    uv.x = (theta + 3.14159265) / (2.0 * 3.14159265);  // Map [-pi, pi] to [0, 1]
    uv.y = phi / 3.14159265;                            // Map [0, pi] to [0, 1]
    
    return uv;
}

void main() {
    vec2 uv = SampleSphericalMap(worldPos);
    uv.y = 1.0 - uv.y;
    vec3 color = texture(equirectangularMap, uv).rgb;
    FragColor = vec4(color, 1.0);
}