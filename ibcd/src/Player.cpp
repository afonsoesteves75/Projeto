#include "Player.h"
#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>

void Player::configureFromMesh(const Mesh& mesh) {
    // Configura fisicamente o avatar baseado nas dimensoes do mapa e tamanho do pixel
    float span = std::max(mesh.bboxMax.x - mesh.bboxMin.x,
                          mesh.bboxMax.y - mesh.bboxMin.y);
    stepSize = mesh.pixelSize * 2.f; // Tamanho de 1 passo em condicoes normais
    minStep  = mesh.pixelSize * 0.25f; // Limite infimo do passo para tentar "deslizar" junto de obstaculos
    currentStep = stepSize;
    halfX = span * 0.015f; // Raio X de colisao
    halfY = span * 0.015f; // Raio Y de colisao
    maxZ  = span * 0.17f;  // Altura do avatar
}

void Player::update(float dx, float dy, CollisionSystem& col, const Mesh& mesh) {
    // Normaliza a direcao do input (W,A,S,D ou setas)
    glm::vec3 dir(dx, dy, 0.f);
    if (glm::length(dir) > 0.001f)
        dir = glm::normalize(dir);

    glm::vec3 prevPos = pos;

    if (glm::length(dir) > 0.001f) {
        // Tenta mover-se para a nova posicao (Eixo XY)
        glm::vec3 tryPos = pos + dir * currentStep;

        // Efetua a verificacao bidimensional recorrendo ao sistema de imagens
        bool blocked = col.checkObstacle(tryPos, mesh, halfX, halfY, minZ, maxZ);
        if (blocked) {
            // Logica de resolucao (deslize/abrandamento): Se colidir, reduzimos o passo e tentamos novamente depois
            if (currentStep > minStep)
                currentStep *= 0.5f;
            else
                pos = prevPos; // Bateu mesmo contra a parede
        } else {
            // Movimento limpo aprovado
            pos.x = tryPos.x;
            pos.y = tryPos.y;
            currentStep = stepSize;
        }
    }

    // Resolucao da Gravidade: Encontra o chao e 'cola' o avatar (Eixo Z)
    auto ground = col.queryGround(pos, mesh, halfX, halfY);
    if (ground.hasGround)
        pos.z = ground.groundZ;

    // Limites de seguranca para evitar que o jogador caia do mundo
    pos.x = std::clamp(pos.x, mesh.bboxMin.x, mesh.bboxMax.x);
    pos.y = std::clamp(pos.y, mesh.bboxMin.y, mesh.bboxMax.y);
    if (pos.z < mesh.bboxMin.z)
        pos.z = mesh.bboxMin.z;
}