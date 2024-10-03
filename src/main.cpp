#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <memory>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "glm/gtx/string_cast.hpp"

// Include your provided implementations
#include "shaderClass.h"
#include "VAO.h"
#include "VBO.h"
#include "EBO.h"
#include "Camera.h"

const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;

const float G = 6.67430e-11f;  // The Gravitational constant :)
const float theta = 0.5f;  // Barnes-Hut opening angle

int initialZoom = 10; // larger values mean more steps
int zoomStatus = initialZoom;
float initialFov = 80.0f;
float fov = initialFov;
float initialFar = 100.0f;
float far = initialFar;

#define PI 3.14159265
using dvec3 = glm::dvec3; // double precision for very large or small values

void createSphereMesh(std::vector<float>& vertices, std::vector<unsigned int>& indices, float radius, int segments) {
    vertices.clear();
    indices.clear();

    for (int y = 0; y <= segments; y++) {
        for (int x = 0; x <= segments; x++) {
            float xSegment = (float)x / (float)segments;
            float ySegment = (float)y / (float)segments;
            float xPos = std::cos(xSegment * 2.0f * PI) * std::sin(ySegment * PI) * radius;
            float yPos = std::cos(ySegment * PI) * radius;
            float zPos = std::sin(xSegment * 2.0f * PI) * std::sin(ySegment * PI) * radius;

            vertices.push_back(xPos);
            vertices.push_back(yPos);
            vertices.push_back(zPos);
            vertices.push_back(xSegment); // Use xSegment as r color
            vertices.push_back(ySegment); // Use ySegment as g color
            vertices.push_back(1.0f - ySegment); // Inverse of ySegment as b color
        }
    }

    for (int y = 0; y < segments; y++) {
        for (int x = 0; x < segments; x++) {
            indices.push_back(y * (segments + 1) + x);
            indices.push_back((y + 1) * (segments + 1) + x);
            indices.push_back(y * (segments + 1) + x + 1);

            indices.push_back(y * (segments + 1) + x + 1);
            indices.push_back((y + 1) * (segments + 1) + x);
            indices.push_back((y + 1) * (segments + 1) + x + 1);
        }
    }
}

class CelestialBody {
public:
    dvec3 position;
    dvec3 velocity;
    dvec3 force;
    double radius;
    double mass;
    glm::vec3 color;
    VAO vao;
    VBO* vbo;
    EBO* ebo;
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    CelestialBody(const dvec3& pos, const dvec3& vel, double r, double m, const glm::vec3& col)
        : position(pos), velocity(vel), force(0.0, 0.0, 0.0), radius(r), mass(m), color(col), vbo(nullptr), ebo(nullptr) {
        createSphereMesh(vertices, indices, static_cast<float>(radius), 20);

        std::cout << "Vertices: " << vertices.size() << ", Indices: " << indices.size() << std::endl;

        if (vertices.empty() || indices.empty()) {
            throw std::runtime_error("Failed to create sphere mesh");
        }

        vbo = new VBO(vertices.data(), vertices.size() * sizeof(float));
        ebo = new EBO(indices.data(), indices.size() * sizeof(unsigned int));
        vao.Bind();
        vao.LinkAttrib(*vbo, 0, 3, GL_FLOAT, 6 * sizeof(float), (void*)0);
        vao.LinkAttrib(*vbo, 1, 3, GL_FLOAT, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        vao.Unbind();
        vbo->Unbind();
        ebo->Unbind();
    }

    ~CelestialBody() {
        delete vbo;
        delete ebo;
    }

    // Prevent copying
    CelestialBody(const CelestialBody&) = delete;
    CelestialBody& operator=(const CelestialBody&) = delete;

    CelestialBody(CelestialBody&& other) noexcept
        : position(other.position), velocity(other.velocity), force(other.force),
          radius(other.radius), mass(other.mass), color(other.color),
          vao(std::move(other.vao)), vbo(other.vbo), ebo(other.ebo),
          vertices(std::move(other.vertices)), indices(std::move(other.indices)) {
        other.vbo = nullptr;
        other.ebo = nullptr;
    }

    CelestialBody& operator=(CelestialBody&& other) noexcept {
        if (this != &other) {
            delete vbo;
            delete ebo;
            position = other.position;
            velocity = other.velocity;
            force = other.force;
            radius = other.radius;
            mass = other.mass;
            color = other.color;
            vao = std::move(other.vao);
            vbo = other.vbo;
            ebo = other.ebo;
            vertices = std::move(other.vertices);
            indices = std::move(other.indices);
            other.vbo = nullptr;
            other.ebo = nullptr;
        }
        return *this;
    }

    void draw(Shader& shader) {
        shader.Activate();
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(position));  // Convert to float for rendering
        shader.setMat4("model", model);
        shader.setVec3("color", color);

        vao.Bind();
        ebo->Bind();
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
        ebo->Unbind();
        vao.Unbind();
    }

    void update(double dt) {
        dvec3 acceleration = force / mass;
        velocity += acceleration * dt;
        position += velocity * dt;
        force = dvec3(0.0, 0.0, 0.0);  // Reset force for next frame

        if (glm::any(glm::isnan(position)) || glm::any(glm::isnan(velocity))) {
            std::cout << "NaN detected! Position: " << glm::to_string(position)
                      << ", Velocity: " << glm::to_string(velocity)
                      << ", Force: " << glm::to_string(force)
                      << ", Mass: " << mass << std::endl;
        }
    }
};

class OctreeNode {
public:
    dvec3 center;
    double size;
    dvec3 centerOfMass;
    double totalMass;
    std::vector<CelestialBody*> bodies;
    std::unique_ptr<OctreeNode> children[8];

    OctreeNode(const dvec3& center, double size)
        : center(center), size(size), centerOfMass(0.0, 0.0, 0.0), totalMass(0.0) {}

    bool isLeaf() const {
        return children[0] == nullptr;
    }

    int getOctant(const dvec3& position) const {
        int octant = 0;
        if (position.x >= center.x) octant |= 4;
        if (position.y >= center.y) octant |= 2;
        if (position.z >= center.z) octant |= 1;
        return octant;
    }

    void insert(CelestialBody* body) {
        if (isLeaf() && bodies.empty()) {
            bodies.push_back(body);
            centerOfMass = body->position;
            totalMass = body->mass;
        } else {
            if (isLeaf() && bodies.size() == 1) {
                CelestialBody* existingBody = bodies[0];
                bodies.clear();
                subdivide();
                insertToChild(existingBody);
            }
            insertToChild(body);

            // Update center of mass and total mass
            dvec3 weightedPos = centerOfMass * totalMass + body->position * body->mass;
            totalMass += body->mass;
            centerOfMass = weightedPos / totalMass;
        }
    }

private:
    void subdivide() {
        double childSize = size / 2.0;
        for (int i = 0; i < 8; ++i) {
            dvec3 childCenter = center;
            childCenter.x += ((i & 4) ? childSize : -childSize) / 2.0;
            childCenter.y += ((i & 2) ? childSize : -childSize) / 2.0;
            childCenter.z += ((i & 1) ? childSize : -childSize) / 2.0;
            children[i] = std::make_unique<OctreeNode>(childCenter, childSize);
        }
    }

    void insertToChild(CelestialBody* body) {
        int octant = getOctant(body->position);
        children[octant]->insert(body);
    }
};

class Octree {
public:
    std::unique_ptr<OctreeNode> root;

    void build(const std::vector<CelestialBody>& bodies) {
        if (bodies.empty()) return;

        // Find bounding box
        dvec3 min = bodies[0].position, max = bodies[0].position;
        for (const auto& body : bodies) {
            min = glm::min(min, body.position);
            max = glm::max(max, body.position);
        }

        dvec3 center = (min + max) * 0.5;
        double size = glm::length(max - min) * 0.5;

        root = std::make_unique<OctreeNode>(center, size);

        for (const auto& body : bodies) {
            root->insert(const_cast<CelestialBody*>(&body));
        }
    }
};

void calculateForce(CelestialBody* body, const OctreeNode* node) {
    if (node->isLeaf() && node->bodies.empty()) {
        return;
    }

    double d = glm::length(node->centerOfMass - body->position);
    if (d < 1) return;  // Prevent division by zero

    if (node->isLeaf() || (node->size / d < theta)) {
        dvec3 direction = glm::normalize(node->centerOfMass - body->position);
        double forceMagnitude = G * body->mass * node->totalMass / (d * d);
        body->force += direction * forceMagnitude;
    } else {
        for (int i = 0; i < 8; ++i) {
            if (node->children[i]) {
                calculateForce(body, node->children[i].get());
            }
        }
    }
}

void updateForces(std::vector<CelestialBody>& bodies, const Octree& octree) {
    for (auto& body : bodies) {
        calculateForce(&body, octree.root.get());
    }
}

glm::vec3 getColorForBody(double mass, double radius) {
    // Example color scheme:
    // - Blue for small, low mass bodies (like planets)
    // - White-yellow for medium bodies
    // - Red for large, high mass bodies (like red giants)

    double massScale = std::log10(mass) / 30.0;  // Assuming masses range from about 1e24 to 1e30
    double radiusScale = std::log10(radius) / 10.0;  // Adjust based on your radius range

    glm::vec3 color;
    if (massScale < 0.3) {
        color = glm::mix(glm::vec3(0.0, 0.0, 1.0), glm::vec3(0.0, 1.0, 1.0), massScale / 0.3);
    } else if (massScale < 0.7) {
        color = glm::mix(glm::vec3(0.0, 1.0, 1.0), glm::vec3(1.0, 1.0, 0.0), (massScale - 0.3) / 0.4);
    } else {
        color = glm::mix(glm::vec3(1.0, 1.0, 0.0), glm::vec3(1.0, 0.0, 0.0), (massScale - 0.7) / 0.3);
    }

    // Adjust brightness based on radius
    color = glm::mix(color, glm::vec3(1.0), radiusScale);

    return color;
}

int main() {

    // GRAPHICS INITIALIZATION STUFF
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Space Simulation", NULL, NULL);
    if (window == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }
    glEnable(GL_DEPTH_TEST);

    Shader shader("assets/default.vert", "assets/default.frag");

    std::vector<CelestialBody> celestialBodies;

    // Star
    celestialBodies.emplace_back(dvec3(0.0), dvec3(0.0), 2.0, 1.989e30, glm::vec3(1.0f, 0.9f, 0.2f));

    // Planets
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dis(-5e7, 5e7);  // Larger scale
    std::uniform_real_distribution<double> velDis(-1e4, 1e4);  // Reasonable velocities
    std::uniform_real_distribution<> colorDis(0.3, 1.0);

    for (int i = 0; i < 100; ++i) {
        dvec3 position(dis(gen), dis(gen), dis(gen));
        dvec3 velocity(velDis(gen), velDis(gen), velDis(gen));
        double radius = 1e8 + static_cast<double>(i % 10) * 1e7;  // Reasonable planet sizes
        double mass = 5.97e24 * (radius / 1e8);  // Scale mass with radius
        glm::vec3 color = getColorForBody(mass, radius);
        celestialBodies.emplace_back(position, velocity, radius, mass, color);
    }

    // manages camera and zoom
    Camera camera(SCR_WIDTH, SCR_HEIGHT, glm::vec3(0.0f, 0.0f, 30.0f));
    glfwSetScrollCallback(window, [](GLFWwindow* window,double xoffset, double yoffset) {
        if (zoomStatus > 1) { zoomStatus += yoffset; }
        std::cout << "zoomStatus = " << zoomStatus << std::endl;
        fov = initialFov * initialZoom / zoomStatus;
        far = initialFar / initialZoom * pow(zoomStatus, 1.5);
        std::cout << "fov: " << fov << std::endl;
        std::cout << "far: " << far << std::endl;
    });



    // Set up lighting
    glm::vec3 lightPos(10.0f, 10.0f, 10.0f);
    shader.Activate();
    shader.setVec3("lightPos", lightPos);

    float lastFrame = 0.0f;
    Octree octree;

    while (!glfwWindowShouldClose(window)) {

        // FRAME COUNTING
        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glClearColor(0.0f, 0.0f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Update physics
        octree.build(celestialBodies);
        updateForces(celestialBodies, octree);
        for (auto& body : celestialBodies) {
            body.update(deltaTime);
        }

        camera.Inputs(window);
        camera.Matrix(fov, 0.1f, far, shader, "camMatrix");

        // Update view position for specular lighting
        shader.setVec3("viewPos", camera.Position);

        for (auto& body : celestialBodies) {
            body.draw(shader);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}