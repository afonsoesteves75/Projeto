#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>

struct Triangle { glm::vec3 v0, v1, v2; };

struct Mesh {
    std::vector<glm::vec3> vertices;
    std::vector<Triangle>  triangles;
    glm::vec3 bboxMin, bboxMax;
    float     pixelSize = 0.f; // t
    float     sigma     = 0.f; // 3t
    int       imgW = 0, imgH = 0;
    bool      isPointCloud = false;
};

Mesh loadOBJ(const std::string& path, int imgW, int imgH);
Mesh loadPointCloud(const std::string& path, int imgW, int imgH);
