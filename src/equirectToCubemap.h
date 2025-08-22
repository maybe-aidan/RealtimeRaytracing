#include <glad2/gl.h>
#include <iostream>
#include <vector>
#include <string>

#include <glm/glm/glm.hpp>
#include "includes/shader.h"

class EquirectToCubemap {
public:
    EquirectToCubemap() {
        setupShaders();
        setupGeometry();
        setupFramebuffer();
    }

    ~EquirectToCubemap() {
        glDeleteVertexArrays(1, &cubeVAO);
        glDeleteBuffers(1, &cubeVBO);
        glDeleteFramebuffers(1, &captureFBO);
        glDeleteRenderbuffers(1, &captureRBO);
    }

    GLuint convertToCubemap(GLuint equirectTexture, int cubemapSize = 512) {
        // Create cubemap texture
        GLuint envCubemap;
        glGenTextures(1, &envCubemap);
        glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

        for (unsigned int i = 0; i < 6; ++i) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                cubemapSize, cubemapSize, 0, GL_RGB, GL_FLOAT, nullptr);
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glm::mat4 projection(
            glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), 
            glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
            glm::vec4(0.0f, 0.0f, -1.002f, -1.0f),
            glm::vec4(0.0f, 0.0f, -0.2002f, 0.0f));

        // View matrices for each cubemap face
        std::vector<glm::mat4> viewMatrices = {
            // +X (Right face)
            { 0.0f, 0.0f,-1.0f, 0.0f,
              0.0f,-1.0f, 0.0f, 0.0f,
             -1.0f, 0.0f, 0.0f, 0.0f,
              0.0f, 0.0f, 0.0f, 1.0f },

              // -X (Left face)
              { 0.0f, 0.0f, 1.0f, 0.0f,
                0.0f,-1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f },

                // +Y (Top face) - FIXED
                { 1.0f, 0.0f, 0.0f, 0.0f,
                  0.0f, 0.0f,-1.0f, 0.0f,
                  0.0f, 1.0f, 0.0f, 0.0f,
                  0.0f, 0.0f, 0.0f, 1.0f },

                  // -Y (Bottom face) - FIXED  
                  { 1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f,-1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f },

                    // +Z (Front face)
                    { 1.0f, 0.0f, 0.0f, 0.0f,
                      0.0f,-1.0f, 0.0f, 0.0f,
                      0.0f, 0.0f,-1.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 1.0f },

                      // -Z (Back face)
                      {-1.0f, 0.0f, 0.0f, 0.0f,
                        0.0f,-1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f,
                        0.0f, 0.0f, 0.0f, 1.0f }
        };

        // Configure framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize, cubemapSize);

        // Convert HDR equirectangular environment map to cubemap
        m_shader.use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, equirectTexture);
        m_shader.setInt("equirectangularMap", 0);

        
        m_shader.setMat4("projection", projection);

        glViewport(0, 0, cubemapSize, cubemapSize);
        glBindVertexArray(cubeVAO);

        for (unsigned int i = 0; i < 6; ++i) {
            m_shader.setMat4("view", viewMatrices.at(i));

            // Attach cubemap face
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, envCubemap, 0);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Draw cube
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        return envCubemap;
    }

private:
    shader m_shader;
    GLuint cubeVAO, cubeVBO;
    GLuint captureFBO, captureRBO;

    void setupShaders() {
        m_shader = shader("src/shaders/projection.vert", "src/shaders/equirectToCubemap.frag");
    }

    void setupGeometry() {
        // Cube vertices
        float cubeVertices[] = {
            // positions          
            -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

            -1.0f,  1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f,  1.0f
        };

        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);

        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

        glBindVertexArray(cubeVAO);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    }

    void setupFramebuffer() {
        glGenFramebuffers(1, &captureFBO);
        glGenRenderbuffers(1, &captureRBO);
    }
};

// Usage example:
/*
// Load your equirectangular HDR texture first (using stb_image or similar)
GLuint equirectTexture = loadHDRTexture("path/to/your/hdr.hdr");

// Create converter and convert to cubemap
EquirectToCubemap converter;
GLuint cubemapTexture = converter.convertToCubemap(equirectTexture, 1024);

// Now you can use cubemapTexture as your HDR cubemap
// Don't forget to generate mipmaps if needed:
glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
*/