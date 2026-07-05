#pragma once
#include <vector>
#include <string>
#include <optional>
#include <glm/glm.hpp>
#include "Slicer.h"
#include "SliceMetadata.h"
#include "OBJLoader.h"

struct CollisionResult {
    bool  blocked   = false;
    bool  hasGround = false;
    float groundZ   = 0.f;
};

class CollisionSystem {
public:
    void init(const SliceMetadata& meta, int nslices = 8);

    // Algorithm 3.4 broad-phase: any non-black pixel in pawn AABB across slice range
    bool broadPhase(const glm::vec3& pos, const Mesh& mesh,
                    float pawnHalfX, float pawnHalfY,
                    float pawnMinZ, float pawnMaxZ) const;

    // Narrow phase: obstacle blocking (blue channel high)
    bool checkObstacle(const glm::vec3& pos, const Mesh& mesh,
                       float pawnHalfX, float pawnHalfY,
                       float pawnMinZ, float pawnMaxZ) const;

    // Narrow phase: floor height from red channel
    CollisionResult queryGround(const glm::vec3& pos, const Mesh& mesh,
                                float pawnHalfX, float pawnHalfY) const;

    int  pawnPixW(float pawnSizeX, float t) const;
    int  pawnPixH(float pawnSizeY, float t) const;
    int  loadedSliceCount() const;
    const SliceMetadata& meta() const { return m_meta; }
    const Slice& getSlice(int index) const { return ensureLoaded(index); }

private:
    SliceMetadata m_meta;
    int           m_nslices = 8;
    mutable std::vector<std::optional<Slice>> m_cache;

    Slice& ensureLoaded(int index) const;
    void   evictFurthest(int playerSlice) const;
    glm::u8vec4 getPixel(int sliceIdx, int px, int py) const;
    void worldToPixelXY(const glm::vec3& pos, int& outPx, int& outPy) const;
    int  sliceRangeStart(float pawnFootZ, float pawnMinZ) const;
    int  sliceRangeEnd(float pawnFootZ, float pawnMaxZ) const;
    bool pixelNonBlack(const glm::u8vec4& px) const;
    bool pixelIsObstacle(const glm::u8vec4& px) const;
    bool pixelIsFloor(const glm::u8vec4& px) const;
};
