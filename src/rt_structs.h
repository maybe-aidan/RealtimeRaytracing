#ifndef RT_STRUCTS_H
#define RT_STRUCTS_H

// Structs for passing geometric and material information to the shader

struct Triangle {
    glm::vec4 v0;      // 16 bytes
    glm::vec4 v1;      // 16 bytes  
    glm::vec4 v2;      // 16 bytes
    glm::vec4 n0;      // 16 bytes
    glm::vec4 n1;      // 16 bytes
    glm::vec4 n2;      // 16 bytes
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

struct MeshInstance {
    std::string name;
    size_t firstTri = 0;
    size_t triCount = 0;
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 modelInv = glm::mat4(1.0f);
    int materialID = -1;
    std::vector<Triangle> originalTris;

    // Transform components
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f); // Euler angles in radians
    glm::vec3 scale = glm::vec3(1.0f);

    // Recompute model matrices from components
    void updateModel() {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, rotation.x, glm::vec3(1, 0, 0));
        m = glm::rotate(m, rotation.y, glm::vec3(0, 1, 0));
        m = glm::rotate(m, rotation.z, glm::vec3(0, 0, 1));
        m = glm::scale(m, scale);
        model = m;
        modelInv = glm::inverse(m);
        for (Triangle& tri : originalTris) {
            tri.materialID = materialID;
        }
    }
};

static void ApplyTransform(const std::vector<Triangle>& src,
    std::vector<Triangle>& dst,
    const glm::mat4& M)
{
    dst.resize(src.size());
    const glm::mat3 N = glm::transpose(glm::inverse(glm::mat3(M)));

    auto xformP = [&](const glm::vec3& p) {
        glm::vec4 r = M * glm::vec4(p.x, p.y, p.z, 1.0f);
        return glm::vec4{ (float)r.x, (float)r.y, (float)r.z, 0.0f };
        };
    auto xformN = [&](const glm::vec3& n) {
        glm::vec3 r = glm::normalize(N * glm::vec3(n.x, n.y, n.z));
        return glm::vec4{ (float)r.x, (float)r.y, (float)r.z, 0.0f };
        };

    for (size_t i = 0; i < src.size(); ++i) {
        Triangle t = src[i];
        t.v0 = xformP(t.v0); t.v1 = xformP(t.v1); t.v2 = xformP(t.v2);
        t.n0 = xformN(t.n0); t.n1 = xformN(t.n1); t.n2 = xformN(t.n2);
        dst[i] = t;
    }
}

#endif // RT_STRUCTS_H