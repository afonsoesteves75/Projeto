#include "SliceMetadata.h"
#include <fstream>
#include <iostream>
#include <cmath>

void SliceMetadata::computeDerived(int w, int h,
                                   const glm::vec3& bMin, const glm::vec3& bMax) {
    imgW = w;
    imgH = h;
    bboxMin = bMin;
    bboxMax = bMax;
    // Baseado nas dimensoes da geometria e resolucao, calculamos metricas vitais
    glm::vec3 sz = bMax - bMin;
    float maxXZ = std::max(sz.x, sz.z);
    t = maxXZ / (float)std::max(w, h);
    sigma = 3.f * t;
    nSlices = std::max(1, (int)std::ceil(sz.z / sigma));
}

bool SliceMetadata::save(const std::string& path) const {
    // Guarda o contexto em texto plano para evitar recalculacao de pre-processamento na proxima sessao
    std::ofstream f(path);
    if (!f) return false;
    f << "imgW " << imgW << "\n"
      << "imgH " << imgH << "\n"
      << "t " << t << "\n"
      << "sigma " << sigma << "\n"
      << "nSlices " << nSlices << "\n"
      << "bboxMin " << bboxMin.x << " " << bboxMin.y << " " << bboxMin.z << "\n"
      << "bboxMax " << bboxMax.x << " " << bboxMax.y << " " << bboxMax.z << "\n"
      << "modelPath " << modelPath << "\n"
      << "sliceDir " << sliceDir << "\n";
    return true;
}

bool SliceMetadata::load(const std::string& path) {
    // Restaura o ambiente base a partir da ultima sessao gerada
    std::ifstream f(path);
    if (!f) return false;
    std::string key;
    while (f >> key) {
        if (key == "imgW")       f >> imgW;
        else if (key == "imgH")  f >> imgH;
        else if (key == "t")     f >> t;
        else if (key == "sigma") f >> sigma;
        else if (key == "nSlices") f >> nSlices;
        else if (key == "bboxMin") f >> bboxMin.x >> bboxMin.y >> bboxMin.z;
        else if (key == "bboxMax") f >> bboxMax.x >> bboxMax.y >> bboxMax.z;
        else if (key == "modelPath") {
            std::getline(f >> std::ws, modelPath);
        } else if (key == "sliceDir") {
            std::getline(f >> std::ws, sliceDir);
        }
    }
    return imgW > 0 && imgH > 0 && nSlices > 0 && sigma > 0.f;
}

bool SliceMetadata::matches(const SliceMetadata& o) const {
    // Verifica se os metadados diferem (novo modelo ou nova resolucao escolhida)
    return imgW == o.imgW && imgH == o.imgH
        && modelPath == o.modelPath
        && std::abs(t - o.t) < 1e-5f
        && std::abs(sigma - o.sigma) < 1e-5f;
}

std::string SliceMetadata::slicePath(int index) const {
    return sliceDir + "/slice_" + std::to_string(index) + ".png";
}