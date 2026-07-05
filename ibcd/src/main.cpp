#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <string>
#include <fstream>
#include <cstdint>

#include "OBJLoader.h"
#include "Oversampler.h"
#include "Slicer.h"
#include "CollisionSystem.h"
#include "Player.h"
#include "SliceMetadata.h"

// Definicoes e parametros de inicializacao da cena
static const int   IMG_W      = 350;
static const int   IMG_H      = 350;
static const int   WIN_W      = 1024;
static const int   WIN_H      = 768;
static const int   NSLICES    = 12; // Limite maximo de Cache das Fatias
static const char* SLICE_DIR  = "output/slices";

static std::string gRoot      = ".";
static std::string gSliceDir  = SLICE_DIR;
static std::string gModelPath = "assets/scene.obj";

// Encontra a diretoria raiz onde se localizam os assets 3D independentemente do binario gerado
static std::string findProjectRoot() {
    for (const char* prefix : {".", "..", "../.."}) {
        if (std::filesystem::exists(std::string(prefix) + "/assets/scene.obj"))
            return prefix;
    }
    return ".";
}

static std::string resolvePath(const std::string& rel) {
    if (rel.empty() || rel.front() == '/')
        return rel;
    if (gRoot == ".")
        return rel;
    return gRoot + "/" + rel;
}

// Shader Programs para desenhar debug lines, pontos codificados, e renderizar quads 2D
static const char* LINE_VS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos, 1.0); }
)";

static const char* LINE_FS = R"(
#version 330 core
uniform vec4 uColor;
out vec4 FragColor;
void main(){ FragColor = uColor; }
)";

static const char* POINT_VS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uMVP;
out vec4 vColor;
void main(){
    gl_Position = uMVP * vec4(aPos, 1.0);
    gl_PointSize = 3.0;
    vColor = aColor;
}
)";

static const char* POINT_FS = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main(){ FragColor = vColor; }
)";

static const char* QUAD_VS = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){ gl_Position=vec4(aPos,0,1); vUV=aUV; }
)";

// Desenha fatias descodificando a cor em cores visiveis (Azul para parede, Vermelho/Verde para solo)
static const char* QUAD_FS = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
void main(){
    vec4 c = texture(uTex, vUV);
    if(c.r < 0.001 && c.g < 0.001 && c.b < 0.001)
        FragColor = vec4(0.08, 0.08, 0.08, 0.85);
    else {
        float isWall = step(0.5, c.b);
        vec3 col = mix(vec3(c.r*4.0, 0.6+c.r*0.4, 0.1), vec3(0.9,0.2,0.2), isWall);
        FragColor = vec4(col, 0.9);
    }
}
)";

static bool keys[GLFW_KEY_LAST + 1] = {};
static bool showSliceView = false;
static int  currentSlice  = 0;
static float gCamYaw   = 45.f;
static float gCamElev  = 32.f;
static float gCamDist  = 12.f;

static void scrollCB(GLFWwindow*, double, double yoff) {
    gCamDist = std::clamp(gCamDist - (float)yoff * 0.8f, 3.f, 80.f);
}

static void keyCB(GLFWwindow*, int key, int, int action, int) {
    if (key >= 0 && key <= GLFW_KEY_LAST) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT) keys[key] = true;
        if (action == GLFW_RELEASE) keys[key] = false;
    }
}

// Cria wireframe da Bounding Box correspondente a regiao do jogador
static void buildPlayerBoxLines(const Player& p, std::vector<glm::vec3>& out) {
    glm::vec3 mn(p.pos.x - p.halfX, p.pos.y - p.halfY, p.pos.z);
    glm::vec3 mx(p.pos.x + p.halfX, p.pos.y + p.halfY, p.pos.z + p.maxZ);
    glm::vec3 c[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
    };
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7},
    };
    out.clear();
    out.reserve(24);
    for (auto& e : edges) {
        out.push_back(c[e[0]]);
        out.push_back(c[e[1]]);
    }
}

static glm::mat4 computeView(const Player& player, const Mesh& mesh) {
    glm::vec3 target = player.pos + glm::vec3(0.f, 0.f, player.maxZ * 0.55f);

    float ce = std::cos(glm::radians(gCamElev));
    float se = std::sin(glm::radians(gCamElev));
    float cy = std::cos(glm::radians(gCamYaw));
    float sy = std::sin(glm::radians(gCamYaw));

    // Z = altura: elevacao positiva coloca a camara acima do alvo do jogador (3a pessoa isolada)
    glm::vec3 eye = target + glm::vec3(
        gCamDist * ce * cy,
        gCamDist * ce * sy,
        gCamDist * se
    );
    eye.z = std::max(eye.z, mesh.bboxMin.z + 1.5f);

    return glm::lookAt(eye, target, glm::vec3(0.f, 0.f, 1.f));
}

static unsigned int mkShader(unsigned int type, const char* src) {
    unsigned int id = glCreateShader(type);
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);
    int ok;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char b[512];
        glGetShaderInfoLog(id, 512, nullptr, b);
        std::cerr << "[Shader] " << b << "\n";
    }
    return id;
}

static unsigned int mkProg(const char* vs, const char* fs) {
    unsigned int p = glCreateProgram();
    unsigned int a = mkShader(GL_VERTEX_SHADER, vs);
    unsigned int b = mkShader(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, a);
    glAttachShader(p, b);
    glLinkProgram(p);
    glDeleteShader(a);
    glDeleteShader(b);
    return p;
}

static Mesh loadModel(const std::string& path, int w, int h) {
    std::string ext = path.substr(path.find_last_of('.') + 1);
    for (auto& c : ext) c = (char)std::tolower(c);
    // Delega no Loader apropriado segundo extensao (Modelo vs Nuvem)
    if (ext == "ply" || ext == "xyz" || ext == "pts")
        return loadPointCloud(path, w, h);
    return loadOBJ(path, w, h);
}

// Controla a invalidez de cache do metadata comparando conf com disco
static bool preprocessNeeded(const SliceMetadata& cached) {
    if (!std::filesystem::exists(cached.sliceDir + "/metadata.txt"))
        return true;
    SliceMetadata existing;
    if (!existing.load(cached.sliceDir + "/metadata.txt"))
        return true;
    if (!existing.matches(cached))
        return true;
    for (int i = 0; i < existing.nSlices; ++i) {
        if (!std::filesystem::exists(existing.slicePath(i)))
            return true;
    }
    return false;
}

static SliceMetadata buildMeta(const Mesh& mesh, const std::string& modelPath) {
    SliceMetadata meta;
    meta.computeDerived(mesh.imgW, mesh.imgH, mesh.bboxMin, mesh.bboxMax);
    meta.modelPath = modelPath;
    meta.sliceDir = gSliceDir;
    meta.t = mesh.pixelSize;
    meta.sigma = mesh.sigma;
    meta.nSlices = std::max(1, (int)std::ceil((mesh.bboxMax.z - mesh.bboxMin.z) / meta.sigma));
    return meta;
}

// Mecanismo de Cache estatico de pontos - acelera inicializacoes futuras
static void savePointsCache(const std::vector<ColoredPoint>& pts, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return;
    uint64_t n = pts.size();
    f.write(reinterpret_cast<const char*>(&n), sizeof(n));
    f.write(reinterpret_cast<const char*>(pts.data()), n * sizeof(ColoredPoint));
}

static bool loadPointsCache(const std::string& path, std::vector<ColoredPoint>& pts) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint64_t n = 0;
    f.read(reinterpret_cast<char*>(&n), sizeof(n));
    if (n == 0 || n > 500000000ull) return false;
    pts.resize((size_t)n);
    f.read(reinterpret_cast<char*>(pts.data()), n * sizeof(ColoredPoint));
    return f.good();
}

int main(int argc, char** argv) {
    gRoot = findProjectRoot();
    gSliceDir = resolvePath(SLICE_DIR);
    if (argc > 1)
        gModelPath = argv[1];
    else
        gModelPath = resolvePath("assets/scene.obj");

    std::cout << "[Main] root=" << gRoot << " modelo=" << gModelPath << "\n";

    // Setup Basico de Janela
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(WIN_W, WIN_H, "IBCD", nullptr, nullptr);
    if (!win) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    glfwSetKeyCallback(win, keyCB);
    glfwSetScrollCallback(win, scrollCB);
    glfwSwapInterval(1);

    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) return -1;
    std::cout << "OpenGL " << glGetString(GL_VERSION) << "\n";

    // Modulo 1: Carga Geometrica
    Mesh mesh = loadModel(gModelPath, IMG_W, IMG_H);
    if (mesh.vertices.empty() && mesh.triangles.empty()) {
        glfwTerminate();
        return -1;
    }

    SliceMetadata meta = buildMeta(mesh, gModelPath);
    std::vector<ColoredPoint> points;
    const std::string pointsPath = gSliceDir + "/points.bin";

    // Modulo 2: Pre-processamento dinamico baseado em necessidade
    if (preprocessNeeded(meta)) {
        std::cout << "[Main] Pre-processamento...\n";
        points = oversample(mesh); // Transforma poligonos em pontos continuos e calcula espessuras

        Slicer slicer(IMG_W, IMG_H);
        if (!slicer.init()) return -1;
        slicer.uploadPoints(points); // Passa para a GPU
        auto slices = slicer.slice(mesh); // Fatiamento Off-screen do cenario
        slicer.savePNGs(slices, gSliceDir);
        meta.save(gSliceDir + "/metadata.txt");
        savePointsCache(points, pointsPath);
    } else {
        std::cout << "[Main] Slices em cache, a saltar pre-processamento\n";
        meta.load(gSliceDir + "/metadata.txt");
        mesh.pixelSize = meta.t;
        mesh.sigma = meta.sigma;
        mesh.imgW = meta.imgW;
        mesh.imgH = meta.imgH;
        mesh.bboxMin = meta.bboxMin;
        mesh.bboxMax = meta.bboxMax;
        if (!loadPointsCache(pointsPath, points))
            points = oversample(mesh);
    }

    // Inicializa o Sistema logico de Colisoes e o Player
    CollisionSystem col;
    col.init(meta, NSLICES);

    glm::vec3 center = (mesh.bboxMin + mesh.bboxMax) * 0.5f;
    Player player(glm::vec3(
        5.0f,
        3.0f,
        mesh.bboxMin.z + mesh.sigma * 0.5f
    ));
    player.configureFromMesh(mesh);
    {
        // Forca um Query inicial a gravidade
        auto g = col.queryGround(player.pos, mesh, player.halfX, player.halfY);
        if (g.hasGround) player.pos.z = g.groundZ;
    }

    float sceneSpan = std::max(mesh.bboxMax.x - mesh.bboxMin.x,
                               mesh.bboxMax.y - mesh.bboxMin.y);
    gCamDist = sceneSpan * 0.9f;

    // Buffer da nuvem de pontos visual
    unsigned int ptVao = 0, ptVbo = 0, ptProg = 0;
    if (!points.empty()) {
        ptProg = mkProg(POINT_VS, POINT_FS);
        glGenVertexArrays(1, &ptVao);
        glGenBuffers(1, &ptVbo);
        glBindVertexArray(ptVao);
        glBindBuffer(GL_ARRAY_BUFFER, ptVbo);
        glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(ColoredPoint),
                     points.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ColoredPoint),
                              (void*)offsetof(ColoredPoint, pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ColoredPoint),
                              (void*)offsetof(ColoredPoint, r));
        glBindVertexArray(0);
    }

    // Buffer visual do ecra dividido logico
    float quadV[] = {-1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,1,0,1};
    unsigned int qi[] = {0,1,2,2,3,0};
    unsigned int qvao, qvbo, qebo, quadProg, sliceTex = 0;
    glGenVertexArrays(1, &qvao);
    glGenBuffers(1, &qvbo);
    glGenBuffers(1, &qebo);
    glBindVertexArray(qvao);
    glBindBuffer(GL_ARRAY_BUFFER, qvbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadV), quadV, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, qebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(qi), qi, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);
    glBindVertexArray(0);
    quadProg = mkProg(QUAD_VS, QUAD_FS);
    glGenTextures(1, &sliceTex);

    unsigned int lineVao = 0, lineVbo = 0, lineProg = 0;
    lineProg = mkProg(LINE_VS, LINE_FS);
    glGenVertexArrays(1, &lineVao);
    glGenBuffers(1, &lineVbo);
    glBindVertexArray(lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo);
    glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glBindVertexArray(0);

    std::vector<glm::vec3> playerLines;
    playerLines.reserve(24);

    // Main Run Loop
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (keys[GLFW_KEY_ESCAPE]) glfwSetWindowShouldClose(win, true);
        if (keys[GLFW_KEY_V]) { showSliceView = !showSliceView; keys[GLFW_KEY_V] = false; }
        if (keys[GLFW_KEY_PAGE_UP]) {
            currentSlice = std::min(currentSlice + 1, meta.nSlices - 1);
            keys[GLFW_KEY_PAGE_UP] = false;
        }
        if (keys[GLFW_KEY_PAGE_DOWN]) {
            currentSlice = std::max(currentSlice - 1, 0);
            keys[GLFW_KEY_PAGE_DOWN] = false;
        }
        if (keys[GLFW_KEY_Q]) gCamYaw -= 2.f;
        if (keys[GLFW_KEY_E]) gCamYaw += 2.f;

        float dx = 0, dy = 0;
        if (keys[GLFW_KEY_W] || keys[GLFW_KEY_UP]) dy += 1.f;
        if (keys[GLFW_KEY_S] || keys[GLFW_KEY_DOWN]) dy -= 1.f;
        if (keys[GLFW_KEY_A] || keys[GLFW_KEY_LEFT]) dx -= 1.f;
        if (keys[GLFW_KEY_D] || keys[GLFW_KEY_RIGHT]) dx += 1.f;

        // Efetua a logica central de atualizacao do avatar (Colisoes via Pixels e Resposta Gravitacional)
        player.update(dx, dy, col, mesh);

        // Atualiza a representacao da fatia debug conforme a elevacao
        if (!showSliceView) {
            currentSlice = std::clamp(
                (int)std::floor((player.pos.z - meta.bboxMin.z) / meta.sigma),
                0, meta.nSlices - 1);
        }

        int fw, fh;
        glfwGetFramebufferSize(win, &fw, &fh);
        glViewport(0, 0, fw, fh);
        glClearColor(0.12f, 0.14f, 0.18f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        // Prepara e aplica transformacoes MVP da Camara
        glm::mat4 proj = glm::perspective(glm::radians(55.f), (float)fw / (float)fh, 0.1f, 500.f);
        glm::mat4 view = computeView(player, mesh);
        glm::mat4 mvp  = proj * view;

        if (ptVao) {
            glUseProgram(ptProg);
            glUniformMatrix4fv(glGetUniformLocation(ptProg, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
            glBindVertexArray(ptVao);
            glDrawArrays(GL_POINTS, 0, (GLsizei)points.size());
        }

        buildPlayerBoxLines(player, playerLines);
        glLineWidth(2.5f);
        glBindBuffer(GL_ARRAY_BUFFER, lineVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, playerLines.size() * sizeof(glm::vec3), playerLines.data());
        glUseProgram(lineProg);
        glUniformMatrix4fv(glGetUniformLocation(lineProg, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform4f(glGetUniformLocation(lineProg, "uColor"), 1.f, 0.85f, 0.f, 1.f);
        glBindVertexArray(lineVao);
        glDrawArrays(GL_LINES, 0, (GLsizei)playerLines.size());
        glLineWidth(1.f);

        // Representacao Mini-mapa (Injecao de fatia RGB no display inferior direito do ecra)
        if (showSliceView) {
            const Slice& sl = col.getSlice(currentSlice);
            glDisable(GL_DEPTH_TEST);
            glViewport(fw / 2, fh / 2, fw / 2, fh / 2);
            glBindTexture(GL_TEXTURE_2D, sliceTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sl.w, sl.h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, sl.pixels.data());
            glUseProgram(quadProg);
            glUniform1i(glGetUniformLocation(quadProg, "uTex"), 0);
            glBindVertexArray(qvao);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            glEnable(GL_DEPTH_TEST);
        }

        // GUI Metrics 
        char title[320];
        snprintf(title, sizeof(title),
                 "IBCD | pos=(%.2f,%.2f,%.2f) step=%.3f | Q/E=rodar scroll=zoom | ESC=sair",
                 player.pos.x, player.pos.y, player.pos.z, player.currentStep);
        glfwSetWindowTitle(win, title);
        glfwSwapBuffers(win);
    }

    // Clean Exit Memory Release
    glDeleteVertexArrays(1, &ptVao);
    glDeleteBuffers(1, &ptVbo);
    glDeleteProgram(lineProg);
    glDeleteVertexArrays(1, &lineVao);
    glDeleteBuffers(1, &lineVbo);
    glDeleteProgram(ptProg);
    glDeleteVertexArrays(1, &qvao);
    glDeleteBuffers(1, &qvbo);
    glDeleteBuffers(1, &qebo);
    glDeleteTextures(1, &sliceTex);
    glDeleteProgram(quadProg);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}