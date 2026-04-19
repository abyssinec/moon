#pragma once

#include "ProjectState.h"
#include "WaveformService.h"

#if MOON_HAS_JUCE
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace moon::ui
{
class WaveformRendererOpenGL final : private juce::OpenGLRenderer
{
public:
    struct DebugStats
    {
        std::uint64_t atlasAllocationCount{0};
        std::uint64_t atlasReuseCount{0};
        std::uint64_t atlasGrowCount{0};
        std::uint64_t atlasFreeCount{0};
        std::uint64_t atlasDirtyBatchCount{0};
        std::uint64_t atlasSkippedUploadCount{0};
        std::uint64_t visibleSetEarlyOutCount{0};
        std::uint64_t mergedUploadCount{0};
        std::uint64_t tileUploadCount{0};
        std::uint64_t tileReuseCount{0};
        std::uint64_t revisionSkipCount{0};
        std::uint64_t mergedReuseCount{0};
        std::size_t atlasBytesUsed{0};
        std::size_t atlasBytesCapacity{0};
        std::size_t rollingUploadBytes{0};
        bool mergedPathActive{false};
        int mergedColourBatchCount{0};
        int atlasActiveBatchCount{0};
    };

    struct Geometry
    {
        std::vector<float> lineVertices;
        std::vector<float> gpuVertices;
        int width{0};
        int height{0};
        std::uint64_t revision{0};
    };

    struct DrawItem
    {
        std::shared_ptr<const Geometry> geometry;
        int drawX{0};
        int drawY{0};
        juce::Colour colour;
    };

    WaveformRendererOpenGL();
    ~WaveformRendererOpenGL();

    void attachTo(juce::Component& component);
    void detach();
    bool isAttached() const noexcept { return attachedComponent_ != nullptr; }
    bool isReady() const noexcept { return ready_.load(); }
    void setVisibleTiles(std::vector<DrawItem> items);
    void invalidateAll();
    void invalidateRevisions(const std::vector<std::uint64_t>& revisions);
    const DebugStats& debugStats() const noexcept { return debugStats_; }

private:
    struct GpuTileBuffer
    {
        std::uint64_t geometryRevision{0};
        GLuint vbo{0};
        int vertexCount{0};
        std::size_t byteSize{0};
        std::size_t capacityBytes{0};
        bool visible{false};
        std::uint64_t lastUse{0};
    };

    struct MergedBatchBuffer
    {
        std::uint64_t signature{0};
        juce::uint32 colourArgb{0};
        std::size_t offsetBytes{0};
        std::size_t byteSize{0};
        std::size_t capacityBytes{0};
        int vertexOffset{0};
        int vertexCount{0};
        std::uint64_t lastUse{0};
    };

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void releaseGpuBuffers();
    void pruneBuffers();
    void renderMergedBatches(const std::vector<DrawItem>& items);
    void uploadBuffer(GLuint vbo, std::size_t& capacityBytes, const float* data, std::size_t byteSize);
    std::size_t allocateAtlasRegion(std::size_t minimumCapacityBytes);
    void freeAtlasRegion(std::size_t offsetBytes, std::size_t byteSize);
    void coalesceAtlasFreeList();
    std::size_t atlasGrowthTarget(std::size_t requiredBytes) const noexcept;
    bool ensureAtlasCapacity(std::size_t requiredEndBytes);
    void recomputeAtlasUsage() noexcept;
    void validateAtlasState() const;

    juce::OpenGLContext openGLContext_;
    juce::Component* attachedComponent_{nullptr};
    std::unique_ptr<juce::OpenGLShaderProgram> shaderProgram_;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> positionAttribute_;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> viewportUniform_;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> offsetUniform_;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> colourUniform_;
    std::unordered_map<const Geometry*, GpuTileBuffer> gpuBuffers_;
    std::mutex drawItemsMutex_;
    std::vector<DrawItem> drawItems_;
    std::unordered_map<const Geometry*, bool> visibleGeometries_;
    std::mutex invalidationMutex_;
    std::vector<std::uint64_t> pendingInvalidatedRevisions_;
    bool pendingClearAll_{false};
    std::atomic<bool> ready_{false};
    std::uint64_t useCounter_{0};
    std::size_t residentBytes_{0};
    std::uint64_t visibleBatchSignature_{0};
    GLuint mergedAtlasVbo_{0};
    std::size_t mergedAtlasCapacityBytes_{0};
    std::size_t mergedAtlasUsedBytes_{0};
    std::size_t mergedAtlasAllocatedBytes_{0};
    std::size_t mergedAtlasResidentBytes_{0};
    std::unordered_map<juce::uint32, MergedBatchBuffer> mergedBatchBuffers_;
    std::vector<std::pair<std::size_t, std::size_t>> atlasFreeList_;
    DebugStats debugStats_;
};

class WaveformTileCache
{
public:
    enum class PrewarmAggressiveness
    {
        Full,
        Reduced,
        Minimal
    };

    struct DebugStats
    {
        bool analysisPending{false};
        int pendingAnalysisCount{0};
        PrewarmAggressiveness prewarmMode{PrewarmAggressiveness::Full};
        int visibleTileCount{0};
        int queuedNearVisibleJobs{0};
        int queuedFarRingJobs{0};
        int pendingPrewarmJobs{0};
        std::uint64_t skippedPrewarmJobs{0};
        std::uint64_t farRingSuppressedCount{0};
        std::uint64_t throttleTransitions{0};
    };

    struct Style
    {
        juce::Colour fillColour;
        juce::Colour placeholderColour;
    };

    struct DrawBatch
    {
        std::vector<WaveformRendererOpenGL::DrawItem> gpuItems;
        bool needsCpuFallback{false};
    };

    explicit WaveformTileCache(moon::engine::WaveformService& waveformService);
    ~WaveformTileCache();
    const DebugStats& debugStats() const noexcept { return debugStats_; }

    void invalidateAll();
    std::vector<std::uint64_t> invalidateSource(const std::string& assetPath);

    DrawBatch prepareVisibleTiles(const moon::engine::ClipInfo& clip,
                                  const std::string& assetPath,
                                  const juce::Rectangle<int>& clipBounds,
                                  const juce::Rectangle<int>& visibleBounds,
                                  double pixelsPerSecond,
                                  double waveformDetailScale,
                                  const Style& style);
    void drawCpuFallback(juce::Graphics& g, const DrawBatch& batch) const;
    std::vector<juce::Rectangle<int>> visibleTileBoundsForClip(const juce::Rectangle<int>& clipBounds,
                                                               const juce::Rectangle<int>& visibleBounds,
                                                               double pixelsPerSecond) const;

private:
    class PrewarmJob;

    enum class ResidencyClass
    {
        Stale = 0,
        NearVisible = 1,
        Visible = 2
    };

    struct TileKey
    {
        std::string assetPath;
        std::uint64_t sourceRevision{0};
        std::int64_t offsetMillis{0};
        std::int64_t durationMillis{0};
        int innerWidth{0};
        int waveformHeight{0};
        int tileIndex{0};
        int tileWidth{0};
        int nominalTileWidth{0};
        int samplesPerBucket{0};
        int detailScaleQuantized{0};

        bool operator==(const TileKey& other) const noexcept;
    };

    struct TileEntry
    {
        TileKey key;
        std::shared_ptr<WaveformRendererOpenGL::Geometry> geometry;
        std::size_t approxBytes{0};
        ResidencyClass residency{ResidencyClass::Stale};
        std::uint64_t lastUse{0};
    };

    std::size_t hashTileKey(const TileKey& key) const noexcept;
    int chooseTileWidth(double pixelsPerSecond) const noexcept;
    std::shared_ptr<WaveformRendererOpenGL::Geometry> buildPlaceholderGeometry(int width, int height) const;
    std::shared_ptr<WaveformRendererOpenGL::Geometry> renderTileGeometry(const TileKey& key, const moon::engine::WaveformData& waveform) const;
    std::size_t estimateGeometryBytes(const std::shared_ptr<WaveformRendererOpenGL::Geometry>& geometry) const noexcept;
    void pruneCache();
    int prefetchTileCount(double pixelsPerSecond) const noexcept;
    int backgroundPrefetchTileCount(double pixelsPerSecond) const noexcept;
    void queuePrewarmTile(const TileKey& key, std::shared_ptr<const moon::engine::WaveformData> waveform);
    void storePrewarmedTile(const TileKey& key, std::shared_ptr<WaveformRendererOpenGL::Geometry> geometry);
    PrewarmAggressiveness updatePrewarmAggressiveness(int pendingAnalysisCount, int visibleTileCount);
    std::size_t prewarmBudgetFor(PrewarmAggressiveness mode) const noexcept;

    moon::engine::WaveformService& waveformService_;
    mutable std::mutex cacheMutex_;
    std::unordered_map<std::size_t, TileEntry> tiles_;
    std::uint64_t useCounter_{0};
    std::size_t residentBytes_{0};
    std::unordered_map<std::size_t, bool> pendingPrewarm_;
    PrewarmAggressiveness prewarmMode_{PrewarmAggressiveness::Full};
    int throttleCooldownTicks_{0};
#if MOON_HAS_JUCE
    juce::ThreadPool prewarmPool_{1};
#endif
    DebugStats debugStats_;
};
}
#endif
