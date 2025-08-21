// ============================================================================
// Realtime Ray Tracer
// Author: Aidan Fox
// 
// Overview:
//   A realtime ray tracer implemented in C++ and OpenGL. The core raytracing
//   logic (ray generation, intersection, shading) is executed almost entirely
//   in the fragment shader, rendered to a full-screen quad, and accumulated
//   over multiple frames for progressive refinement.
//
// Influences & References:
//   - "Ray Tracing in One Weekend" by Peter Shirley
//       https://raytracing.github.io/
//   - LearnOpenGL.com tutorials by Joey de Vries
//       https://learnopengl.com/
//
// Key Design Choices:
//   - CPU → GPU data transfer is minimized, with scene data (geometry,
//     materials, and BVH nodes) uploaded to GPU buffers.
//   - A Bounding Volume Hierarchy (BVH) is used to accelerate ray/scene
//     intersections, enabling realtime performance on static meshes.
//   - Progressive accumulation over time provides noise reduction and higher
//     quality images without sacrificing interactivity.
//
// Notes:
//   This project is for educational and experimental purposes. Some portions
//   of the code structure and algorithms are adapted from the above sources,
//   with custom modifications for GPU-based execution and real-time rendering.
// ============================================================================


#include "glad2/gl.h"
#include "GLFW/glfw3.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../external/stb_image.h"
#include <iostream>
#include <vector>

#include "../includes/shader.h"
#include "../includes/camera.h"
#include "rt_structs.h"
#include "rt_mesh.h"
#include "rt_bvh.h"

void processInput(GLFWwindow* window);

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

// Handling Cubemap Texture Loading
GLuint loadCubemap(const std::vector<std::string>& faces);
GLuint loadHDRCubemap(const std::vector<std::string>& faces);

const unsigned int WIDTH = 1920;
const unsigned int HEIGHT = 1080;

// Camera Setup
Camera camera(glm::vec3(4.56854f, 0.754347f, - 3.15879f));
float lastX = WIDTH / 2.0f;
float lastY = HEIGHT / 2.0f;
bool firstMouse = true;

// timing
float deltaTime = 0.0f;	// time between current frame and last frame
float lastFrame = 0.0f;

inline double random_double() {
	// Returns a random real in [0,1).
	return std::rand() / (RAND_MAX + 1.0);
}

// Basic window setup with GLFW and GLAD
GLFWwindow* init(unsigned int width, unsigned int height, const char* name) {
	if (!glfwInit()) {
		std::cout << "Failed to intialize GLFW" << std::endl;
		std::exit(-1);
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow* window = glfwCreateWindow(width, height, name, NULL, NULL);

	if (window == NULL) {
		std::cout << "Failed to create GLFW window!" << std::endl;
		glfwTerminate();
		std::exit(-1);
	}

	glfwMakeContextCurrent(window);
	// Callbacks
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwSetCursorPosCallback(window, mouse_callback);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSetKeyCallback(window, key_callback);

	// tell GLFW to capture our mouse
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);


	if (!gladLoadGL(glfwGetProcAddress)) {
		std::cout << "Failed to intialize GLAD" << std::endl;
		glfwTerminate();
		std::exit(-1);
	}

	return window;
}

int main() {
	// Create GLFW window
	GLFWwindow* window = init(WIDTH, HEIGHT, "Raytracing");

	// Create some shaders
	shader my_shader("src/shaders/fullscreen.vert", "src/shaders/fragment.frag");
	shader screen_shader("src/shaders/screen_quad.vert", "src/shaders/screen_quad.frag");

	// Create an array of Materials for lookup in the Shader.
	Material mats[9] = {
		//  albedo x,   y,   z,   w,       Material Type, Emission, Fuzz, IOR
		Material{0.7, 0.7, 0.9, 0.0, MaterialType::Metal,      0.0, 0.01, 0.0},
		Material{0.6, 0.6, 0.6, 0.0, MaterialType::Lambertian, 0.0, 0.0,  0.0},
		Material{1.0, 0.8, 0.6, 0.0, MaterialType::Emissive,   1.0, 0.0,  0.0},
		Material{1.0, 0.9, 0.6, 0.0, MaterialType::Emissive,   1.0, 0.0,  0.0},
		Material{1.0, 1.0, 1.0, 0.0, MaterialType::Dielectric, 0.0, 0.0,  1.5}, // Glass
		Material{1.0, 0.8, 1.0, 0.0, MaterialType::Lambertian, 0.0, 0.0,  0.0},
		Material{0.7, 0.7, 0.7, 0.0, MaterialType::Metal,      0.0, 0.5,  0.0},
		Material{0.8, 0.6, 0.2, 0.0, MaterialType::Metal,      0.0, 0.0,  0.0}, // Brass
		Material{0.6, 0.6, 0.6, 0.0, MaterialType::Metal,      0.0, 0.0,  0.0}	// Iron(?)
	}; // Note: albedo.w is for memory alignment, and is unused in the shader.

	// Load mesh and create triangles
	std::vector<Triangle> allTriangles;

	// Load meshes

	// Bunny
	try {
		rt_Mesh mesh("external/smooth-bunny.obj", 8); // path, material ID (index in mats)

		// Transform the mesh
		glm::mat4 transform = glm::mat4(1.0f);
		transform = glm::translate(transform, glm::vec3(2.0f, -0.65f, -1.0f));
		transform = glm::rotate(transform, 3.0f * 3.14f / 4.0f, glm::vec3(0, 1, 0));
		transform = glm::scale(transform, glm::vec3(5.0f));
		mesh.transform(transform);

		// Add mesh triangles to the triangle list
		const auto& meshTriangles = mesh.getTriangles();
		allTriangles.insert(allTriangles.end(), meshTriangles.begin(), meshTriangles.end());

		// Debug stuff
		std::cout << "Loaded mesh with " << meshTriangles.size() << " triangles" << std::endl;
	}
	catch (const std::exception& e) {
		std::cout << "Failed to load mesh: " << e.what() << std::endl;
	}

	// Monkey
	try {
		rt_Mesh mesh("external/smooth-monkey.obj", 7); // path, material ID (index in mats)

		// Transform the mesh
		glm::mat4 transform = glm::mat4(1.0f);
		transform = glm::translate(transform, glm::vec3(1.0f, -0.35f, -1.0f));
		transform = glm::rotate(transform, 3.0f * 3.14f / 4.0f, glm::vec3(0, 1, 0));
		transform = glm::rotate(transform, -3.14f/4.0f, glm::vec3(1, 0, 0));
		transform = glm::scale(transform, glm::vec3(0.35f));
		mesh.transform(transform);

		// Add mesh triangles to the triangle list
		const auto& meshTriangles = mesh.getTriangles();
		allTriangles.insert(allTriangles.end(), meshTriangles.begin(), meshTriangles.end());

		// Debug stuff
		std::cout << "Loaded mesh with " << meshTriangles.size() << " triangles" << std::endl;
	}
	catch (const std::exception& e) {
		std::cout << "Failed to load mesh: " << e.what() << std::endl;
	}

	// Spheres setup
	Sphere spheres[7] = {
		Sphere{0.0,    0.0,  -1.0, 0.0, 0.5,   0, 0, 0},
		Sphere{0.0, -100.5,  -1.0, 0.0, 100.0, 1, 0, 0},
		Sphere{-3.0,   0.0,   0.0, 0.0, 0.2,   2, 0, 0},
		Sphere{3.0,    0.5,  0.75, 0.0, 0.5,   3, 0, 0},
		//Sphere{2,  -0.25, -0.25, 0.0, 0.25,  4, 0, 0},
		Sphere{1.0,    0.5,   3.5, 0.0, 3.0,   0, 0, 0}
		//Sphere{0.0,   15.0,   0.0, 0.0, 10.0,  2, 0, 0}
	};

	int sphereCount = sizeof(spheres) / sizeof(Sphere);


	// Start building the BVH
	BVHBuilder bvhBuilder;
	bvhBuilder.build(allTriangles);

	const auto& bvhNodes = bvhBuilder.getNodes();
	const auto& primitives = bvhBuilder.getPrimitiveIndices();

	// Some BVH Debug stuff. -------------------------------------------------------------
	std::cout << "=== BVH DEBUG ===" << std::endl;
	std::cout << "Input triangles: " << allTriangles.size() << std::endl;
	std::cout << "BVH nodes: " << bvhNodes.size() << std::endl;
	std::cout << "BVH primitives: " << primitives.size() << std::endl;

	if (!bvhNodes.empty()) {
		std::cout << "Root node bounds: ("
			<< bvhNodes[0].min.x << "," << bvhNodes[0].min.y << "," << bvhNodes[0].min.z
			<< ") to ("
			<< bvhNodes[0].max.x << "," << bvhNodes[0].max.y << "," << bvhNodes[0].max.z
			<< ")" << std::endl;
		std::cout << "Root left child: " << bvhNodes[0].leftChild << std::endl;
		std::cout << "Root right child: " << bvhNodes[0].rightChild << std::endl;
	}

	if (!primitives.empty()) {
		std::cout << "First primitive triangle index: " << primitives[0] << std::endl;
	}
	std::cout << "=================" << std::endl;
	// End of BVH Debug ------------------------------------------------------------------

	std::srand(std::time({}));

	// Quad for rendering out raytraced texture
	float quadVertices[] = {
		-1.0f, -1.0f,
		 1.0f, -1.0f,
		-1.0f,  1.0f,
		 1.0f,  1.0f
	};

	unsigned int VAO, VBO;
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);

	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	
	// The textures being swapped for accumulation of rays over time.
	// This is what makes renders gradually increase in quality as you let the camera sit.
	GLuint accumulationTex[2];
	glGenTextures(2, accumulationTex);

	GLuint accumulationFBO;
	glGenFramebuffers(1, &accumulationFBO);

	for (int i = 0; i < 2; i++) {
		glBindTexture(GL_TEXTURE_2D, accumulationTex[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, WIDTH, HEIGHT, 0,
			GL_RGBA, GL_FLOAT, nullptr); // store HDR float data
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	int readIndex = 0;
	int writeIndex = 1;

	int frameCount = 1;

	for (int i = 0; i < 2; i++) {
		glBindTexture(GL_TEXTURE_2D, accumulationTex[i]);
		std::vector<float> empty(WIDTH * HEIGHT * 4, 0.0f);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RGBA, GL_FLOAT, empty.data());
	}

	float focusDistance = 20.0f; // Unused for now
	camera.lookAt(glm::vec3(0.0, 0.0, 0.0));

	// Sending material, sphere, and triangle data to the GPU through SSBOs.
	GLuint matSSBO;
	glGenBuffers(1, &matSSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, matSSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(mats), mats, GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, matSSBO); // binding=0 in GLSL
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	GLuint triSSBO;
	glGenBuffers(1, &triSSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, triSSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, allTriangles.size() * sizeof(Triangle), allTriangles.data(), GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, triSSBO); // binding=1 in GLSL
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	
	GLuint sphereSSBO;
	glGenBuffers(1, &sphereSSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, sphereSSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(spheres), spheres, GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sphereSSBO); // binding=2 in GLSL
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);


	// BVH Nodes SSBO
	GLuint bvhSSBO;

	if (!bvhNodes.empty()) {
		glGenBuffers(1, &bvhSSBO);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, bvhSSBO);
		glBufferData(GL_SHADER_STORAGE_BUFFER,
			bvhNodes.size() * sizeof(BVHNode),
			bvhNodes.data(), GL_STATIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, bvhSSBO); // binding = 3
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}

	// Primitive references SSBO
	GLuint primSSBO;

	if (!primitives.empty()) {
		glGenBuffers(1, &primSSBO);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, primSSBO);
		glBufferData(GL_SHADER_STORAGE_BUFFER,
			primitives.size() * sizeof(int),
			primitives.data(), GL_STATIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, primSSBO); // binding = 4
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}


	// Skybox setup
	std::vector<std::string> faces{
		"textures/skybox/right.jpg",
		"textures/skybox/left.jpg",
		"textures/skybox/top.jpg",
		"textures/skybox/bottom.jpg",
		"textures/skybox/front.jpg",
		"textures/skybox/back.jpg"
	};

	GLuint cubemapTexture = 0;
	bool useSkybox = false;

	try {
		cubemapTexture = loadHDRCubemap(faces);
		useSkybox = true;
		std::cout << "Cubemap skybox loaded successfully" << std::endl;
	}
	catch (...) {
		std::cout << "Failed to load cubemap, using procedural sky" << std::endl;
		useSkybox = false;
	}


	while (!glfwWindowShouldClose(window)) {

		float currentFrame = static_cast<float>(glfwGetTime());
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		// Store previous camera state
		static glm::vec3 lastCamPos = camera.Position;
		static glm::vec3 lastCamFront = camera.Front;
		static glm::vec3 lastCamUp = camera.Up;

		processInput(window);
		// Detect change
		if (camera.Position != lastCamPos || camera.Front != lastCamFront || camera.Up != lastCamUp) {
			frameCount = 1; // reset accumulation
			lastCamPos = camera.Position;
			lastCamFront = camera.Front;
			lastCamUp = camera.Up;
		}


		glBindFramebuffer(GL_FRAMEBUFFER, accumulationFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumulationTex[writeIndex], 0);

		// Make sure it's valid
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			std::cerr << "Framebuffer incomplete!" << std::endl;
		}

		// Bind the previous frame's accumulation texture for reading in the shader
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, accumulationTex[readIndex]);
		my_shader.setInt("u_accumulationTex", 0);

		glClearColor(0.2f, 0.5f, 0.2f, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);

		my_shader.use();

		// Set cubemap uniforms
		if (useSkybox && cubemapTexture != 0) {
			glActiveTexture(GL_TEXTURE1); // Use texture unit 1 for cubemap
			glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
			my_shader.setInt("u_skybox", 1);
			my_shader.setBool("u_useSkybox", true);
		}
		else {
			my_shader.setBool("u_useSkybox", false);
		}

		my_shader.setVec3("camPos", camera.Position);
		my_shader.setVec3("camFront", camera.Front);
		my_shader.setVec3("camRight", camera.Right);
		my_shader.setVec3("camUp", camera.Up);
		my_shader.setFloat("camFov", camera.Zoom);
		my_shader.setVec2("resolution", glm::vec2(WIDTH, HEIGHT));
		my_shader.setFloat("time", glfwGetTime());
		my_shader.setInt("frameCount", frameCount++);

		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		// Unbind FBO so we draw to screen next
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Swap read/write indices
		std::swap(readIndex, writeIndex);

		// Now draw the accumulated texture to the screen
		screen_shader.use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, accumulationTex[readIndex]);
		screen_shader.setInt("u_texture", 0);

		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		glfwPollEvents();
		glfwSwapBuffers(window);
	}
	// Cleanup
	if (cubemapTexture != 0) {
		glDeleteTextures(1, &cubemapTexture);
	}

	if (matSSBO) glDeleteBuffers(1, &matSSBO);
	if (triSSBO) glDeleteBuffers(1, &triSSBO);
	if (sphereSSBO) glDeleteBuffers(1, &sphereSSBO);
	if (bvhSSBO) glDeleteBuffers(1, &bvhSSBO);
	if (primSSBO) glDeleteBuffers(1, &primSSBO);

	glfwTerminate();
	return 0;
}

// process all input: query GLFW whether relevant keys are pressed/released this frame and react accordingly
// ---------------------------------------------------------------------------------------------------------
void processInput(GLFWwindow* window)
{
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		camera.ProcessKeyboard(FORWARD, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		camera.ProcessKeyboard(BACKWARD, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		camera.ProcessKeyboard(LEFT, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		camera.ProcessKeyboard(RIGHT, deltaTime);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	// Will use at some point probably
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	// make sure the viewport matches the new window dimensions; note that width and 
	// height will be significantly larger than specified on retina displays.
	glViewport(0, 0, width, height);
}


// glfw: whenever the mouse moves, this callback is called
// -------------------------------------------------------
void mouse_callback(GLFWwindow* window, double xposIn, double yposIn)
{
	float xpos = static_cast<float>(xposIn);
	float ypos = static_cast<float>(yposIn);

	if (firstMouse)
	{
		lastX = xpos;
		lastY = ypos;
		firstMouse = false;
	}

	float xoffset = xpos - lastX;
	float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top

	lastX = xpos;
	lastY = ypos;

	camera.ProcessMouseMovement(xoffset, yoffset);
}

// glfw: whenever the mouse scroll wheel scrolls, this callback is called
// ----------------------------------------------------------------------
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	// Will use at some point probably
}

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

	int width, height, nrChannels;
	for (unsigned int i = 0; i < faces.size(); i++) {
		float* data = stbi_loadf(faces[i].c_str(), &width, &height, &nrChannels, 0);
		if (data) {
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
				0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data);
			stbi_image_free(data);
		}
		else {
			std::cout << "HDR Cubemap texture failed to load at path: " << faces[i] << std::endl;
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