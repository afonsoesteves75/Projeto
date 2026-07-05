# Image-Based Collision Detection (IBCD)

Implementação de um sistema de deteção de colisões baseado em imagens, desenvolvido em **C++17** com **OpenGL 3.3**, **GLFW**, **GLAD2** e **GLM**.

## Dependências

- C++17
- CMake (>= 3.16)
- OpenGL 3.3
- GLFW
- GLAD2
- GLM

## Compilação

### Linux

```bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### Windows

```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

O executável é gerado na raiz do projeto (`ibcd` ou `ibcd.exe`).

## Execução

Executar a cena de teste:

```bash
./ibcd
```

Executar um modelo específico:

```bash
./ibcd caminho/para/modelo.obj
./ibcd caminho/para/modelo.ply
```

## Controlos

| Tecla | Função |
|--------|--------|
| **W/A/S/D** ou setas | Mover o jogador |
| **Q / E** | Rodar a câmara |
| **Roda do rato** | Zoom |
| **Esc** | Sair |

## Estrutura do projeto

```
src/
 ├── OBJLoader         Carregamento de modelos
 ├── Oversampler       Oversampling da geometria
 ├── Slicer            Geração das slices
 ├── CollisionSystem   Sistema de colisões
 ├── Player            Movimento do jogador
 ├── SliceMetadata     Metadados das slices
 └── main.cpp          Aplicação principal

assets/
 └── scene.obj         Modelo de teste

output/
 └── slices/           Slices e ficheiros de cache
```

## Cache

As slices geradas são armazenadas em `output/slices/`.

Se o modelo e os parâmetros não forem alterados, o pré-processamento é reutilizado automaticamente. Para forçar uma nova geração, basta apagar a pasta `output/slices/`.