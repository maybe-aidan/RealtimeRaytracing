#ifndef SHADER_H
#define SHADER_H

// another class taken from https://learnopengl.com

#include "glad2/gl.h"
#include "glm/glm/glm.hpp"

#include <sstream>
#include <fstream>
#include <iostream>

class shader {
public:
	unsigned int ID;

	shader(const char* vertex_path, const char* fragment_path) {
        std::string vertexCode;
        std::string fragmentCode;

        std::ifstream vShaderFile;
        std::ifstream fShaderFile;

        vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

        try {
            // open files
            vShaderFile.open(vertex_path);
            fShaderFile.open(fragment_path);
            std::stringstream vShaderStream, fShaderStream;

            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();

            vShaderFile.close();
            fShaderFile.close();

            vertexCode = vShaderStream.str();
            fragmentCode = fShaderStream.str();

        }
        catch (std::ifstream::failure e) {
            std::cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ" << std::endl;
        }

        const char* vShaderCode = vertexCode.c_str();
        const char* fShaderCode = fragmentCode.c_str();

        // Compile the Shaders

        unsigned int vertex, fragment;
        int success;
        char infoLog[512];

        // Compile Vertex Shader
        vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vShaderCode, NULL);
        glCompileShader(vertex);

        // print any compile errors
        glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vertex, 512, NULL, infoLog);
            std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
        }

        // Compile Fragment Shader
        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fShaderCode, NULL);
        glCompileShader(fragment);

        // print any compile errors
        glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fragment, 512, NULL, infoLog);
            std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
        }

        // link into shader program
        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);
        glLinkProgram(ID);

        glGetProgramiv(ID, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(ID, 512, NULL, infoLog);
            std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
        }

        glDeleteShader(vertex);
        glDeleteShader(fragment);
	}


    void use() {
        glUseProgram(ID);
    }

    // Bool uniforms

    void setBool(const std::string& name, bool value) const {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value);
    }
    void setBool2(const std::string& name, bool v1, bool v2) const {
        glUniform2i(glGetUniformLocation(ID, name.c_str()), (int)v1, (int)v2);
    }
    void setBool3(const std::string& name, bool v1, bool v2, bool v3) const {
        glUniform3i(glGetUniformLocation(ID, name.c_str()), (int)v1, (int)v2, (int)v3);
    }
    void setBool4(const std::string& name, bool v1, bool v2, bool v3, bool v4) const {
        glUniform4i(glGetUniformLocation(ID, name.c_str()), (int)v1, (int)v2, (int)v3, (int)v4);
    }

    // Integer uniforms

    void setInt(const std::string& name, int value) const {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
    }
    void setInt2(const std::string& name, int v1, int v2) const {
        glUniform2i(glGetUniformLocation(ID, name.c_str()), v1, v2);
    }
    void setInt3(const std::string& name, int v1, int v2, int v3) const {
        glUniform3i(glGetUniformLocation(ID, name.c_str()), v1, v2, v3);
    }
    void setInt4(const std::string& name, int v1, int v2, int v3, int v4) const {
        glUniform4i(glGetUniformLocation(ID, name.c_str()), v1, v2, v3, v4);
    }

    // Float uniforms

    void setFloat(const std::string& name, float value) const {
        glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
    }
    void setFloat2(const std::string& name, float v1, float v2) const {
        glUniform2f(glGetUniformLocation(ID, name.c_str()), v1, v2);
    }
    void setFloat3(const std::string& name, float v1, float v2, float v3) const {
        glUniform3f(glGetUniformLocation(ID, name.c_str()), v1, v2, v3);
    }
    void setFloat4(const std::string& name, float v1, float v2, float v3, float v4) const {
        glUniform4f(glGetUniformLocation(ID, name.c_str()), v1, v2, v3, v4);
    }

    // Default Vectors
    void setFloatArray(const std::string& name, int size, const float* data) const {
        glUniform1fv(glGetUniformLocation(ID, name.c_str()), size, data);
    }
    void setFloat3Array(const std::string& name, int size, const float* data) const {
        glUniform3fv(glGetUniformLocation(ID, name.c_str()), size, data);
    }

    // GLM Vectors
    void setVec2(const std::string& name, glm::vec2 value) const {
        glUniform2fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
    }
    void setVec3(const std::string& name, glm::vec3 value) const {
        glUniform3fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
    }
    void setVec4(const std::string& name, glm::vec4 value) const {
        glUniform4fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
    }

    void setMat2(const std::string& name, glm::mat2 mat) const {
        glUniformMatrix2fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
    }
    void setMat3(const std::string& name, glm::mat3 mat) const {
        glUniformMatrix3fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
    }
    void setMat4(const std::string& name, glm::mat4 mat) const {
        glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
    }


    unsigned int GetID() {
        return ID;
    }

};

#endif
