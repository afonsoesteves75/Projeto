#pragma once
#include <vector>
#include <string>
#include "OBJLoader.h"
#include "Oversampler.h"
#include "SliceMetadata.h"

struct Slice {
    int   index;
    float zBase, sigma;
    int   w, h;
    std::vector<uint8_t> pixels;
};

class Slicer {
public:
    Slicer(int w, int h);
    ~Slicer();
    bool init();
    void uploadPoints(const std::vector<ColoredPoint>& pts);
    std::vector<Slice> slice(const Mesh& mesh);
    void savePNGs(const std::vector<Slice>& slices, const std::string& dir);
    static bool loadMetadata(const std::string& dir, SliceMetadata& out);

private:
    int          m_w, m_h;
    unsigned int m_fbo = 0, m_tex = 0, m_rbo = 0;
    unsigned int m_vao = 0, m_vbo = 0;
    unsigned int m_prog = 0;
    int          m_npts = 0;
    unsigned int compileShader(unsigned int type, const char* src);
};
