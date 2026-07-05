#pragma once
#include <string>
#include <glm/glm.hpp>

struct SliceMetadata {
    int       imgW = 0, imgH = 0;
    int       nSlices = 0;
    float     t = 0.f;      // pixel size in object space
    float     sigma = 0.f;  // slice thickness (3t)
    glm::vec3 bboxMin{0.f}, bboxMax{0.f};
    std::string modelPath;
    std::string sliceDir;

    void computeDerived(int w, int h, const glm::vec3& bMin, const glm::vec3& bMax);
    bool save(const std::string& path) const;
    bool load(const std::string& path);
    bool matches(const SliceMetadata& other) const;
    std::string slicePath(int index) const;
};
