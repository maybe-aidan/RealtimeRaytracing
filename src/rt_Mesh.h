#ifndef RT_MESH_H
#define RT_MESH_H
#include <glad2/gl.h>
#include <glm/glm/glm.hpp>
#include <glm/glm/gtc/matrix_transform.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>

#include "rt_structs.h"

struct MeshData {
    std::vector<glm::vec3> vertices;
    std::vector<glm::vec3> normals;
    std::vector<unsigned int> indices;
};

class rt_Mesh {
public:
    std::vector<MeshData> meshes;
    std::vector<Triangle> triangles;

    rt_Mesh(std::string const& path, int materialID = 0) : defaultMaterialID(materialID) {
        loadMesh(path);
        generateTriangles();
    }

    // Get all triangles for uploading to GPU
    const std::vector<Triangle>& getTriangles() const {
        return triangles;
    }

    // Transform all triangles by a matrix (for positioning/scaling meshes)
    void transform(const glm::mat4& matrix) {
        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(matrix)));

        for (auto& triangle : triangles) {
            // Transform vertices
            glm::vec4 v0 = matrix * glm::vec4(triangle.v0.x, triangle.v0.y, triangle.v0.z, 1.0);
            glm::vec4 v1 = matrix * glm::vec4(triangle.v1.x, triangle.v1.y, triangle.v1.z, 1.0);
            glm::vec4 v2 = matrix * glm::vec4(triangle.v2.x, triangle.v2.y, triangle.v2.z, 1.0);

            triangle.v0 = v0;
            triangle.v1 = v1;
            triangle.v2 = v2;

            // Transform normals
            glm::vec3 n0 = normalMatrix * glm::vec3(triangle.n0.x, triangle.n0.y, triangle.n0.z);
            glm::vec3 n1 = normalMatrix * glm::vec3(triangle.n1.x, triangle.n1.y, triangle.n1.z);
            glm::vec3 n2 = normalMatrix * glm::vec3(triangle.n2.x, triangle.n2.y, triangle.n2.z);

            triangle.n0.x = n0.x; triangle.n0.y = n0.y; triangle.n0.z = n0.z;
            triangle.n1.x = n1.x; triangle.n1.y = n1.y; triangle.n1.z = n1.z;
            triangle.n2.x = n2.x; triangle.n2.y = n2.y; triangle.n2.z = n2.z;
        }
    }

private:
    int defaultMaterialID;

    void loadMesh(std::string const& path) {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_FlipUVs);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
            return;
        }

        processNode(scene->mRootNode, scene);
    }

    void processNode(aiNode* node, const aiScene* scene) {
        // Process each mesh located at the current node
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene));
        }

        // Recursively process child nodes
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            processNode(node->mChildren[i], scene);
        }
    }

    MeshData processMesh(aiMesh* mesh, const aiScene* scene) {
        MeshData meshData;

        // Process vertices and normals
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            glm::vec3 vertex;
            vertex.x = mesh->mVertices[i].x;
            vertex.y = mesh->mVertices[i].y;
            vertex.z = mesh->mVertices[i].z;
            meshData.vertices.push_back(vertex);

            if (mesh->mNormals) {
                glm::vec3 normal;
                normal.x = mesh->mNormals[i].x;
                normal.y = mesh->mNormals[i].y;
                normal.z = mesh->mNormals[i].z;
                meshData.normals.push_back(normal);
            }
        }

        // Process indices
        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                meshData.indices.push_back(face.mIndices[j]);
            }
        }

        return meshData;
    }

    void generateTriangles() {
        for (const auto& mesh : meshes) {
            // Generate triangles from indices
            for (size_t i = 0; i < mesh.indices.size(); i += 3) {
                if (i + 2 < mesh.indices.size()) {
                    unsigned int idx0 = mesh.indices[i];
                    unsigned int idx1 = mesh.indices[i + 1];
                    unsigned int idx2 = mesh.indices[i + 2];

                    if (idx0 < mesh.vertices.size() &&
                        idx1 < mesh.vertices.size() &&
                        idx2 < mesh.vertices.size()) {

                        Triangle tri = {};

                        // Set vertices
                        tri.v0.x = mesh.vertices[idx0].x;
                        tri.v0.y = mesh.vertices[idx0].y;
                        tri.v0.z = mesh.vertices[idx0].z;
                        tri.v0.w = 0.0f;

                        tri.v1.x = mesh.vertices[idx1].x;
                        tri.v1.y = mesh.vertices[idx1].y;
                        tri.v1.z = mesh.vertices[idx1].z;
                        tri.v1.w = 0.0f;

                        tri.v2.x = mesh.vertices[idx2].x;
                        tri.v2.y = mesh.vertices[idx2].y;
                        tri.v2.z = mesh.vertices[idx2].z;
                        tri.v2.w = 0.0f;

                        // Set normals (use mesh normals if available, otherwise compute face normal)
                        if (!mesh.normals.empty()) {
                            tri.n0.x = mesh.normals[idx0].x;
                            tri.n0.y = mesh.normals[idx0].y;
                            tri.n0.z = mesh.normals[idx0].z;
                            tri.n0.w = 0.0f;

                            tri.n1.x = mesh.normals[idx1].x;
                            tri.n1.y = mesh.normals[idx1].y;
                            tri.n1.z = mesh.normals[idx1].z;
                            tri.n1.w = 0.0f;

                            tri.n2.x = mesh.normals[idx2].x;
                            tri.n2.y = mesh.normals[idx2].y;
                            tri.n2.z = mesh.normals[idx2].z;
                            tri.n2.w = 0.0f;
                        }
                        else {
                            // Compute face normal
                            glm::vec3 edge1 = mesh.vertices[idx1] - mesh.vertices[idx0];
                            glm::vec3 edge2 = mesh.vertices[idx2] - mesh.vertices[idx0];
                            glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

                            tri.n0.x = tri.n1.x = tri.n2.x = normal.x;
                            tri.n0.y = tri.n1.y = tri.n2.y = normal.y;
                            tri.n0.z = tri.n1.z = tri.n2.z = normal.z;
                            tri.n0.w = tri.n1.w = tri.n2.w = 0.0f;
                        }

                        tri.materialID = defaultMaterialID;
                        tri.cx = (tri.v0.x + tri.v1.x + tri.v2.x) / 3;
                        tri.cy = (tri.v0.y + tri.v1.y + tri.v2.y) / 3;
                        tri.cz = (tri.v0.z + tri.v1.z + tri.v2.z) / 3;

                        triangles.push_back(tri);
                    }
                }
            }
        }
#ifdef RT_DEBUG
        std::cout << "Generated " << triangles.size() << " triangles from mesh" << std::endl;
#endif
    }
};

#endif