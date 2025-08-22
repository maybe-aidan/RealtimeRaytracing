#ifndef RT_SKYBOX_H
#define RT_SKYBOX_H

#include <glad2/gl.h>

#include <vector>
#include <string>
#include <iostream>

GLuint loadCubemap(const std::vector<std::string>& faces) {
	GLuint textureID;
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

	int width, height, nrChannels;
	for (unsigned int i = 0; i < faces.size(); i++) {
		unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
		if (data) {
			GLenum format = GL_RGB;
			if (nrChannels == 1) format = GL_RED;
			else if (nrChannels == 3) format = GL_RGB;
			else if (nrChannels == 4) format = GL_RGBA;

			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
				0, GL_RGB, width, height, 0, format, GL_UNSIGNED_BYTE, data);
			stbi_image_free(data);
		}
		else {
			std::cout << "Cubemap texture failed to load at path: " << faces[i] << std::endl;
			stbi_image_free(data);
		}
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	return textureID;
}

// For HDR cubemaps (.hdr files)
GLuint loadHDRCubemap(const std::vector<std::string>& faces) {
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    // Enable HDR loading in stb_image
    stbi_set_flip_vertically_on_load(false);

    for (unsigned int i = 0; i < faces.size(); i++) {
        int width, height, nrChannels;

        // Load as HDR float data
        float* data = stbi_loadf(faces[i].c_str(), &width, &height, &nrChannels, 0);

        if (data) {
            GLenum format = GL_RGB;
            if (nrChannels == 1) format = GL_RED;
            else if (nrChannels == 3) format = GL_RGB;
            else if (nrChannels == 4) format = GL_RGBA;

            // Use GL_RGB32F for HDR storage
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F,
                width, height, 0, format, GL_FLOAT, data);

            stbi_image_free(data);
            std::cout << "HDR Cubemap face " << i << " loaded successfully" << std::endl;
        }
        else {
            std::cout << "HDR Cubemap face " << i << " failed to load: " << faces[i] << std::endl;
            stbi_image_free(data);

            // Fallback: try loading as regular LDR and warn user
            unsigned char* ldr_data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
            if (ldr_data) {
                std::cout << "Warning: Loading " << faces[i] << " as LDR (no HDR data)" << std::endl;
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB,
                    width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, ldr_data);
                stbi_image_free(ldr_data);
            }
        }
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

#endif