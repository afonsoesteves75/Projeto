#include "CollisionSystem.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "../external/stb_image.h"

int CollisionSystem::loadedSliceCount() const {
    int n = 0;
    // Conta quantas fatias nao sao nulas na cache
    for (auto& c : m_cache)
        if (c.has_value()) ++n;
    return n;
}

void CollisionSystem::init(const SliceMetadata& meta, int nslices) {
    m_meta = meta;
    m_nslices = std::max(1, nslices);
    // Inicializa a cache com espacos vazios (nullopt) para todas as fatias do modelo
    m_cache.assign(m_meta.nSlices, std::nullopt);
    std::cout << "[Collision] " << m_meta.nSlices << " fatias, cache=" << m_nslices << "\n";
}

Slice& CollisionSystem::ensureLoaded(int index) const {
    index = std::clamp(index, 0, m_meta.nSlices - 1);
    
    // Se a fatia ja estiver na RAM (Cache Hit), devolvemos imediatamente
    if (m_cache[index].has_value())
        return *m_cache[index];

    // Cache Miss: Carrega a imagem PNG do disco rigido
    std::string path = m_meta.slicePath(index);
    int w, h, comp;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!data) {
        std::cerr << "[Collision] falha a carregar " << path << "\n";
        m_cache[index] = Slice{};
        return *m_cache[index];
    }

    // Configura a estrutura da fatia e redimensiona o vetor de pixeis
    Slice s;
    s.index = index;
    s.zBase = m_meta.bboxMin.z + index * m_meta.sigma;
    s.sigma = m_meta.sigma;
    s.w = w;
    s.h = h;
    s.pixels.resize((size_t)w * h * 4);
    
    // Inverte o eixo Y da imagem (necessario para alinhar as coordenadas de imagem com o mundo 3D)
    for (int y = 0; y < h; ++y) {
        const stbi_uc* src = data + (size_t)(h - 1 - y) * w * 4;
        std::memcpy(s.pixels.data() + (size_t)y * w * 4, src, (size_t)w * 4);
    }
    stbi_image_free(data);

    m_cache[index] = std::move(s);

    // Gestao de Memoria: Se ultrapassarmos o limite de fatias permitidas na cache,
    // procuramos e removemos a fatia mais distante
    int loaded = 0;
    for (auto& c : m_cache)
        if (c.has_value()) ++loaded;
    if (loaded > m_nslices)
        evictFurthest(index);

    return *m_cache[index];
}

void CollisionSystem::evictFurthest(int playerSlice) const {
    int best = -1;
    int bestDist = -1;
    // Procura na cache a fatia cuja distancia ao jogador (index) seja a maior
    for (int i = 0; i < (int)m_cache.size(); ++i) {
        if (!m_cache[i].has_value()) continue;
        int d = std::abs(i - playerSlice);
        if (d > bestDist) {
            bestDist = d;
            best = i;
        }
    }
    // Remove a fatia escolhida libertando a memoria
    if (best >= 0)
        m_cache[best] = std::nullopt;
}

glm::u8vec4 CollisionSystem::getPixel(int sliceIdx, int px, int py) const {
    auto& s = ensureLoaded(sliceIdx);
    // Devolve transparente se fora dos limites (seguranca)
    if (px < 0 || px >= s.w || py < 0 || py >= s.h)
        return {0, 0, 0, 0};
    int base = (py * s.w + px) * 4;
    return {s.pixels[base], s.pixels[base + 1], s.pixels[base + 2], s.pixels[base + 3]};
}

void CollisionSystem::worldToPixelXY(const glm::vec3& pos, int& outPx, int& outPy) const {
    const float t = m_meta.t;
    // Converte a coordenada continua do mundo para indice discreto do pixel
    outPx = (int)std::floor((pos.x - m_meta.bboxMin.x) / t);
    outPy = (int)std::floor((pos.y - m_meta.bboxMin.y) / t);
}

int CollisionSystem::sliceRangeStart(float pawnFootZ, float pawnMinZ) const {
    // Determina o indice da fatia correspondente a base do jogador
    float z = pawnFootZ + pawnMinZ - m_meta.bboxMin.z;
    return std::clamp((int)std::floor(z / m_meta.sigma), 0, m_meta.nSlices - 1);
}

int CollisionSystem::sliceRangeEnd(float pawnFootZ, float pawnMaxZ) const {
    // Determina o indice da fatia correspondente ao topo do jogador
    float z = pawnFootZ + pawnMaxZ - m_meta.bboxMin.z;
    return std::clamp((int)std::floor(z / m_meta.sigma), 0, m_meta.nSlices - 1);
}

int CollisionSystem::pawnPixW(float pawnSizeX, float t) const {
    return std::max(1, (int)std::ceil(pawnSizeX / t));
}

int CollisionSystem::pawnPixH(float pawnSizeY, float t) const {
    return std::max(1, (int)std::ceil(pawnSizeY / t));
}

bool CollisionSystem::pixelNonBlack(const glm::u8vec4& px) const {
    return px.r > 0 || px.g > 0 || px.b > 0 || px.a > 0;
}

bool CollisionSystem::pixelIsObstacle(const glm::u8vec4& px) const {
    // Obstaculos estao codificados com o canal Azul superior a 50%
    return pixelNonBlack(px) && (px.b / 255.f) > 0.5f;
}

bool CollisionSystem::pixelIsFloor(const glm::u8vec4& px) const {
    // O solo e considerado como qualquer pixel que nao seja obstaculo (Azul <= 50%)
    return pixelNonBlack(px) && (px.b / 255.f) <= 0.5f;
}

bool CollisionSystem::broadPhase(const glm::vec3& pos, const Mesh& mesh,
                                 float pawnHalfX, float pawnHalfY,
                                 float pawnMinZ, float pawnMaxZ) const {
    (void)mesh;
    int cx, cy;
    worldToPixelXY(pos, cx, cy);

    int pixW = pawnPixW(pawnHalfX * 2.f, m_meta.t);
    int pixH = pawnPixH(pawnHalfY * 2.f, m_meta.t);
    int hw = pixW / 2;
    int hh = pixH / 2;

    int s0 = sliceRangeStart(pos.z, pawnMinZ);
    int s1 = sliceRangeEnd(pos.z, pawnMaxZ);
    if (s0 > s1) std::swap(s0, s1);

    int playerSlice = (int)std::floor((pos.z - m_meta.bboxMin.z) / m_meta.sigma);

    // Percorre todas as fatias que o jogador atravessa
    for (int s = s0; s <= s1; ++s) {
        ensureLoaded(s);
        for (int dy = -hh; dy <= hh; ++dy) {
            for (int dx = -hw; dx <= hw; ++dx) {
                // Broad-phase: se encontrar qualquer coisa nao preta, considera colisao global
                if (pixelNonBlack(getPixel(s, cx + dx, cy + dy)))
                    return true;
            }
        }
    }

    int loaded = 0;
    for (auto& c : m_cache)
        if (c.has_value()) ++loaded;
    if (loaded > m_nslices)
        evictFurthest(playerSlice);

    return false;
}

bool CollisionSystem::checkObstacle(const glm::vec3& pos, const Mesh& mesh,
                                    float pawnHalfX, float pawnHalfY,
                                    float pawnMinZ, float pawnMaxZ) const {
    (void)mesh;
    int cx, cy;
    // Converte a posicao real do jogador no mundo para as coordenadas da imagem (Pixeis)
    worldToPixelXY(pos, cx, cy);

    // Determina a largura e altura da area de colisao do jogador em pixeis
    int pixW = pawnPixW(pawnHalfX * 2.f, m_meta.t);
    int pixH = pawnPixH(pawnHalfY * 2.f, m_meta.t);
    int hw = pixW / 2;
    int hh = pixH / 2;

    // Determina quais as fatias afetadas pela altura total do jogador
    int s0 = sliceRangeStart(pos.z, pawnMinZ);
    int s1 = sliceRangeEnd(pos.z, pawnMaxZ);
    if (s0 > s1) std::swap(s0, s1);

    // Avalia apenas a regiao especifica da imagem (Bounding Box) em todas as fatias necessarias
    for (int s = s0; s <= s1; ++s) {
        for (int dy = -hh; dy <= hh; ++dy) {
            for (int dx = -hw; dx <= hw; ++dx) {
                // Se algum pixel tiver o canal azul acima do limiar, deteta obstaculo
                if (pixelIsObstacle(getPixel(s, cx + dx, cy + dy)))
                    return true;
            }
        }
    }
    return false; // Movimento valido
}

CollisionResult CollisionSystem::queryGround(const glm::vec3& pos, const Mesh& mesh,
                                             float pawnHalfX, float pawnHalfY) const {
    (void)mesh;
    CollisionResult res;
    res.groundZ = pos.z;

    int cx, cy;
    worldToPixelXY(pos, cx, cy);

    int pixW = pawnPixW(pawnHalfX * 2.f, m_meta.t);
    int pixH = pawnPixH(pawnHalfY * 2.f, m_meta.t);
    int hw = pixW / 2;
    int hh = pixH / 2;

    // Comeca a procurar o chao a partir da fatia atual do jogador para baixo
    int startSlice = std::clamp(
        (int)std::floor((pos.z - m_meta.bboxMin.z) / m_meta.sigma),
        0, m_meta.nSlices - 1);

    float bestR = -1.f;
    int   bestSlice = -1;

    for (int s = startSlice; s >= 0; --s) {
        bool foundInSlice = false;
        float maxR = 0.f; // Guarda o valor maximo do canal vermelho (ponto mais alto)
        
        // Verifica todos os pixeis debaixo da base do jogador
        for (int dy = -hh; dy <= hh; ++dy) {
            for (int dx = -hw; dx <= hw; ++dx) {
                auto px = getPixel(s, cx + dx, cy + dy);
                if (!pixelIsFloor(px)) continue;
                
                // O canal R [0, 255] representa a altura normalizada [0, 1] dentro da fatia
                float rNorm = px.r / 255.f;
                if (!foundInSlice || rNorm > maxR) {
                    maxR = rNorm;
                    foundInSlice = true;
                }
            }
        }
        if (foundInSlice) {
            bestR = maxR;
            bestSlice = s;
            break; // Chao encontrado nesta fatia, paramos a descida
        }
    }

    if (bestSlice >= 0) {
        // Reconstroi a coordenada Z do mundo: Z da base da fatia + altura relativa
        float zBase = m_meta.bboxMin.z + bestSlice * m_meta.sigma;
        res.groundZ = zBase + bestR * m_meta.sigma;
        res.hasGround = true;
    }
    return res;
}