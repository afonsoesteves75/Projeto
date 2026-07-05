#pragma once
#include <vector>
#include "OBJLoader.h"

struct ColoredPoint {
    glm::vec3 pos;
    float r, g, b, a;
};

std::vector<ColoredPoint> oversample(const Mesh& mesh);
std::vector<ColoredPoint> colorPointCloud(const Mesh& mesh);
