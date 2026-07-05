#include "Slicer.h"
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <fstream>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../external/stb_image_write.h"

// Vertex Shader: projeta a geometria de forma ortografica 
// Passa a cor codificada (altura e obstaculo) diretamente para o Fragment Shader
static const char* VS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
out vec4 vColor;
uniform mat4 uOrtho;
void main(){
    gl_Position  = uOrtho * vec4(aPos,1.0);
    gl_PointSize = 1.0;
    vColor       = aColor;
}
)";

// Fragment Shader: Simplesmente define a cor do pixel
static const char* FS = R"(
#version 330 core
in  vec4 vColor;
out vec4 FragColor;
void main(){ FragColor = vColor; }
)";

Slicer::Slicer(int w, int h) : m_w(w), m_h(h) {}

Slicer::~Slicer() {
    // Limpeza de recursos da placa grafica (VRAM)
    if (m_fbo)  glDeleteFramebuffers(1, &m_fbo);
    if (m_tex)  glDeleteTextures(1, &m_tex);
    if (m_rbo)  glDeleteRenderbuffers(1, &m_rbo);
    if (m_vao)  glDeleteVertexArrays(1, &m_vao);
    if (m_vbo)  glDeleteBuffers(1, &m_vbo);
    if (m_prog) glDeleteProgram(m_prog);
}

unsigned int Slicer::compileShader(unsigned int type, const char* src) {
    unsigned int id = glCreateShader(type);
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);
    int ok;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(id, 512, nullptr, buf);
        std::cerr << "[Slicer shader] " << buf << "\n";
    }
    return id;
}

bool Slicer::init() {
    // Cria um Framebuffer Object (FBO) para fazer renderizacao escondida (off-screen)
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // Cria a textura que serve de 'tela' para a camara ortografica
    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_w, m_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);

    // Cria um Renderbuffer para lidar com testes de profundidade (se necessarios)
    glGenRenderbuffers(1, &m_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_w, m_h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[Slicer] FBO incompleto!\n";
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Configura o VAO e VBO associando as coordenadas (XYZ) e a cor (RGBA)
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ColoredPoint),
                          (void*)offsetof(ColoredPoint, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ColoredPoint),
                          (void*)offsetof(ColoredPoint, r));
    glBindVertexArray(0);

    // Compila e junta os shaders
    unsigned int vs = compileShader(GL_VERTEX_SHADER, VS);
    unsigned int fs = compileShader(GL_FRAGMENT_SHADER, FS);
    m_prog = glCreateProgram();
    glAttachShader(m_prog, vs);
    glAttachShader(m_prog, fs);
    glLinkProgram(m_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glEnable(GL_PROGRAM_POINT_SIZE);
    std::cout << "[Slicer] init OK (" << m_w << "x" << m_h << ")\n";
    return true;
}

void Slicer::uploadPoints(const std::vector<ColoredPoint>& pts) {
    // Transfere os pontos pre-processados da RAM para a VRAM (Memoria Grafica)
    m_npts = (int)pts.size();
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, pts.size() * sizeof(ColoredPoint), pts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    std::cout << "[Slicer] " << m_npts << " pontos na GPU\n";
}

std::vector<Slice> Slicer::slice(const Mesh& mesh) {
    const float zMin  = mesh.bboxMin.z;
    const float zMax  = mesh.bboxMax.z;
    const float sigma = mesh.sigma; // Espessura da fatia
    const float xMin  = mesh.bboxMin.x, xMax = mesh.bboxMax.x;
    const float yMin  = mesh.bboxMin.y, yMax = mesh.bboxMax.y;

    int nSlices = std::max(1, (int)std::ceil((zMax - zMin) / sigma));
    std::vector<Slice> slices;
    slices.reserve(nSlices);

    // Ativa o ambiente off-screen
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_w, m_h);
    glUseProgram(m_prog);
    glBindVertexArray(m_vao);
    
    // Desativamos o teste de profundidade porque a oclusao e tratada pela geracao de cores no Oversampler
    glDisable(GL_DEPTH_TEST);

    int uLoc = glGetUniformLocation(m_prog, "uOrtho");

    // Varre o modelo de baixo para cima, cortando fatias horizontais sucessivas
    for (int i = 0; i < nSlices; ++i) {
        float zBase = zMin + i * sigma;

        // Limita a projecao ortografica estritamente entre 0 e a espessura 'sigma'
        glm::mat4 proj = glm::ortho(xMin, xMax, yMin, yMax, 0.0f, sigma);
        // Coloca a "camara" no nivel da base da fatia iterada
        glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.f, 0.f, -zBase));
        glm::mat4 ortho = proj * view;
        glUniformMatrix4fv(uLoc, 1, GL_FALSE, glm::value_ptr(ortho));

        glClearColor(0, 0, 0, 0); // O vazio e representado por pixeis transparentes (preto)
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_POINTS, 0, m_npts);

        // Transfere o resultado desta fatia da memoria da GPU de volta para a RAM (CPU)
        Slice s;
        s.index = i;
        s.zBase = zBase;
        s.sigma = sigma;
        s.w = m_w;
        s.h = m_h;
        s.pixels.resize((size_t)m_w * m_h * 4);
        glReadPixels(0, 0, m_w, m_h, GL_RGBA, GL_UNSIGNED_BYTE, s.pixels.data());
        slices.push_back(std::move(s));
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::cout << "[Slicer] " << nSlices << " fatias (sigma=" << sigma << ")\n";
    return slices;
}

void Slicer::savePNGs(const std::vector<Slice>& slices, const std::string& dir) {
    std::filesystem::create_directories(dir);
    // Inverte a imagem verticalmente para compensar o referencial do OpenGL na hora de gravar
    stbi_flip_vertically_on_write(1);
    for (auto& s : slices) {
        std::string p = dir + "/slice_" + std::to_string(s.index) + ".png";
        stbi_write_png(p.c_str(), s.w, s.h, 4, s.pixels.data(), s.w * 4);
    }
    std::cout << "[Slicer] " << slices.size() << " PNGs em " << dir << "\n";
}

bool Slicer::loadMetadata(const std::string& dir, SliceMetadata& out) {
    return out.load(dir + "/metadata.txt");
}