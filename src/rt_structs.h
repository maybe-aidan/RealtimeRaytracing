#ifndef RT_STRUCTS_H
#define RT_STRUCTS_H

// Structs for passing geometric and material information to the shader

struct Triangle {
    float v0_x, v0_y, v0_z, v0_w;      // 16 bytes
    float v1_x, v1_y, v1_z, v1_w;      // 16 bytes  
    float v2_x, v2_y, v2_z, v2_w;      // 16 bytes
    float n0_x, n0_y, n0_z, n0_w;      // 16 bytes
    float n1_x, n1_y, n1_z, n1_w;      // 16 bytes
    float n2_x, n2_y, n2_z, n2_w;      // 16 bytes
    int materialID;                     // 4 bytes
    float cx, cy, cz;               // centroid for bvh
    // Total: 112 bytes
};
static_assert(sizeof(Triangle) == 112, "Triangle must be 112 bytes");

struct Sphere {
    float center_x, center_y, center_z, center_w;  // 16 bytes (vec3 + padding)
    float radius;                                   // 4 bytes
    int materialID;                                 // 4 bytes
    int pad1, pad2;                                 // 8 bytes padding
    // Total: 32 bytes
};
static_assert(sizeof(Sphere) == 32, "Sphere must be 32 bytes");

enum MaterialType {
    Lambertian,
    Metal,
    Dielectric,
    Emissive
};

struct Material {
    float albedo_x, albedo_y, albedo_z, albedo_w;  // 16 bytes (vec3 + padding)
    int type;                                       // 4 bytes
    float emissionStrength;                         // 4 bytes  
    float fuzz;                                     // 4 bytes
    float refractionIndex;                          // 4 bytes
    // Total: 32 bytes
};
static_assert(sizeof(Material) == 32, "Material must be 32 bytes");

struct BVHNode {
    glm::vec4 min;  // 16 bytes
    glm::vec4 max;  // 16 bytes
    int leftChild;           // 4 bytes
    int rightChild;          // 4 bytes
    int pad0, pad1;         // 8 bytes
    // Total: 48 bytes
};
static_assert(sizeof(BVHNode) == 48, "BVHNode must be 48 bytes");


#endif // RT_STRUCTS_H