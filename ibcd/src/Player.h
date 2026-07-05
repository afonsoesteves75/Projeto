#pragma once
#include <glm/glm.hpp>
#include "CollisionSystem.h"
#include "OBJLoader.h"

class Player {
public:
    glm::vec3 pos;
    float stepSize   = 0.2f;
    float minStep    = 0.01f;
    float currentStep = 0.2f;

    float halfX = 0.15f;
    float halfY = 0.15f;
    float minZ  = 0.f;
    float maxZ  = 1.7f;

    Player(const glm::vec3& startPos) : pos(startPos), currentStep(stepSize) {}

    void configureFromMesh(const Mesh& mesh);
    void update(float dx, float dy, CollisionSystem& col, const Mesh& mesh);
};
