#version 430 core
out vec4 fragColor;
in vec2 fragUV;
uniform vec2 resolution;
uniform float time;
uniform int frameCount;
uniform sampler2D u_accumulationTex;

// Camera Uniforms
uniform vec3 camPos;
uniform vec3 camFront;
uniform vec3 camRight;
uniform vec3 camUp;
uniform float camFov;

// Skybox Uniforms
uniform samplerCube u_skybox;
uniform bool u_useSkybox;

// Some Constants
#define PI 3.1415926535896932385
#define MAX_OBJECTS 64
const float infinity = 1.0 / 0.0;

// Global Seed for our random functions. 
vec2 seed;

// BVH Traversal Infrastructure
struct BVHNode {
    vec4 minBounds;
    vec4 maxBounds;
    int leftChild;
    int rightChild;
    int pad0;
    int pad1;
};

layout(std430, binding = 3) buffer BVHNodes{
    BVHNode bvhNodes[];
};

layout(std430, binding = 4) buffer PrimitiveIndices{
    int primitiveIndices[];
};

// Helper and utility functions

float rand(vec2 co){
    return fract(sin(dot(co + time, vec2(12.9898, 78.233))) * 43758.5453123);
}

float random_float(float mn, float mx, vec2 seed) {
    return mn + (mx - mn) * rand(seed);
}

vec3 randomUnitVector(vec2 seed) {
    float z = rand(seed) * 2.0 - 1.0;         // z in [-1,1]
    float a = rand(seed.yx + 1.0) * 2.0 * PI; // azimuth angle
    float r = sqrt(1.0 - z*z);
    return vec3(r * cos(a), r * sin(a), z);
}

vec3 random_on_hemisphere(vec3 normal, vec2 seed) {
    vec3 p = randomUnitVector(seed);
    return (dot(p, normal) > 0.0) ? p : -p;
}

vec3 randomInUnitDisk(vec2 co){
    float r = sqrt(rand(co));            // radius
    float theta = rand(co.yx + 1.0) * 2.0 * PI; // angle
    return vec3(r * cos(theta), r * sin(theta), 0.0);
}

bool nearZero(vec3 v) {
    const float s = 1e-8; // tolerance threshold
    return (abs(v.x) < s) && (abs(v.y) < s) && (abs(v.z) < s);
}

float reflectance(float cosine, float refIdx) {
    // Schlick's approximation
    float r0 = (1.0 - refIdx) / (1.0 + refIdx);
    r0 = r0 * r0;
    return r0 + (1.0 - r0) * pow(1.0 - cosine, 5.0);
}

// Ray struct

struct Ray{
    vec3 origin;
    vec3 direction;
};

vec3 rayAt(Ray r, float t){
    return r.origin + t* r.direction;
}

// Material Types
#define MATERIAL_LAMBERTIAN 0
#define MATERIAL_METAL 1
#define MATERIAL_DIELECTRIC 2
#define MATERIAL_EMISSIVE 3

struct Material {
    vec4 albedo;
    int type;
    float emissionStrength;
    float fuzz;
    float refractionIndex;
};

// The Materials SSBO
layout(std430, binding = 0) buffer Materials {
    Material materials[];
};

// Records what was hit and where.
struct HitRecord {
    vec3 p;
    vec3 normal;
    float t;
    bool frontFace;
    Material mat;
};

// Determines behavior of ray after hitting certain materials.
bool scatter(Ray rayIn, HitRecord rec, out vec3 attenuation, out Ray scattered) {
    if (rec.mat.type == MATERIAL_LAMBERTIAN) {
        vec3 scatterDir = random_on_hemisphere(rec.normal, seed);
        if (nearZero(scatterDir)) scatterDir = rec.normal;
        scattered = Ray(rec.p + rec.normal * 0.01, scatterDir);
        attenuation = rec.mat.albedo.rgb;
        return true;
    } 
    else if (rec.mat.type == MATERIAL_METAL) {
        vec3 reflected = reflect(normalize(rayIn.direction), rec.normal);
        scattered = Ray(rec.p, reflected + rec.mat.fuzz * randomUnitVector(seed));
        attenuation = rec.mat.albedo.rgb;
        return dot(scattered.direction, rec.normal) > 0.0;
    }
    else if (rec.mat.type == MATERIAL_DIELECTRIC) {
        float refractionRatio = rec.frontFace ? (1.0 / rec.mat.refractionIndex) : rec.mat.refractionIndex;
        vec3 unitDir = normalize(rayIn.direction);
        float cosTheta = min(dot(-unitDir, rec.normal), 1.0);
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

        bool cannotRefract = refractionRatio * sinTheta > 1.0;
        vec3 direction;
        if (cannotRefract || reflectance(cosTheta, refractionRatio) > rand(seed))
            direction = reflect(unitDir, rec.normal);
        else
            direction = refract(unitDir, rec.normal, refractionRatio);

        scattered = Ray(rec.p, direction);
        attenuation = vec3(1.0); // Glass absorbs nothing here
        return true;
    }
    else if (rec.mat.type == MATERIAL_EMISSIVE) {
        attenuation = rec.mat.albedo.rgb * rec.mat.emissionStrength;
        return false; // Light sources don’t scatter
    }

    return false;
}

void setFaceNormal(inout HitRecord rec, Ray r, vec3 outwardNormal){
    rec.frontFace = dot(r.direction, outwardNormal) < 0.0;
    rec.normal = rec.frontFace ? outwardNormal : -outwardNormal;
}

// Primitives

struct Triangle {
    vec4 pointA; // w is unused for all these vec4's. Only exists for memory alignment
    vec4 pointB;
    vec4 pointC;
    vec4 normalA;
    vec4 normalB;
    vec4 normalC;
    int materialID;
    float pad1, pad2, pad3;  // For memory alignment.
};

// The Triangles SSBO
layout(std430, binding = 1) buffer Triangles{
    Triangle triangles[];
};

struct Sphere {
    vec4 center; // w is unused. Memory alignment
    float radius;
    int materialID;
    int pad1, pad2; // Memory alignment. Unused.
};

// The Spheres SSBO.
layout(std430, binding = 2) buffer Spheres{
    Sphere spheres[];
};

// Types of "hittables"
#define HITTABLE_SPHERE 0
#define HITTABLE_TRIANGLE 1

struct Hittable {
    int type;
    int index; // index into the array of the respective type.
};

Hittable hittables[MAX_OBJECTS*2];
int hittableCount = 0;

// Smooths out sharper edges. (Supposedly)
vec3 averageNormal(Triangle t){
    return normalize(t.normalA.xyz + t.normalB.xyz + t.normalC.xyz);
}

// Maintains surface detail from sharp edges.
vec3 faceNormal(Triangle t) {
    return normalize(cross(t.pointB.xyz - t.pointA.xyz, t.pointC.xyz - t.pointA.xyz));
}

// Detects an intersection with an AABB.
bool rayAABBIntersect(Ray r, vec3 minBounds, vec3 maxBounds, float tMin, float tMax){
    vec3 invDir = 1.0 / r.direction;
    vec3 t0 = (minBounds - r.origin) * invDir;
    vec3 t1 = (maxBounds - r.origin) * invDir;

    vec3 tNear = min(t0, t1);
    vec3 tFar = max(t0, t1);

    float tNearMax = max(max(tNear.x, tNear.y), tNear.z);
    float tFarMin = min(min(tFar.x, tFar.y), tFar.z);

    return tFarMin >= tNearMax && tNearMax <= tMax && tFarMin >= tMin;
}

// Moeller-Trumbore Algorithm
// [https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm]
bool hitTriangle(Triangle triangle, Ray r, float tMin, float tMax, out HitRecord rec) {
    const float EPSILON = 1e-5;
    vec3 edge1 = triangle.pointB.xyz - triangle.pointA.xyz;
    vec3 edge2 = triangle.pointC.xyz - triangle.pointA.xyz;
    vec3 ray_cross_e2 = cross(r.direction, edge2); // perpendicular to both the ray direction and the edge of the triangle.
    float det = dot(edge1, ray_cross_e2); // 0.0 if ray_cross_e2 is perpendicular to edge1, only possible if the ray is parallel to the triangle.
    
    if(abs(det) < EPSILON) return false;

    // Now we need to find t, u, v to satisfy the equation:
    // O + tD = A + u(B-A) + v(C-A)
    // O - A = -tD + u(B-A) + v(C-A)

    // solving for u
    float inv_det = 1.0 / det;
    vec3 s = r.origin - triangle.pointA.xyz;
    float u = inv_det * dot(s, ray_cross_e2);

    if(u < -EPSILON || u > 1.0 + EPSILON) return false;

    // Solve for v
    vec3 s_cross_e1 = cross(s, edge1);
    float v = inv_det * dot(r.direction, s_cross_e1);

    if(v < -EPSILON || (u+v) > 1.0 + EPSILON) return false;

    // Solve for t
    float t = inv_det * dot(edge2, s_cross_e1);

    if(t > EPSILON){
        if(t < tMin || t > tMax) return false;
        rec.t = t;
        rec.p = r.origin + r.direction * t;
        rec.mat = materials[triangle.materialID];
        vec3 computedNormal = averageNormal(triangle);

        if(rec.mat.type == MATERIAL_DIELECTRIC) {
            rec.frontFace = true;
            rec.normal = dot(computedNormal, r.direction) < 0.0 ? computedNormal : -computedNormal;
        }else{
            rec.frontFace = dot(r.direction, computedNormal) < 0.0;
            rec.normal = rec.frontFace ? computedNormal : -computedNormal;
        }

        return true;
    }
    return false;
}

// Sphere intersection algorithm.
bool hitSphere(Sphere sphere, Ray r, float tMin, float tMax, out HitRecord rec){
    vec3 oc = r.origin - sphere.center.xyz;
    float a = dot(r.direction, r.direction);
    float half_b = dot(oc, r.direction);
    float c = dot(oc, oc) - sphere.radius * sphere.radius;
    float discriminant = half_b * half_b - a * c;

    if (discriminant < 0.0) return false;
    float sqrtd = sqrt(discriminant);

    float root = (-half_b - sqrtd) / a;
    if (root < tMin || root > tMax){
        root = (-half_b + sqrtd) / a;
        if (root < tMin || root > tMax) return false;
    }

    rec.t = root;
    rec.p = r.origin + root * r.direction;
    rec.mat = materials[sphere.materialID];
    vec3 outwardNormal = (rec.p - sphere.center.xyz) / sphere.radius;
    setFaceNormal(rec, r, outwardNormal);

    return true;
}

// Stack based approach to the traditional recursive search througha BVH.
bool hitWorldBVH(Ray r, float tMin, float tMax, out HitRecord rec){
    if(bvhNodes.length() == 0) return false;

    HitRecord tempRec;
    bool hitAnything = false;
    float closestSoFar = tMax;

    int stack[32];
    int stackPtr = 0;
    stack[stackPtr++] = 0;

    while(stackPtr > 0) {
        int nodeIndex = stack[--stackPtr];
        if (nodeIndex < 0 || nodeIndex >= bvhNodes.length()) continue;

        BVHNode node = bvhNodes[nodeIndex];

        if(!rayAABBIntersect(r, node.minBounds.xyz, node.maxBounds.xyz, tMin, closestSoFar))
            continue;


        // Is a leaf node?
        if(node.leftChild < 0) {
            // Test against Primitives
            int primStart = -node.leftChild - 1; // Convert the negative offset to positive
            int primCount = node.rightChild;

            for(int i = 0; i < primCount; ++i){
                int primIndex = primStart + i;
                if(primIndex >= primitiveIndices.length()) break;

                int triangleIndex = primitiveIndices[primIndex];
                if(triangleIndex >= triangles.length()) continue;

                Triangle tri = triangles[triangleIndex];
                if(hitTriangle(tri, r, tMin, closestSoFar, tempRec)){
                    hitAnything = true;
                    closestSoFar = tempRec.t;
                    rec = tempRec;
                }
            }
        }else{
            // Add children to stack
            if(stackPtr < 30) { // prevent stack overflow
                stack[stackPtr++] = node.leftChild;
                stack[stackPtr++] = node.rightChild;
            }
        }
    }

    return hitAnything;
}

// In case something breaks in bvh transfer - the original, slow approach.
bool hitWorldBruteForce(Ray r, float tMin, float tMax, out HitRecord rec) {
    HitRecord tempRec;
    bool hit_anything = false;
    float closest_so_far = tMax;

    // Loops though all hittables and checks for intersection based on type.
    for(int i = 0; i < hittableCount; i++){
        Hittable h = hittables[i];
        bool hit = false;

        if(h.type == HITTABLE_SPHERE) {
            Sphere s = spheres[h.index];
            hit = hitSphere(s, r, tMin, closest_so_far, tempRec);
        }
        else if(h.type == HITTABLE_TRIANGLE) {
            Triangle t = triangles[h.index];
            hit = hitTriangle(t, r, tMin, closest_so_far, tempRec);
        }

        if(hit) {
            hit_anything = true;
            closest_so_far = tempRec.t;
            rec = tempRec;
        }
    }
    
    return hit_anything;
}

// Essentially a helper function to perform the BVH search and the sphere intersection search simultaneously.
bool hitWorld(Ray r, float tMin, float tMax, out HitRecord rec){
    // Use BVH if available, otherwise fall back to brute force
    if (bvhNodes.length() > 0) {
        bool bvhHit = hitWorldBVH(r, tMin, tMax, rec);
        
        // Still test spheres with brute force (or build separate BVH for them)
        HitRecord tempRec;
        bool sphereHit = false;
        float closest_so_far = bvhHit ? rec.t : tMax;
        
        for (int i = 0; i < spheres.length(); i++) {
            if (hitSphere(spheres[i], r, tMin, closest_so_far, tempRec)) {
                sphereHit = true;
                closest_so_far = tempRec.t;
                rec = tempRec;
            }
        }
        
        return bvhHit || sphereHit;
    } else {
        // Fallback to original brute force method
        return hitWorldBruteForce(r, tMin, tMax, rec);
    }
}

// Gamma adjustment. Not sure if I love this, might revert back to normal.
vec3 linearToGamma(vec3 linear){
    vec3 gamma = vec3(0.0,0.0,0.0);
    if (linear.r > 0) gamma.r = sqrt(linear.r);
    if (linear.g > 0) gamma.g = sqrt(linear.g);
    if (linear.b > 0) gamma.b = sqrt(linear.b);

    return gamma;
}

// Sample from the skybox.
vec3 GainSkyBoxLight(Ray ray) {
    if(u_useSkybox){
        vec3 skyColor = texture(u_skybox, ray.direction).rgb;
        return skyColor;
    }else{
        vec3 unitDir = normalize(ray.direction);
        float t = 0.5 * (unitDir.y + 1.0); // blend factor
        return mix(vec3(0.0), vec3(0.5, 0.7, 1.0) * 0.5, t);
    }
}

// The real driver function of the whole algorithm.
// This function decides the color of each ray shot out by the camera.
// Starting as pure white light, each scatter modulates the ray by the
// scattering object's albedo, until it reaches the skybox, or has 
// bounced enough to lose all color,
vec3 rayColor(Ray r, int maxBounces, vec2 fragCoord){
    vec3 accumulatedColor = vec3(1.0); // keeps track of throughput
    vec3 brightnessScore = vec3(0.0);
    vec3 finalColor = vec3(0.0);

    for (int bounce = 0; bounce < maxBounces; ++bounce) {
        HitRecord rec;
        if (hitWorld(r, 0.01, infinity, rec)) {
            // Scatter ray in random hemisphere
            seed = fragCoord + vec2(frameCount, time);

            vec3 attenuation;
            Ray scattered;

            // scatter returns true if the ray continues, false if it hits light
            bool didScatter = scatter(r, rec, attenuation, scattered);

            // accumulate emitted light
            brightnessScore += rec.mat.albedo.rgb * rec.mat.emissionStrength * accumulatedColor;

            if (!didScatter) {
                break; // material is emissive; terminate ray
            }

            // update throughput and ray
            accumulatedColor *= attenuation;
            r = scattered;

        } else {
            brightnessScore += accumulatedColor * GainSkyBoxLight(r);
            break;
        }
    }

    // gamma correction.
    return linearToGamma(brightnessScore);
}

// This is for antialiasing.
vec2 getJitteredUV(vec2 fragCoord, vec2 resolution, vec2 seed) {
    // random offset in [-0.5, 0.5] per pixel
    vec2 jitter = vec2(rand(seed), rand(seed.yx)) - 0.5;
    return (fragCoord + jitter) / resolution;
}

// Determines the direction of the ray at the current fragment, based on camera parameters.
Ray getRay(vec2 fragCoord){
    float aspectRatio = resolution.x / resolution.y;

    // jittered pixel coordinates
    vec2 seed = fragCoord + vec2(frameCount, time);
    vec2 jitteredUV = getJitteredUV(fragCoord, resolution, seed);

    // convert to NDC [-1,1]
    vec2 ndc = jitteredUV * 2.0 - 1.0;
    ndc.x *= aspectRatio;


    float fovScale = tan(radians(camFov) * 0.5);
    
    vec3 rayDir = normalize(
        camFront + 
        ndc.x * fovScale * camRight +
        ndc.y * fovScale * camUp
    );

    Ray r;
    r.origin = camPos;
    r.direction = rayDir;
    return r;
}

// Populates the hittable array with the Spheres and Triangles passed into the shader.
void loadHittables(){
    hittableCount = 0;
    int s_length = spheres.length();
    int t_length = triangles.length();
    for(int i = 0; i < s_length; i++){
        hittables[hittableCount].type = HITTABLE_SPHERE;
        hittables[hittableCount].index = i;
        hittableCount++;
    }
    for(int i = 0 ; i < t_length; i++){
        hittables[hittableCount].type = HITTABLE_TRIANGLE;
        hittables[hittableCount].index = i;
        hittableCount++;
    }
}

void main() {
    vec2 uv = gl_FragCoord.xy / resolution;
    
    loadHittables();

    Ray r = getRay(gl_FragCoord.xy);
    
    vec3 newSample = rayColor(r, 50, gl_FragCoord.xy);
    vec3 accumulated = texture(u_accumulationTex, uv).rgb * float(frameCount - 1);

    accumulated += newSample;
    vec3 color = accumulated / float(frameCount);

    fragColor = vec4(color, 1.0);
}