#include "Oversampler.h"
#include <cmath>
#include <iostream>
#include <vector>
#include <algorithm>

static const float EPSILON  = 0.05f;
static const float EPSILON2 = 0.001f;

static void sampleEdge(const glm::vec3& a, const glm::vec3& b,
                       float t, std::vector<glm::vec3>& out) {
    // Interpola pontos de forma uniforme (espacamento 't') ao longo de uma aresta
    float len = glm::length(b - a);
    if (len <= t) {
        out.push_back(a);
        return;
    }
    int n = (int)std::ceil(len / t);
    for (int i = 0; i <= n; ++i)
        out.push_back(a + (float)i / (float)n * (b - a));
}

static void sampleTriangle(const Triangle& tri, float t,
                           std::vector<glm::vec3>& out) {
    // Mede as 3 arestas do triangulo
    float d01 = glm::length(tri.v1 - tri.v0);
    float d02 = glm::length(tri.v2 - tri.v0);
    float d12 = glm::length(tri.v2 - tri.v1);
    
    // Se o triangulo for mais pequeno que 1 pixel 't', e um ponto unico
    if (d01 <= t && d02 <= t && d12 <= t) {
        out.push_back(tri.v0);
        out.push_back(tri.v1);
        out.push_back(tri.v2);
        return;
    }

    int n1 = std::max(1, (int)std::ceil(d01 / t));
    int n2 = std::max(1, (int)std::ceil(d02 / t));
    int n3 = std::max(1, (int)std::ceil(d12 / t));
    (void)n3;

    // Discretiza os bordos
    std::vector<glm::vec3> abPts, acPts;
    for (int i = 0; i <= n1; ++i)
        abPts.push_back(tri.v0 + (float)i / (float)n1 * (tri.v1 - tri.v0));
    for (int i = 0; i <= n2; ++i)
        acPts.push_back(tri.v0 + (float)i / (float)n2 * (tri.v2 - tri.v0));

    // Liga os bordos entre si criando linhas interiores transversais (Rasterizacao manual do triangulo)
    int maxI = std::max((int)abPts.size(), (int)acPts.size());
    for (int i = 0; i < maxI; ++i) {
        glm::vec3 abi = abPts[std::min(i, (int)abPts.size() - 1)];
        glm::vec3 aci = acPts[std::min(i, (int)acPts.size() - 1)];
        sampleEdge(abi, aci, t, out);
    }

    // Segunda passagem para garantir que a superificie fica densa e sem buracos noutra direcao
    int nx;
    glm::vec3 xPt, yPt;
    if (n1 >= n2) { nx = n1; xPt = tri.v1; yPt = tri.v2; }
    else          { nx = n2; xPt = tri.v2; yPt = tri.v1; }

    for (int i = 0; i <= nx; ++i) {
        glm::vec3 axi = tri.v0 + (float)i / (float)nx * (xPt - tri.v0);
        glm::vec3 xyi = xPt + (float)i / (float)nx * (yPt - xPt);
        sampleEdge(axi, xyi, t, out);
    }
}

static std::vector<glm::vec3> generatePoints(const Mesh& mesh) {
    // Se a geometria ja for uma nuvem de pontos, basta devolve-la
    if (mesh.isPointCloud)
        return mesh.vertices;

    std::vector<glm::vec3> raw;
    raw.reserve(mesh.triangles.size() * 30);
    // Transforma cada triangulo numa nuvem densa de pontos
    for (auto& tri : mesh.triangles)
        sampleTriangle(tri, mesh.pixelSize, raw);
    return raw;
}

static std::vector<ColoredPoint> encodePoints(const Mesh& mesh,
                                              std::vector<glm::vec3> points) {
    const float sigma = mesh.sigma;
    const float zMin  = mesh.bboxMin.z;
    const int   w     = mesh.imgW;
    const int   h     = mesh.imgH;
    const float t     = mesh.pixelSize;
    const int   nSl   = std::max(1, (int)std::ceil((mesh.bboxMax.z - zMin) / sigma));

    // Cria um cubo 3D temporario para sabermos que pontos cairao no mesmo pixel da mesma fatia
    std::vector<float> cube((size_t)w * h * nSl, -1.f);

    auto cubeIdx = [&](int x, int y, int s) -> size_t {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        s = std::clamp(s, 0, nSl - 1);
        return (size_t)s * w * h + (size_t)y * w + x;
    };

    std::vector<ColoredPoint> result;
    result.reserve(points.size());

    for (auto& p : points) {
        // Encontra o indice no "voxel"
        int xs = (int)std::floor((p.x - mesh.bboxMin.x) / t);
        int ys = (int)std::floor((p.y - mesh.bboxMin.y) / t);
        int s  = (int)std::floor(std::abs(p.z - zMin) / sigma);

        // A cor Vermelha dita a altura real dentro do intervalo 'sigma' (percentagem 0.0 a 1.0)
        float relZ = std::fmod(std::abs(p.z - zMin), sigma);
        if (relZ < 0.f) relZ += sigma;
        float red = relZ / sigma;

        size_t idx = cubeIdx(xs, ys, s);
        float redOld = cube[idx];

        float bVal = 0.1f; // Nao e obstaculo por defeito
        if (redOld >= 0.f) {
            float diff = std::abs(redOld - red) * sigma;
            // Se existirem dois pontos alinhados verticalmente no mesmo pixel da mesma fatia
            // com uma grande diferenca de altura, e classificado como parede/obstaculo
            if (diff > sigma * EPSILON) {
                cube[idx] = 1.f;
                bVal = 1.f; // Canal Azul elevado = Obstaculo marcado
                if (redOld > red)
                    p.z += (redOld - red) * sigma + EPSILON2; // Ajuste para prevenir oclusao visual
            } else {
                cube[idx] = red;
            }
        } else {
            cube[idx] = red; // Regista a primeira altura vista
        }

        // Ponto RGB: (Vermelho = altura Z, Azul = obstaculo ou solo)
        result.push_back({p, red, 1.f, bVal, 1.f});
    }

    return result;
}

std::vector<ColoredPoint> oversample(const Mesh& mesh) {
    auto raw = generatePoints(mesh);
    std::cout << "[Oversampler] " << raw.size() << " pontos gerados\n";
    auto colored = encodePoints(mesh, std::move(raw));
    std::cout << "[Oversampler] " << colored.size() << " pontos codificados\n";
    return colored;
}

std::vector<ColoredPoint> colorPointCloud(const Mesh& mesh) {
    return oversample(mesh);
}