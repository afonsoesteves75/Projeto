#include "OBJLoader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <limits>
#include <cmath>
#include <algorithm>

static void updateBBox(Mesh& m, const glm::vec3& v) {
    // Expande a 'Caixa Delimitadora' (Bounding Box) englobando o novo vertice
    m.bboxMin = glm::min(m.bboxMin, v);
    m.bboxMax = glm::max(m.bboxMax, v);
}

static void finalizeMesh(Mesh& m, int imgW, int imgH) {
    m.imgW = imgW;
    m.imgH = imgH;
    glm::vec3 sz = m.bboxMax - m.bboxMin;
    float maxXZ = std::max(sz.x, sz.z);
    // Determina o tamanho teorico do pixel no espaco do mundo para alinhar a resolucao da imagem
    m.pixelSize = maxXZ / (float)std::max(imgW, imgH);
    // Espessura da fatia, garantindo sobreposicao logica para os obstaculos
    m.sigma = 3.f * m.pixelSize; 
}

static int parseIdx(const std::string& tok) {
    // Isola o indice do vertice quebrando por '/' (evita Normais e Texcoords)
    return std::stoi(tok.substr(0, tok.find('/')));
}

Mesh loadOBJ(const std::string& path, int imgW, int imgH) {
    Mesh m;
    m.isPointCloud = false;
    m.bboxMin = glm::vec3( 1e30f);
    m.bboxMax = glm::vec3(-1e30f);

    std::ifstream f(path);
    if (!f) { std::cerr << "[OBJ] nao abriu: " << path << "\n"; return m; }

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tok;
        ss >> tok;

        // Recolhe Vertices (v x y z)
        if (tok == "v") {
            glm::vec3 v;
            ss >> v.x >> v.y >> v.z;
            m.vertices.push_back(v);
            updateBBox(m, v);
        } 
        // Recolhe Faces Poligonais (f v1 v2 v3) convertendo em triangulos unicos
        else if (tok == "f") {
            std::vector<int> idx;
            std::string t;
            while (ss >> t) idx.push_back(parseIdx(t) - 1); // 0-indexing
            for (size_t i = 1; i + 1 < idx.size(); ++i)
                m.triangles.push_back({m.vertices[idx[0]],
                                       m.vertices[idx[i]],
                                       m.vertices[idx[i + 1]]});
        }
    }

    finalizeMesh(m, imgW, imgH);
    std::cout << "[OBJ] " << m.vertices.size() << " vertices, "
              << m.triangles.size() << " triangulos | t=" << m.pixelSize
              << " sigma=" << m.sigma << "\n";
    return m;
}

Mesh loadPointCloud(const std::string& path, int imgW, int imgH) {
    Mesh m;
    m.isPointCloud = true;
    m.bboxMin = glm::vec3( 1e30f);
    m.bboxMax = glm::vec3(-1e30f);

    std::ifstream f(path);
    if (!f) { std::cerr << "[PC] nao abriu: " << path << "\n"; return m; }

    std::string line;
    bool plyHeader = false;
    int plyVerts = 0;
    int plyRead = 0;

    // Leitura generica ou suportada por standard PLY Header
    while (std::getline(f, line)) {
        if (line.empty()) continue;

        if (!plyHeader) {
            if (line == "ply") { plyHeader = true; continue; } // Formato standard PLY
            std::istringstream ss(line);
            float x, y, z;
            if (ss >> x >> y >> z) {
                glm::vec3 v(x, y, z);
                m.vertices.push_back(v);
                updateBBox(m, v);
            }
            continue;
        }

        if (line.find("element vertex") != std::string::npos) {
            std::istringstream ss(line);
            std::string a, b;
            ss >> a >> b >> plyVerts;
        } else if (line == "end_header") {
            for (int i = 0; i < plyVerts; ++i) {
                std::getline(f, line);
                std::istringstream ss(line);
                float x, y, z;
                if (ss >> x >> y >> z) {
                    glm::vec3 v(x, y, z);
                    m.vertices.push_back(v);
                    updateBBox(m, v);
                    ++plyRead;
                }
            }
            break;
        }
    }

    if (plyHeader && plyRead == 0 && m.vertices.empty()) {
        std::cerr << "[PC] PLY vazio ou invalido\n";
        return m;
    }

    finalizeMesh(m, imgW, imgH);
    std::cout << "[PC] " << m.vertices.size() << " pontos | t=" << m.pixelSize
              << " sigma=" << m.sigma << "\n";
    return m;
}