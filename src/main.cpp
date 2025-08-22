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

#define RT_DEBUG

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "glad2/gl.h"
#include "GLFW/glfw3.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../external/stb_image.h"
#include "../external/stb_image_write.h"

#include <chrono>
#include <iomanip>

#include "rt_includes.h"
#include "equirectToCubemap.h"

void processInput(GLFWwindow* window);

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

// Handling Cubemap Texture Loading
GLuint loadCubemap(const std::vector<std::string>& faces);
GLuint loadHDRCubemap(const std::vector<std::string>& faces);

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

void saveScreenshot(const char* filename, int width, int height) {
	std::vector<unsigned char> pixels(width * height * 3);
	glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

	// Flip vertically
	for (int y = 0; y < height / 2; ++y) {
		for (int x = 0; x < width * 3; ++x) {
			std::swap(pixels[y * width * 3 + x], pixels[(height - 1 - y) * width * 3 + x]);
		}
	}

	stbi_write_png(filename, width, height, 3, pixels.data(), width * 3);
}

int main() {
	// Create GLFW window
	GLFWwindow* window = init(WIDTH, HEIGHT, "Raytracing");

	// Create some shaders
	shader my_shader("src/shaders/fullscreen.vert", "src/shaders/fragment.frag");
	shader brightPassShader("src/shaders/fullscreen.vert", "src/shaders/bloom_extract.frag");
	shader blurShader("src/shaders/fullscreen.vert", "src/shaders/blur.frag");
	shader finalCompositeShader("src/shaders/fullscreen.vert", "src/shaders/composite.frag");

	// Create an array of Materials for lookup in the Shader.
	std::vector<Material> mats = {
		//  albedo x,   y,   z,   w,       Material Type, Emission, Fuzz, IOR
		Material{0.7, 0.7, 0.9, 0.0, MaterialType::Metal,      0.0, 0.01, 0.0},
		Material{0.6, 0.6, 0.6, 0.0, MaterialType::Lambertian, 0.0, 0.0,  0.0},
		Material{1.0, 0.8, 0.6, 0.0, MaterialType::Emissive,   5.0, 0.0,  0.0},
		Material{1.0, 0.9, 0.6, 0.0, MaterialType::Emissive,   3.5, 0.0,  0.0},
		Material{1.0, 1.0, 1.0, 0.0, MaterialType::Dielectric, 0.0, 0.0,  1.5}, // Glass
		Material{1.0, 0.8, 1.0, 0.0, MaterialType::Lambertian, 0.0, 0.0,  0.0},
		Material{0.7, 0.7, 0.7, 0.0, MaterialType::Metal,      0.0, 0.5,  0.0},
		Material{0.8, 0.6, 0.2, 0.0, MaterialType::Metal,      0.0, 0.0,  0.0}, // Brass
		Material{0.6, 0.6, 0.6, 0.0, MaterialType::Metal,      0.0, 0.0,  0.0}	// Iron(?)
	}; // Note: albedo.w is for memory alignment, and is unused in the shader.

	// Load mesh and create triangles
	std::vector<Triangle> allTriangles;

	std::vector<MeshInstance> instances(2);

	// Load meshes

	float meshPositions[2][3] = { 
		{2.0f, -0.65f, -1.0f}, 
		{1.0f, -0.35f, -1.0f} 
	};
	float meshRotations[2][3] = { 
		{0.0f, 3.0f * 3.14f / 4.0f, 0.0f},
		{0.0f, 3.0f * 3.14f / 4.0f, 0.0f},
	};
	float meshScales[2] = {0.0f, 0.35f};

	// Bunny
	try {
		rt_Mesh mesh("external/box.obj", 8); // path, material ID (index in mats)

		MeshInstance meshInst;
		meshInst.name = "Box";
		meshInst.materialID = 8;
		meshInst.originalTris = mesh.getTriangles(); // object-space
		meshInst.firstTri = allTriangles.size();
		meshInst.triCount = meshInst.originalTris.size();
		
		meshInst.position = glm::vec3(meshPositions[0][0], meshPositions[0][1], meshPositions[0][2]);
		meshInst.rotation = glm::vec3(meshRotations[0][0], meshRotations[0][1], meshRotations[0][2]);
		meshInst.scale = glm::vec3(meshScales[0]); // your original scale
		meshInst.updateModel();
		std::vector<Triangle> tmp;
		ApplyTransform(meshInst.originalTris, tmp, meshInst.model);
		allTriangles.insert(allTriangles.end(), tmp.begin(), tmp.end());

		instances[0] = meshInst;
	}
	catch (const std::exception& e) {
		std::cout << "Failed to load mesh: " << e.what() << std::endl;
	}

	// Monkey
	try {
		rt_Mesh mesh("external/smooth-monkey.obj", 7); // path, material ID (index in mats)

		MeshInstance meshInst;
		meshInst.name = "Monkey";
		meshInst.materialID = 7;
		meshInst.originalTris = mesh.getTriangles(); // object-space
		meshInst.firstTri = allTriangles.size();
		meshInst.triCount = meshInst.originalTris.size();

		meshInst.position = glm::vec3(meshPositions[1][0], meshPositions[1][1], meshPositions[1][2]);
		meshInst.rotation = glm::vec3(meshRotations[1][0], meshRotations[1][1], meshRotations[1][2]);
		meshInst.scale = glm::vec3(meshScales[1]);
		meshInst.updateModel();
		std::vector<Triangle> tmp;
		ApplyTransform(meshInst.originalTris, tmp, meshInst.model);
		allTriangles.insert(allTriangles.end(), tmp.begin(), tmp.end());
		instances[1] = meshInst;
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
		Sphere{2,  -0.25, -0.25, 0.0, 0.25,  4, 0, 0},
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
#ifdef RT_DEBUG
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
#endif
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
	glBufferData(GL_SHADER_STORAGE_BUFFER, mats.size() * sizeof(Material), mats.data(), GL_STATIC_DRAW);
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

	// Post Processing Setup

	GLuint hdrFBO;
	glGenFramebuffers(1, &hdrFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);

	GLuint colorBuffer;
	glGenTextures(1, &colorBuffer);
	glBindTexture(GL_TEXTURE_2D, colorBuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, WIDTH, HEIGHT, 0, GL_RGBA, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer, 0);


	GLuint bloomFBO[2];
	GLuint bloomTex[2];

	glGenFramebuffers(2, bloomFBO);
	glGenTextures(2, bloomTex);

	for (int i = 0; i < 2; i++) {
		glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[i]);
		glBindTexture(GL_TEXTURE_2D, bloomTex[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, WIDTH, HEIGHT, 0, GL_RGBA, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex[i], 0);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cerr << "Bloom FBO " << i << " not complete!" << std::endl;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	///

	// Load HDR Cubemap
	GLuint equirectTexture = loadHDRTexture("textures/skybox/hdrSky.hdr");

	EquirectToCubemap converter;
	GLuint cubemapTexture = converter.convertToCubemap(equirectTexture, 1024);

	glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	bool useSkybox = true;
	float skyboxIntentsity = 1.0f;
	float maxIntensity = 10.0f;
		
	//

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 430 core");

	while (!glfwWindowShouldClose(window)) {
		float currentFrame = static_cast<float>(glfwGetTime());
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		processInput(window);

		// Store previous camera state
		static glm::vec3 lastCamPos = camera.Position;
		static glm::vec3 lastCamFront = camera.Front;
		static glm::vec3 lastCamUp = camera.Up;

		// Detect change
		if (camera.Position != lastCamPos || camera.Front != lastCamFront || camera.Up != lastCamUp) {
			frameCount = 1; // reset accumulation
			lastCamPos = camera.Position;
			lastCamFront = camera.Front;
			lastCamUp = camera.Up;
		}

		// === STEP 1: RAYTRACING PASS ===
		// Render raytracing result to accumulation buffer
		glBindFramebuffer(GL_FRAMEBUFFER, accumulationFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumulationTex[writeIndex], 0);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			std::cerr << "Framebuffer incomplete!" << std::endl;
		}

		glViewport(0, 0, WIDTH, HEIGHT);
		glClearColor(0.2f, 0.0f, 0.2f, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);

		// Bind the previous frame's accumulation texture for reading
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, accumulationTex[readIndex]);

		my_shader.use();
		my_shader.setInt("u_accumulationTex", 0);

		// Set cubemap uniforms
		if (useSkybox && cubemapTexture != 0) {
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
			my_shader.setInt("u_skybox", 1);
			my_shader.setBool("u_useSkybox", true);
		}
		else {
			my_shader.setBool("u_useSkybox", false);
		}

		// Set camera uniforms
		my_shader.setVec3("camPos", camera.Position);
		my_shader.setVec3("camFront", camera.Front);
		my_shader.setVec3("camRight", camera.Right);
		my_shader.setVec3("camUp", camera.Up);
		my_shader.setFloat("camFov", camera.Zoom);
		my_shader.setVec2("resolution", glm::vec2(WIDTH, HEIGHT));
		my_shader.setFloat("time", glfwGetTime());
		my_shader.setInt("frameCount", frameCount++);
		my_shader.setFloat("skyboxIntensity", skyboxIntentsity);
		my_shader.setFloat("maxIntensity", maxIntensity);

		// RENDER THE RAYTRACING
		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		// Swap read/write indices for accumulation
		std::swap(readIndex, writeIndex);

		// === STEP 2: BLOOM BRIGHT PASS ===
		glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[0]);
		glViewport(0, 0, WIDTH, HEIGHT);
		glClear(GL_COLOR_BUFFER_BIT);

		brightPassShader.use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, accumulationTex[readIndex]); // Use the newly written accumulation
		brightPassShader.setInt("hdrTex", 0);
		brightPassShader.setFloat("threshold", 1.0f);

		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		// === STEP 3: BLOOM BLUR PASSES ===
		bool horizontal = true;
		int blurIterations = 10;
		int read = 0, write = 1;

		for (int i = 0; i < blurIterations; i++) {
			glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[write]);
			glViewport(0, 0, WIDTH, HEIGHT);
			glClear(GL_COLOR_BUFFER_BIT);

			blurShader.use();
			blurShader.setBool("horizontal", horizontal);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, bloomTex[read]);
			blurShader.setInt("image", 0);

			glBindVertexArray(VAO);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

			horizontal = !horizontal;
			std::swap(read, write);
		}

		// === STEP 4: FINAL COMPOSITE TO SCREEN ===
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, WIDTH, HEIGHT);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);

		finalCompositeShader.use();

		// Bind raytraced scene
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, accumulationTex[readIndex]);
		finalCompositeShader.setInt("hdrTex", 0);

		// Bind bloom result
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, bloomTex[read]);
		finalCompositeShader.setInt("bloomTex", 1);

		finalCompositeShader.setFloat("exposure", 1.0f);

		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		// Mesh Controls
		std::string label;

		bool anyMeshMoved = false;
		int fps = 1 / deltaTime;
		std::string fps_label = "FPS: " + std::to_string(fps);

		ImGui::Begin("Settings");
		ImGui::Text(fps_label.c_str());

		ImGui::Separator();

		ImGui::Text("Move Meshes");
		
		label = instances[0].name + " Position";
		anyMeshMoved |= ImGui::DragFloat3(label.c_str(), meshPositions[0], 0.01f);
		label = instances[1].name + " Position";
		anyMeshMoved |= ImGui::DragFloat3(label.c_str(), meshPositions[1], 0.01f);

		ImGui::Separator();
		label = instances[0].name + " Rotation";
		anyMeshMoved |= ImGui::DragFloat3(label.c_str(), meshRotations[0], 0.01f);
		label = instances[1].name + " Rotation";
		anyMeshMoved |= ImGui::DragFloat3(label.c_str(), meshRotations[1], 0.01f);

		ImGui::Separator();
		label = instances[0].name + " Scale";
		anyMeshMoved |= ImGui::DragFloat(label.c_str(), &meshScales[0], 0.01f);
		label = instances[1].name + " Scale";
		anyMeshMoved |= ImGui::DragFloat(label.c_str(), &meshScales[1], 0.01f);

		ImGui::Separator();
		label = instances[0].name + " Material";
		anyMeshMoved |= ImGui::DragInt(label.c_str(), &instances[0].materialID, 1.0f, 0, mats.size() - 1);
		label = instances[1].name + " Material";
		anyMeshMoved |= ImGui::DragInt(label.c_str(), &instances[1].materialID, 1.0f, 0, mats.size() - 1);

		ImGui::Separator();
		if (ImGui::DragFloat("Skybox Intensity", &skyboxIntentsity, 0.01f, 0.05f, 10.0f)) {
			frameCount = 1;
		}
		if (ImGui::DragFloat("Skybox Max Intensity", &maxIntensity, 0.01f, 1.0f, 20.0f)) {
			frameCount = 1;
		}

		ImGui::Separator();

		if (ImGui::Button("Click to Regain Mouse Control")) {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			mouse_captured = true;
		}
		if (ImGui::Button("Take Screenshot")) {
			auto now = std::chrono::system_clock::now();
			std::time_t t = std::chrono::system_clock::to_time_t(now);

			std::tm tm;
			localtime_s(&tm, &t);

			std::ostringstream ss;
			ss << "screenshots/raytracing-"
				<< std::put_time(&tm, "%Y-%m-%d_%H-%M-%S")
				<< ".png";
			std::string filename = ss.str();
			saveScreenshot(filename.c_str(), WIDTH, HEIGHT);
		}

		if (anyMeshMoved) {
			frameCount = 1;

			int i = 0;
			for (auto& inst : instances) {
				inst.position = glm::vec3(meshPositions[i][0], meshPositions[i][1], meshPositions[i][2]);
				inst.rotation = glm::vec3(meshRotations[i][0], meshRotations[i][1], meshRotations[i][2]);
				inst.scale = glm::vec3(meshScales[i]);
				inst.updateModel();

				std::vector<Triangle> transformed;
				ApplyTransform(inst.originalTris, transformed, inst.model);
				std::copy(transformed.begin(), transformed.end(), allTriangles.begin() + inst.firstTri);
				i++;
			}

			glBindBuffer(GL_SHADER_STORAGE_BUFFER, triSSBO);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
				allTriangles.size() * sizeof(Triangle), allTriangles.data());
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

			bvhBuilder.refit(allTriangles);
			const auto& bvhNodes = bvhBuilder.getNodes();
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, bvhSSBO);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
				bvhNodes.size() * sizeof(BVHNode), bvhNodes.data());
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		}

		ImGui::End();
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

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

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwTerminate();
	return 0;
}