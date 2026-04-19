#include "WaveformRenderer.h"

#if MOON_HAS_JUCE
#include <algorithm>
#include <cmath>
#include <tuple>

namespace moon::ui
{
namespace
{
namespace gl = juce::gl;

constexpr int kMaxCachedTiles = 768;
constexpr int kMaxGpuTiles = 1024;
constexpr std::size_t kMaxGpuBytes = 96u * 1024u * 1024u;
constexpr std::size_t kMaxTileBytes = 128u * 1024u * 1024u;
constexpr std::size_t kMergedBatchThreshold = 16;
constexpr std::size_t kMaxPendingPrewarm = 192;
constexpr std::size_t kMergedAtlasBudget = 32u * 1024u * 1024u;

void appendQuad(std::vector<float>& vertices, float leftX, float rightX, float topY, float bottomY)
{
    vertices.push_back(leftX);  vertices.push_back(topY);
    vertices.push_back(rightX); vertices.push_back(topY);
    vertices.push_back(rightX); vertices.push_back(bottomY);

    vertices.push_back(leftX);  vertices.push_back(topY);
    vertices.push_back(rightX); vertices.push_back(bottomY);
    vertices.push_back(leftX);  vertices.push_back(bottomY);
}

template <typename TValue>
void hashCombine(std::uint64_t& seed, TValue value)
{
    const auto hashed = static_cast<std::uint64_t>(std::hash<TValue>{}(value));
    seed ^= hashed + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}
}

class WaveformTileCache::PrewarmJob final : public juce::ThreadPoolJob
{
public:
    PrewarmJob(WaveformTileCache& owner,
               TileKey key,
               std::shared_ptr<const moon::engine::WaveformData> waveform)
        : juce::ThreadPoolJob("Waveform tile prewarm")
        , owner_(owner)
        , key_(std::move(key))
        , waveform_(std::move(waveform))
    {
    }

    JobStatus runJob() override
    {
        if (shouldExit() || waveform_ == nullptr)
        {
            return jobHasFinished;
        }

        auto geometry = owner_.renderTileGeometry(key_, *waveform_);
        owner_.storePrewarmedTile(key_, std::move(geometry));
        return jobHasFinished;
    }

private:
    WaveformTileCache& owner_;
    TileKey key_;
    std::shared_ptr<const moon::engine::WaveformData> waveform_;
};

WaveformRendererOpenGL::WaveformRendererOpenGL()
{
    openGLContext_.setComponentPaintingEnabled(true);
    openGLContext_.setContinuousRepainting(false);
    openGLContext_.setMultisamplingEnabled(true);
    openGLContext_.setRenderer(this);
}

WaveformRendererOpenGL::~WaveformRendererOpenGL()
{
    detach();
}

void WaveformRendererOpenGL::attachTo(juce::Component& component)
{
    if (attachedComponent_ == &component)
    {
        return;
    }

    detach();
    openGLContext_.attachTo(component);
    attachedComponent_ = &component;
}

void WaveformRendererOpenGL::detach()
{
    if (attachedComponent_ == nullptr)
    {
        return;
    }

    openGLContext_.detach();
    attachedComponent_ = nullptr;
}

void WaveformRendererOpenGL::setVisibleTiles(std::vector<DrawItem> items)
{
    std::uint64_t signature = 0;
    std::unordered_map<const Geometry*, bool> visibleGeometries;
    visibleGeometries.reserve(items.size());
    for (const auto& item : items)
    {
        if (item.geometry != nullptr)
        {
            visibleGeometries[item.geometry.get()] = true;
            hashCombine(signature, item.geometry->revision);
            hashCombine(signature, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(item.geometry.get())));
            hashCombine(signature, item.drawX);
            hashCombine(signature, item.drawY);
            hashCombine(signature, item.colour.getARGB());
        }
    }

    {
        std::scoped_lock lock(drawItemsMutex_);
        if (signature == visibleBatchSignature_ && items.size() == drawItems_.size())
        {
            debugStats_.visibleSetEarlyOutCount += 1;
            return;
        }
        drawItems_ = std::move(items);
        visibleGeometries_ = std::move(visibleGeometries);
        visibleBatchSignature_ = signature;
    }
    openGLContext_.triggerRepaint();
}

void WaveformRendererOpenGL::invalidateAll()
{
    {
        std::scoped_lock lock(drawItemsMutex_);
        drawItems_.clear();
        visibleGeometries_.clear();
        visibleBatchSignature_ = 0;
    }
    {
        std::scoped_lock lock(invalidationMutex_);
        pendingClearAll_ = true;
        pendingInvalidatedRevisions_.clear();
    }
    openGLContext_.triggerRepaint();
}

void WaveformRendererOpenGL::invalidateRevisions(const std::vector<std::uint64_t>& revisions)
{
    if (revisions.empty())
    {
        return;
    }

    {
        std::scoped_lock lock(invalidationMutex_);
        pendingInvalidatedRevisions_.insert(
            pendingInvalidatedRevisions_.end(),
            revisions.begin(),
            revisions.end());
    }
    openGLContext_.triggerRepaint();
}

void WaveformRendererOpenGL::newOpenGLContextCreated()
{
    static constexpr const char* vertexShader = R"(
        attribute vec2 aPosition;
        uniform vec2 uViewport;
        uniform vec2 uOffset;
        void main()
        {
            vec2 pixel = aPosition + uOffset;
            vec2 ndc = vec2((pixel.x / uViewport.x) * 2.0 - 1.0,
                            1.0 - (pixel.y / uViewport.y) * 2.0);
            gl_Position = vec4(ndc, 0.0, 1.0);
        })";

    static constexpr const char* fragmentShader = R"(
        uniform vec4 uColour;
        void main()
        {
            gl_FragColor = uColour;
        })";

    shaderProgram_ = std::make_unique<juce::OpenGLShaderProgram>(openGLContext_);
    const bool linked =
        shaderProgram_->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(vertexShader)) &&
        shaderProgram_->addFragmentShader(juce::OpenGLHelpers::translateFragmentShaderToV3(fragmentShader)) &&
        shaderProgram_->link();

    if (!linked)
    {
        shaderProgram_.reset();
        ready_.store(false);
        return;
    }

    positionAttribute_ = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shaderProgram_, "aPosition");
    viewportUniform_ = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram_, "uViewport");
    offsetUniform_ = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram_, "uOffset");
    colourUniform_ = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*shaderProgram_, "uColour");

    ready_.store(
        positionAttribute_ != nullptr &&
        viewportUniform_ != nullptr &&
        offsetUniform_ != nullptr &&
        colourUniform_ != nullptr);
}

void WaveformRendererOpenGL::renderOpenGL()
{
    if (!ready_.load() || attachedComponent_ == nullptr || shaderProgram_ == nullptr)
    {
        return;
    }

    {
        std::scoped_lock lock(invalidationMutex_);
        if (pendingClearAll_)
        {
            releaseGpuBuffers();
            residentBytes_ = 0;
            pendingClearAll_ = false;
        }
        else if (!pendingInvalidatedRevisions_.empty())
        {
            for (auto it = gpuBuffers_.begin(); it != gpuBuffers_.end();)
            {
                if (std::find(pendingInvalidatedRevisions_.begin(), pendingInvalidatedRevisions_.end(), it->second.geometryRevision) != pendingInvalidatedRevisions_.end())
                {
                    if (it->second.vbo != 0)
                    {
                        openGLContext_.extensions.glDeleteBuffers(1, &it->second.vbo);
                    }
                    residentBytes_ -= it->second.byteSize;
                    it = gpuBuffers_.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            pendingInvalidatedRevisions_.clear();
        }
    }

    std::vector<DrawItem> items;
    std::unordered_map<const Geometry*, bool> visibleGeometries;
    {
        std::scoped_lock lock(drawItemsMutex_);
        items = drawItems_;
        visibleGeometries = visibleGeometries_;
    }

    juce::OpenGLHelpers::clear(juce::Colours::transparentBlack);
    gl::glViewport(0, 0, attachedComponent_->getWidth(), attachedComponent_->getHeight());
    gl::glDisable(gl::GL_DEPTH_TEST);
    gl::glEnable(gl::GL_BLEND);
    gl::glBlendFunc(gl::GL_SRC_ALPHA, gl::GL_ONE_MINUS_SRC_ALPHA);
    shaderProgram_->use();
    viewportUniform_->set(static_cast<float>(attachedComponent_->getWidth()), static_cast<float>(attachedComponent_->getHeight()));
    debugStats_.rollingUploadBytes = 0;
    debugStats_.mergedPathActive = items.size() >= kMergedBatchThreshold;
    debugStats_.mergedColourBatchCount = 0;
    debugStats_.atlasBytesUsed = mergedAtlasAllocatedBytes_;
    debugStats_.atlasBytesCapacity = mergedAtlasCapacityBytes_;
    debugStats_.atlasActiveBatchCount = static_cast<int>(mergedBatchBuffers_.size());

    for (auto& [_, buffer] : gpuBuffers_)
    {
        buffer.visible = false;
    }

    if (items.size() >= kMergedBatchThreshold)
    {
        renderMergedBatches(items);
        pruneBuffers();
        return;
    }

    for (const auto& item : items)
    {
        if (item.geometry == nullptr || item.geometry->gpuVertices.empty())
        {
            continue;
        }

        auto& buffer = gpuBuffers_[item.geometry.get()];
        buffer.visible = visibleGeometries.find(item.geometry.get()) != visibleGeometries.end();
        if (buffer.vbo == 0)
        {
            openGLContext_.extensions.glGenBuffers(1, &buffer.vbo);
        }

        if (buffer.geometryRevision != item.geometry->revision)
        {
            residentBytes_ -= buffer.byteSize;
            buffer.geometryRevision = item.geometry->revision;
            buffer.vertexCount = static_cast<int>(item.geometry->gpuVertices.size() / 2);
            buffer.byteSize = item.geometry->gpuVertices.size() * sizeof(float);
            uploadBuffer(buffer.vbo, buffer.capacityBytes, item.geometry->gpuVertices.data(), buffer.byteSize);
            residentBytes_ += buffer.byteSize;
            debugStats_.tileUploadCount += 1;
        }
        else
        {
            debugStats_.revisionSkipCount += 1;
            debugStats_.tileReuseCount += 1;
        }

        buffer.lastUse = ++useCounter_;
        offsetUniform_->set(static_cast<float>(item.drawX), static_cast<float>(item.drawY));
        colourUniform_->set(item.colour.getFloatRed(), item.colour.getFloatGreen(), item.colour.getFloatBlue(), item.colour.getFloatAlpha());
        openGLContext_.extensions.glBindBuffer(gl::GL_ARRAY_BUFFER, buffer.vbo);
        openGLContext_.extensions.glVertexAttribPointer(static_cast<GLuint>(positionAttribute_->attributeID), 2, gl::GL_FLOAT, gl::GL_FALSE, 0, nullptr);
        openGLContext_.extensions.glEnableVertexAttribArray(static_cast<GLuint>(positionAttribute_->attributeID));
        gl::glDrawArrays(gl::GL_TRIANGLES, 0, buffer.vertexCount);
        openGLContext_.extensions.glDisableVertexAttribArray(static_cast<GLuint>(positionAttribute_->attributeID));
    }

    openGLContext_.extensions.glBindBuffer(gl::GL_ARRAY_BUFFER, 0);
    pruneBuffers();
}

void WaveformRendererOpenGL::uploadBuffer(GLuint vbo, std::size_t& capacityBytes, const float* data, std::size_t byteSize)
{
    openGLContext_.extensions.glBindBuffer(gl::GL_ARRAY_BUFFER, vbo);
    if (capacityBytes < byteSize)
    {
        const auto newCapacity = juce::jmax<std::size_t>(byteSize, capacityBytes == 0 ? byteSize : capacityBytes * 2);
        openGLContext_.extensions.glBufferData(gl::GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(newCapacity), nullptr, gl::GL_DYNAMIC_DRAW);
        capacityBytes = newCapacity;
    }
    openGLContext_.extensions.glBufferSubData(gl::GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(byteSize), data);
    debugStats_.rollingUploadBytes += byteSize;
}

void WaveformRendererOpenGL::renderMergedBatches(const std::vector<DrawItem>& items)
{
    struct BatchItem
    {
        const Geometry* geometry{nullptr};
        int drawX{0};
        int drawY{0};
    };

    struct BatchInfo
    {
        std::uint64_t signature{0};
        std::size_t floatCount{0};
        std::vector<BatchItem> items;
    };

    std::unordered_map<juce::uint32, BatchInfo> batches;
    batches.reserve(items.size());
    for (const auto& item : items)
    {
        if (item.geometry == nullptr || item.geometry->gpuVertices.empty())
        {
            continue;
        }

        const auto colourKey = item.colour.getARGB();
        auto& batch = batches[colourKey];
        hashCombine(batch.signature, item.geometry->revision);
        hashCombine(batch.signature, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(item.geometry.get())));
        hashCombine(batch.signature, item.drawX);
        hashCombine(batch.signature, item.drawY);
        batch.floatCount += item.geometry->gpuVertices.size();
        batch.items.push_back({item.geometry.get(), item.drawX, item.drawY});
    }

    if (mergedAtlasVbo_ == 0)
    {
        openGLContext_.extensions.glGenBuffers(1, &mergedAtlasVbo_);
    }

    for (auto it = mergedBatchBuffers_.begin(); it != mergedBatchBuffers_.end();)
    {
        if (batches.find(it->first) == batches.end())
        {
            freeAtlasRegion(it->second.offsetBytes, it->second.capacityBytes);
            it = mergedBatchBuffers_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    recomputeAtlasUsage();

    std::unordered_map<juce::uint32, bool> batchNeedsUpload;
    std::unordered_map<juce::uint32, std::size_t> batchByteSizes;
    batchNeedsUpload.reserve(batches.size());
    batchByteSizes.reserve(batches.size());
    std::size_t requiredAtlasEnd = 0;

    for (const auto& [colourKey, batch] : batches)
    {
        auto& merged = mergedBatchBuffers_[colourKey];
        merged.colourArgb = colourKey;
        merged.lastUse = ++useCounter_;
        const auto byteSize = batch.floatCount * sizeof(float);
        const bool needsReupload = merged.signature != batch.signature;
        const bool needsResize = merged.capacityBytes < byteSize;
        batchNeedsUpload[colourKey] = needsReupload || needsResize;
        batchByteSizes[colourKey] = byteSize;
        if (needsResize)
        {
            if (merged.capacityBytes > 0)
            {
                freeAtlasRegion(merged.offsetBytes, merged.capacityBytes);
            }
            merged.capacityBytes = atlasGrowthTarget(byteSize);
            merged.offsetBytes = allocateAtlasRegion(merged.capacityBytes);
            debugStats_.atlasAllocationCount += 1;
        }
        else if (merged.capacityBytes > 0)
        {
            debugStats_.atlasReuseCount += 1;
        }
        requiredAtlasEnd = juce::jmax(requiredAtlasEnd, merged.offsetBytes + merged.capacityBytes);
    }
    recomputeAtlasUsage();

    const bool atlasGrew = requiredAtlasEnd > 0 ? ensureAtlasCapacity(requiredAtlasEnd) : false;
    if (atlasGrew)
    {
        // glBufferData orphaned the old atlas contents, so every live batch must refresh once.
        for (auto& [colourKey, _] : batches)
        {
            batchNeedsUpload[colourKey] = true;
        }
    }

    for (const auto& [colourKey, batch] : batches)
    {
        auto& merged = mergedBatchBuffers_[colourKey];
        const auto byteSize = batchByteSizes[colourKey];
        const auto needsUpload = batchNeedsUpload[colourKey];

        if (needsUpload)
        {
            std::vector<float> vertices;
            vertices.reserve(batch.floatCount);
            for (const auto& batchItem : batch.items)
            {
                const auto& source = batchItem.geometry->gpuVertices;
                const auto oldSize = vertices.size();
                vertices.resize(oldSize + source.size());
                for (std::size_t index = 0; index < source.size(); index += 2)
                {
                    vertices[oldSize + index + 0] = source[index + 0] + static_cast<float>(batchItem.drawX);
                    vertices[oldSize + index + 1] = source[index + 1] + static_cast<float>(batchItem.drawY);
                }
            }

            openGLContext_.extensions.glBindBuffer(gl::GL_ARRAY_BUFFER, mergedAtlasVbo_);
            jassert(merged.capacityBytes >= byteSize);
            jassert(merged.offsetBytes + merged.capacityBytes <= mergedAtlasCapacityBytes_);
            openGLContext_.extensions.glBufferSubData(
                gl::GL_ARRAY_BUFFER,
                static_cast<GLintptr>(merged.offsetBytes),
                static_cast<GLsizeiptr>(byteSize),
                vertices.data());
            merged.signature = batch.signature;
            merged.byteSize = byteSize;
            merged.vertexOffset = static_cast<int>(merged.offsetBytes / sizeof(float) / 2);
            merged.vertexCount = static_cast<int>(vertices.size() / 2);
            debugStats_.atlasDirtyBatchCount += 1;
            debugStats_.mergedUploadCount += 1;
            debugStats_.rollingUploadBytes += byteSize;
        }
        else
        {
            merged.vertexOffset = static_cast<int>(merged.offsetBytes / sizeof(float) / 2);
            merged.vertexCount = static_cast<int>(batch.floatCount / 2);
            debugStats_.atlasSkippedUploadCount += 1;
            debugStats_.mergedReuseCount += 1;
        }

        jassert(merged.byteSize <= merged.capacityBytes);
        jassert(merged.vertexOffset >= 0);
        jassert(merged.vertexCount >= 0);
        jassert(static_cast<std::size_t>(merged.vertexCount) * sizeof(float) * 2 >= merged.byteSize || merged.vertexCount == 0);
    }

    for (auto left = mergedBatchBuffers_.begin(); left != mergedBatchBuffers_.end(); ++left)
    {
        if (batches.find(left->first) == batches.end())
        {
            continue;
        }
        for (auto right = std::next(left); right != mergedBatchBuffers_.end(); ++right)
        {
            if (batches.find(right->first) == batches.end())
            {
                continue;
            }

            const auto leftEnd = left->second.offsetBytes + left->second.capacityBytes;
            const auto rightEnd = right->second.offsetBytes + right->second.capacityBytes;
            const bool overlap = left->second.offsetBytes < rightEnd && right->second.offsetBytes < leftEnd;
            jassert(!overlap);
        }
    }
    validateAtlasState();

    offsetUniform_->set(0.0f, 0.0f);
    openGLContext_.extensions.glBindBuffer(gl::GL_ARRAY_BUFFER, mergedAtlasVbo_);
    openGLContext_.extensions.glVertexAttribPointer(static_cast<GLuint>(positionAttribute_->attributeID), 2, gl::GL_FLOAT, gl::GL_FALSE, 0, nullptr);
    openGLContext_.extensions.glEnableVertexAttribArray(static_cast<GLuint>(positionAttribute_->attributeID));
    for (const auto& [colourKey, merged] : mergedBatchBuffers_)
    {
        if (batches.find(colourKey) == batches.end())
        {
            continue;
        }
        const auto colour = juce::Colour(colourKey);
        colourUniform_->set(colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue(), colour.getFloatAlpha());
        gl::glDrawArrays(gl::GL_TRIANGLES, merged.vertexOffset, merged.vertexCount);
    }
    openGLContext_.extensions.glDisableVertexAttribArray(static_cast<GLuint>(positionAttribute_->attributeID));
    debugStats_.mergedColourBatchCount = static_cast<int>(batches.size());
    debugStats_.atlasActiveBatchCount = static_cast<int>(mergedBatchBuffers_.size());
    debugStats_.atlasBytesUsed = mergedAtlasAllocatedBytes_;
    debugStats_.atlasBytesCapacity = mergedAtlasCapacityBytes_;
}

std::size_t WaveformRendererOpenGL::allocateAtlasRegion(std::size_t byteSize)
{
    jassert(byteSize > 0);
    // Atlas regions are stable until explicitly freed; we prefer reuse over reshuffling.
    coalesceAtlasFreeList();
    for (auto it = atlasFreeList_.begin(); it != atlasFreeList_.end(); ++it)
    {
        if (it->second >= byteSize)
        {
            const auto offset = it->first;
            if (it->second > byteSize)
            {
                it->first += byteSize;
                it->second -= byteSize;
            }
            else
            {
                atlasFreeList_.erase(it);
            }
            return offset;
        }
    }

    const auto offset = mergedAtlasUsedBytes_;
    mergedAtlasUsedBytes_ += byteSize;
    return offset;
}

void WaveformRendererOpenGL::freeAtlasRegion(std::size_t offsetBytes, std::size_t byteSize)
{
    if (byteSize == 0)
    {
        return;
    }

    jassert(mergedAtlasCapacityBytes_ == 0 || offsetBytes + byteSize <= mergedAtlasCapacityBytes_);
    atlasFreeList_.emplace_back(offsetBytes, byteSize);
    debugStats_.atlasFreeCount += 1;
    coalesceAtlasFreeList();
}

void WaveformRendererOpenGL::coalesceAtlasFreeList()
{
    if (atlasFreeList_.empty())
    {
        return;
    }

    std::sort(atlasFreeList_.begin(), atlasFreeList_.end(), [](const auto& left, const auto& right)
    {
        return left.first < right.first;
    });

    std::vector<std::pair<std::size_t, std::size_t>> merged;
    merged.reserve(atlasFreeList_.size());
    merged.push_back(atlasFreeList_.front());
    for (std::size_t index = 1; index < atlasFreeList_.size(); ++index)
    {
        auto& tail = merged.back();
        if (tail.first + tail.second == atlasFreeList_[index].first)
        {
            tail.second += atlasFreeList_[index].second;
        }
        else
        {
            merged.push_back(atlasFreeList_[index]);
        }
    }
    atlasFreeList_ = std::move(merged);

    // Reclaim trailing free space from the atlas high-water mark without doing a full compaction pass.
    while (!atlasFreeList_.empty())
    {
        auto& tail = atlasFreeList_.back();
        if (tail.first + tail.second != mergedAtlasUsedBytes_)
        {
            break;
        }

        mergedAtlasUsedBytes_ = tail.first;
        atlasFreeList_.pop_back();
    }
}

std::size_t WaveformRendererOpenGL::atlasGrowthTarget(std::size_t requiredBytes) const noexcept
{
    return juce::jmax<std::size_t>(requiredBytes, juce::jmax<std::size_t>(256, requiredBytes + requiredBytes / 2));
}

bool WaveformRendererOpenGL::ensureAtlasCapacity(std::size_t requiredEndBytes)
{
    if (mergedAtlasCapacityBytes_ >= requiredEndBytes)
    {
        return false;
    }

    auto newCapacity = mergedAtlasCapacityBytes_ == 0 ? atlasGrowthTarget(requiredEndBytes) : mergedAtlasCapacityBytes_;
    while (newCapacity < requiredEndBytes)
    {
        newCapacity = juce::jmax(atlasGrowthTarget(requiredEndBytes), newCapacity + juce::jmax<std::size_t>(1024, newCapacity / 2));
    }

    openGLContext_.extensions.glBindBuffer(gl::GL_ARRAY_BUFFER, mergedAtlasVbo_);
    openGLContext_.extensions.glBufferData(gl::GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(newCapacity), nullptr, gl::GL_DYNAMIC_DRAW);
    mergedAtlasCapacityBytes_ = newCapacity;
    mergedAtlasResidentBytes_ = newCapacity;
    debugStats_.atlasGrowCount += 1;
    return true;
}

void WaveformRendererOpenGL::recomputeAtlasUsage() noexcept
{
    mergedAtlasAllocatedBytes_ = 0;
    for (const auto& [_, merged] : mergedBatchBuffers_)
    {
        mergedAtlasAllocatedBytes_ += merged.capacityBytes;
    }
}

void WaveformRendererOpenGL::validateAtlasState() const
{
    for (const auto& [_, merged] : mergedBatchBuffers_)
    {
        jassert(merged.capacityBytes > 0);
        jassert(merged.byteSize <= merged.capacityBytes);
        jassert(merged.offsetBytes + merged.capacityBytes <= mergedAtlasCapacityBytes_);
        jassert(merged.vertexOffset == static_cast<int>(merged.offsetBytes / sizeof(float) / 2));
        jassert(merged.vertexCount >= 0);
        jassert(static_cast<std::size_t>(merged.vertexCount) * sizeof(float) * 2 >= merged.byteSize || merged.vertexCount == 0);
    }

    for (std::size_t index = 0; index < atlasFreeList_.size(); ++index)
    {
        jassert(atlasFreeList_[index].second > 0);
        jassert(atlasFreeList_[index].first + atlasFreeList_[index].second <= mergedAtlasCapacityBytes_);
        if (index > 0)
        {
            jassert(atlasFreeList_[index - 1].first + atlasFreeList_[index - 1].second <= atlasFreeList_[index].first);
        }

        for (const auto& [_, merged] : mergedBatchBuffers_)
        {
            const auto freeEnd = atlasFreeList_[index].first + atlasFreeList_[index].second;
            const auto mergedEnd = merged.offsetBytes + merged.capacityBytes;
            const bool overlap = atlasFreeList_[index].first < mergedEnd && merged.offsetBytes < freeEnd;
            jassert(!overlap);
        }
    }
}

void WaveformRendererOpenGL::openGLContextClosing()
{
    releaseGpuBuffers();
    colourUniform_.reset();
    offsetUniform_.reset();
    viewportUniform_.reset();
    positionAttribute_.reset();
    shaderProgram_.reset();
    ready_.store(false);
}

void WaveformRendererOpenGL::releaseGpuBuffers()
{
    for (auto& [_, buffer] : gpuBuffers_)
    {
        if (buffer.vbo != 0)
        {
            openGLContext_.extensions.glDeleteBuffers(1, &buffer.vbo);
            buffer.vbo = 0;
        }
    }
    gpuBuffers_.clear();
    if (mergedAtlasVbo_ != 0)
    {
        openGLContext_.extensions.glDeleteBuffers(1, &mergedAtlasVbo_);
        mergedAtlasVbo_ = 0;
    }
    mergedAtlasCapacityBytes_ = 0;
    mergedAtlasUsedBytes_ = 0;
    mergedAtlasAllocatedBytes_ = 0;
    mergedAtlasResidentBytes_ = 0;
    atlasFreeList_.clear();
    mergedBatchBuffers_.clear();
    residentBytes_ = 0;
}

void WaveformRendererOpenGL::pruneBuffers()
{
    if (gpuBuffers_.size() <= kMaxGpuTiles && (residentBytes_ + mergedAtlasResidentBytes_) <= (kMaxGpuBytes + kMergedAtlasBudget))
    {
        return;
    }

    std::vector<std::pair<const Geometry*, std::uint64_t>> ordered;
    ordered.reserve(gpuBuffers_.size());
    for (const auto& [geometry, buffer] : gpuBuffers_)
    {
        if (!buffer.visible)
        {
            ordered.emplace_back(geometry, buffer.lastUse);
        }
    }

    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
    {
        return left.second < right.second;
    });

    std::size_t index = 0;
    while ((gpuBuffers_.size() > kMaxGpuTiles || (residentBytes_ + mergedAtlasResidentBytes_) > (kMaxGpuBytes + kMergedAtlasBudget)) && index < ordered.size())
    {
        auto it = gpuBuffers_.find(ordered[index].first);
        if (it != gpuBuffers_.end())
        {
            if (it->second.vbo != 0)
            {
                openGLContext_.extensions.glDeleteBuffers(1, &it->second.vbo);
            }
            residentBytes_ -= it->second.byteSize;
            gpuBuffers_.erase(it);
        }
        ++index;
    }
}

WaveformTileCache::WaveformTileCache(moon::engine::WaveformService& waveformService)
    : waveformService_(waveformService)
{
}

WaveformTileCache::~WaveformTileCache()
{
    prewarmPool_.removeAllJobs(true, 1000);
}

void WaveformTileCache::invalidateAll()
{
    std::scoped_lock lock(cacheMutex_);
    tiles_.clear();
    pendingPrewarm_.clear();
    residentBytes_ = 0;
    debugStats_.pendingPrewarmJobs = 0;
}

std::vector<std::uint64_t> WaveformTileCache::invalidateSource(const std::string& assetPath)
{
    std::vector<std::uint64_t> revisions;
    std::scoped_lock lock(cacheMutex_);
    for (auto it = tiles_.begin(); it != tiles_.end();)
    {
        if (it->second.key.assetPath == assetPath)
        {
            if (it->second.geometry != nullptr)
            {
                revisions.push_back(it->second.geometry->revision);
            }
            residentBytes_ -= it->second.approxBytes;
            it = tiles_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    debugStats_.pendingPrewarmJobs = static_cast<int>(pendingPrewarm_.size());
    return revisions;
}

WaveformTileCache::DrawBatch WaveformTileCache::prepareVisibleTiles(const moon::engine::ClipInfo& clip,
                                                                    const std::string& assetPath,
                                                                    const juce::Rectangle<int>& clipBounds,
                                                                    const juce::Rectangle<int>& visibleBounds,
                                                                    double pixelsPerSecond,
                                                                    double waveformDetailScale,
                                                                    const Style& style)
{
    DrawBatch batch;
    debugStats_.queuedNearVisibleJobs = 0;
    debugStats_.queuedFarRingJobs = 0;
    debugStats_.visibleTileCount = 0;
    {
        std::scoped_lock lock(cacheMutex_);
        debugStats_.pendingPrewarmJobs = static_cast<int>(pendingPrewarm_.size());
    }
    if (assetPath.empty())
    {
        batch.gpuItems.push_back({
            buildPlaceholderGeometry(juce::jmax(1, clipBounds.getWidth() - 4), juce::jmax(1, clipBounds.getHeight() - 8)),
            clipBounds.getX() + 2,
            clipBounds.getY() + 4,
            style.placeholderColour});
        batch.needsCpuFallback = true;
        return batch;
    }

    waveformService_.requestWaveform(assetPath);
    const auto snapshot = waveformService_.snapshotFor(assetPath);
    const auto innerBounds = clipBounds.reduced(2, 4);
    if (innerBounds.isEmpty())
    {
        return batch;
    }

    const auto drawBounds = innerBounds.getIntersection(visibleBounds);
    if (drawBounds.isEmpty())
    {
        return batch;
    }

    if (snapshot.status != moon::engine::WaveformService::Status::Ready || snapshot.data == nullptr || snapshot.data->mipLevels.empty())
    {
        batch.gpuItems.push_back({buildPlaceholderGeometry(drawBounds.getWidth(), drawBounds.getHeight()), drawBounds.getX(), drawBounds.getY(), style.placeholderColour});
        batch.needsCpuFallback = true;
        return batch;
    }

    const auto clipDurationSamples = static_cast<std::uint64_t>(std::max(0.001, clip.durationSec) * static_cast<double>(snapshot.data->sampleRate));
    const auto samplesPerPixel = static_cast<double>(clipDurationSamples) / static_cast<double>(juce::jmax(1, innerBounds.getWidth()));
    const auto detailScale = juce::jlimit(0.1, 10.0, waveformDetailScale);
    const auto effectiveSamplesPerPixel = std::max(1.0, samplesPerPixel / std::max(0.2, detailScale));
    const auto& mipLevel = snapshot.data->bestLevelForSamplesPerPixel(effectiveSamplesPerPixel);
    const auto tileWidth = chooseTileWidth(pixelsPerSecond);
    const auto pendingAnalysisCount = waveformService_.pendingAnalysisCount();
    const auto estimatedVisibleTileCount = juce::jmax(1, (drawBounds.getWidth() + tileWidth - 1) / tileWidth);
    const auto prewarmMode = updatePrewarmAggressiveness(pendingAnalysisCount, estimatedVisibleTileCount);

    const auto totalTileCount = juce::jmax(1, (innerBounds.getWidth() + tileWidth - 1) / tileWidth);
    const auto firstVisibleTile = std::max(0, (drawBounds.getX() - innerBounds.getX()) / tileWidth);
    const auto lastVisibleTile = std::max(firstVisibleTile, (drawBounds.getRight() - innerBounds.getX() + tileWidth - 1) / tileWidth);
    const auto prefetchCount = prefetchTileCount(pixelsPerSecond);
    const auto backgroundPrefetchCount = prewarmMode == PrewarmAggressiveness::Minimal
        ? prefetchCount
        : (prewarmMode == PrewarmAggressiveness::Reduced
            ? juce::jmax(prefetchCount, backgroundPrefetchTileCount(pixelsPerSecond) / 2)
            : backgroundPrefetchTileCount(pixelsPerSecond));
    const auto firstTile = juce::jlimit(0, totalTileCount - 1, firstVisibleTile - prefetchCount);
    const auto lastTile = juce::jlimit(0, totalTileCount - 1, lastVisibleTile + prefetchCount);

    for (int tileIndex = firstTile; tileIndex <= lastTile; ++tileIndex)
    {
        const auto tileStartX = innerBounds.getX() + tileIndex * tileWidth;
        const auto tileActualWidth = std::min(tileWidth, innerBounds.getRight() - tileStartX);
        if (tileActualWidth <= 0)
        {
            continue;
        }

        TileKey key;
        key.assetPath = assetPath;
        key.sourceRevision = snapshot.revision;
        key.offsetMillis = static_cast<std::int64_t>(std::llround(clip.offsetSec * 1000.0));
        key.durationMillis = static_cast<std::int64_t>(std::llround(clip.durationSec * 1000.0));
        key.innerWidth = innerBounds.getWidth();
        key.waveformHeight = innerBounds.getHeight();
        key.tileIndex = tileIndex;
        key.tileWidth = tileActualWidth;
        key.nominalTileWidth = tileWidth;
        key.samplesPerBucket = mipLevel.samplesPerBucket;
        key.detailScaleQuantized = static_cast<int>(std::round(detailScale * 100.0));

        const auto hash = hashTileKey(key);
        std::shared_ptr<WaveformRendererOpenGL::Geometry> geometry;
        bool haveGeometry = false;
        {
            std::scoped_lock lock(cacheMutex_);
            auto [it, inserted] = tiles_.try_emplace(hash);
            auto& entry = it->second;
            if (!inserted && (entry.key == key) && entry.geometry != nullptr)
            {
                entry.residency = (tileIndex >= firstVisibleTile && tileIndex <= lastVisibleTile) ? ResidencyClass::Visible : ResidencyClass::NearVisible;
                entry.lastUse = ++useCounter_;
                geometry = entry.geometry;
                haveGeometry = true;
            }
        }

        if (!haveGeometry && tileIndex >= firstVisibleTile && tileIndex <= lastVisibleTile)
        {
            geometry = renderTileGeometry(key, *snapshot.data);
            {
                std::scoped_lock lock(cacheMutex_);
                auto& entry = tiles_[hash];
                residentBytes_ -= entry.approxBytes;
                entry.key = key;
                entry.geometry = geometry;
                entry.approxBytes = estimateGeometryBytes(entry.geometry);
                residentBytes_ += entry.approxBytes;
                entry.residency = ResidencyClass::Visible;
                entry.lastUse = ++useCounter_;
            }
        }

        if (tileIndex >= firstVisibleTile && tileIndex <= lastVisibleTile)
        {
            if (geometry == nullptr)
            {
                geometry = buildPlaceholderGeometry(tileActualWidth, innerBounds.getHeight());
            }
            batch.gpuItems.push_back({geometry, tileStartX, innerBounds.getY(), style.fillColour});
        }
        else if (!haveGeometry)
        {
            debugStats_.queuedNearVisibleJobs += 1;
            queuePrewarmTile(key, snapshot.data);
        }
    }

    if (prewarmMode != PrewarmAggressiveness::Minimal)
    {
        const auto backgroundFirstTile = juce::jlimit(0, totalTileCount - 1, firstVisibleTile - backgroundPrefetchCount);
        const auto backgroundLastTile = juce::jlimit(0, totalTileCount - 1, lastVisibleTile + backgroundPrefetchCount);
        for (int tileIndex = backgroundFirstTile; tileIndex <= backgroundLastTile; ++tileIndex)
        {
            if (tileIndex >= firstTile && tileIndex <= lastTile)
            {
                continue;
            }

            const auto tileActualWidth = std::min(tileWidth, innerBounds.getRight() - (innerBounds.getX() + tileIndex * tileWidth));
            if (tileActualWidth <= 0)
            {
                continue;
            }

            TileKey key;
            key.assetPath = assetPath;
            key.sourceRevision = snapshot.revision;
            key.offsetMillis = static_cast<std::int64_t>(std::llround(clip.offsetSec * 1000.0));
            key.durationMillis = static_cast<std::int64_t>(std::llround(clip.durationSec * 1000.0));
            key.innerWidth = innerBounds.getWidth();
            key.waveformHeight = innerBounds.getHeight();
            key.tileIndex = tileIndex;
            key.tileWidth = tileActualWidth;
            key.nominalTileWidth = tileWidth;
            key.samplesPerBucket = mipLevel.samplesPerBucket;
            key.detailScaleQuantized = static_cast<int>(std::round(detailScale * 100.0));
            debugStats_.queuedFarRingJobs += 1;
            queuePrewarmTile(key, snapshot.data);
        }
    }
    else
    {
        debugStats_.farRingSuppressedCount += 1;
    }

    debugStats_.visibleTileCount = static_cast<int>(batch.gpuItems.size());
    pruneCache();
    return batch;
}

std::vector<juce::Rectangle<int>> WaveformTileCache::visibleTileBoundsForClip(const juce::Rectangle<int>& clipBounds,
                                                                               const juce::Rectangle<int>& visibleBounds,
                                                                               double pixelsPerSecond) const
{
    std::vector<juce::Rectangle<int>> bounds;
    const auto innerBounds = clipBounds.reduced(2, 4);
    const auto drawBounds = innerBounds.getIntersection(visibleBounds);
    if (drawBounds.isEmpty())
    {
        return bounds;
    }

    const auto tileWidth = chooseTileWidth(pixelsPerSecond);
    const auto firstTile = std::max(0, (drawBounds.getX() - innerBounds.getX()) / tileWidth);
    const auto lastTile = std::max(firstTile, (drawBounds.getRight() - innerBounds.getX() + tileWidth - 1) / tileWidth);
    bounds.reserve(static_cast<std::size_t>(lastTile - firstTile + 1));
    for (int tileIndex = firstTile; tileIndex <= lastTile; ++tileIndex)
    {
        const auto tileStartX = innerBounds.getX() + tileIndex * tileWidth;
        const auto tileActualWidth = std::min(tileWidth, innerBounds.getRight() - tileStartX);
        if (tileActualWidth <= 0)
        {
            continue;
        }
        const auto tileRect = juce::Rectangle<int>(tileStartX, innerBounds.getY(), tileActualWidth, innerBounds.getHeight()).getIntersection(visibleBounds);
        if (!tileRect.isEmpty())
        {
            bounds.push_back(tileRect);
        }
    }
    return bounds;
}

void WaveformTileCache::drawCpuFallback(juce::Graphics& g, const DrawBatch& batch) const
{
    for (const auto& item : batch.gpuItems)
    {
        if (item.geometry == nullptr)
        {
            continue;
        }

        g.setColour(item.colour);
        const auto& vertices = item.geometry->lineVertices;
        for (std::size_t index = 0; index + 3 < vertices.size(); index += 4)
        {
            g.drawLine(
                static_cast<float>(item.drawX) + vertices[index + 0],
                static_cast<float>(item.drawY) + vertices[index + 1],
                static_cast<float>(item.drawX) + vertices[index + 2],
                static_cast<float>(item.drawY) + vertices[index + 3],
                1.35f);
        }
    }
}

bool WaveformTileCache::TileKey::operator==(const TileKey& other) const noexcept
{
    return assetPath == other.assetPath &&
           sourceRevision == other.sourceRevision &&
           offsetMillis == other.offsetMillis &&
           durationMillis == other.durationMillis &&
           innerWidth == other.innerWidth &&
           waveformHeight == other.waveformHeight &&
           tileIndex == other.tileIndex &&
           tileWidth == other.tileWidth &&
           nominalTileWidth == other.nominalTileWidth &&
           samplesPerBucket == other.samplesPerBucket &&
           detailScaleQuantized == other.detailScaleQuantized;
}

std::size_t WaveformTileCache::hashTileKey(const TileKey& key) const noexcept
{
    std::size_t hash = std::hash<std::string>{}(key.assetPath);
    auto mix = [&hash](std::size_t value)
    {
        hash ^= value + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    };

    mix(std::hash<std::uint64_t>{}(key.sourceRevision));
    mix(std::hash<std::int64_t>{}(key.offsetMillis));
    mix(std::hash<std::int64_t>{}(key.durationMillis));
    mix(std::hash<int>{}(key.innerWidth));
    mix(std::hash<int>{}(key.waveformHeight));
    mix(std::hash<int>{}(key.tileIndex));
    mix(std::hash<int>{}(key.tileWidth));
    mix(std::hash<int>{}(key.nominalTileWidth));
    mix(std::hash<int>{}(key.samplesPerBucket));
    mix(std::hash<int>{}(key.detailScaleQuantized));
    return hash;
}

int WaveformTileCache::chooseTileWidth(double pixelsPerSecond) const noexcept
{
    if (pixelsPerSecond >= 320.0)
    {
        return 192;
    }
    if (pixelsPerSecond >= 140.0)
    {
        return 256;
    }
    return 320;
}

std::shared_ptr<WaveformRendererOpenGL::Geometry> WaveformTileCache::buildPlaceholderGeometry(int width, int height) const
{
    auto geometry = std::make_shared<WaveformRendererOpenGL::Geometry>();
    geometry->width = juce::jmax(1, width);
    geometry->height = juce::jmax(1, height);
    geometry->revision = static_cast<std::uint64_t>(geometry->width) * 257u + static_cast<std::uint64_t>(geometry->height);
    const auto centreY = static_cast<float>(height) * 0.5f;
    const auto upperY = juce::jmax(1.0f, centreY - 3.0f);
    const auto lowerY = juce::jmin(static_cast<float>(height) - 1.0f, centreY + 3.0f);
    geometry->lineVertices = {
        0.0f, centreY, static_cast<float>(width), centreY,
        0.0f, upperY, static_cast<float>(width), upperY,
        0.0f, lowerY, static_cast<float>(width), lowerY
    };
    appendQuad(geometry->gpuVertices, 0.0f, static_cast<float>(width), upperY, lowerY);
    return geometry;
}

std::shared_ptr<WaveformRendererOpenGL::Geometry> WaveformTileCache::renderTileGeometry(const TileKey& key, const moon::engine::WaveformData& waveform) const
{
    if (waveform.mipLevels.empty())
    {
        return buildPlaceholderGeometry(key.tileWidth, key.waveformHeight);
    }

    const auto& selectedLevel = [&waveform, &key]() -> const moon::engine::WaveformMipLevel&
    {
        for (const auto& level : waveform.mipLevels)
        {
            if (level.samplesPerBucket == key.samplesPerBucket)
            {
                return level;
            }
        }
        return waveform.mipLevels.front();
    }();

    auto geometry = std::make_shared<WaveformRendererOpenGL::Geometry>();
    geometry->width = juce::jmax(1, key.tileWidth);
    geometry->height = juce::jmax(1, key.waveformHeight);
    geometry->revision =
        (static_cast<std::uint64_t>(key.sourceRevision) << 32) ^
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.tileIndex)) ^
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.detailScaleQuantized)) << 16);

    const auto totalSourceSamples = juce::jmax<std::uint64_t>(1, waveform.totalSamples);
    const auto clipOffsetSamples = static_cast<std::uint64_t>(std::max<std::int64_t>(0, key.offsetMillis) * static_cast<std::int64_t>(waveform.sampleRate) / 1000);
    const auto clipDurationSamples = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::max<std::int64_t>(1, key.durationMillis) * static_cast<std::int64_t>(waveform.sampleRate) / 1000));
    const auto centreY = static_cast<float>(key.waveformHeight) * 0.5f;
    const auto amplitudeScale = static_cast<float>(key.waveformHeight) * 0.48f;

    geometry->lineVertices.reserve(static_cast<std::size_t>(key.tileWidth) * 4);
    geometry->gpuVertices.reserve(static_cast<std::size_t>(key.tileWidth) * 12);
    for (int x = 0; x < key.tileWidth; ++x)
    {
        const auto absolutePixel = key.tileIndex * key.nominalTileWidth + x;
        const auto startRatio = static_cast<double>(absolutePixel) / static_cast<double>(juce::jmax(1, key.innerWidth));
        const auto endRatio = static_cast<double>(absolutePixel + 1) / static_cast<double>(juce::jmax(1, key.innerWidth));
        const auto sourceStart = juce::jlimit<std::uint64_t>(0, totalSourceSamples - 1, clipOffsetSamples + static_cast<std::uint64_t>(startRatio * static_cast<double>(clipDurationSamples)));
        const auto sourceEnd = juce::jlimit<std::uint64_t>(sourceStart, totalSourceSamples, clipOffsetSamples + static_cast<std::uint64_t>(endRatio * static_cast<double>(clipDurationSamples)));
        const auto firstBucket = juce::jlimit<int>(0, static_cast<int>(selectedLevel.buckets.size()) - 1, static_cast<int>(sourceStart / static_cast<std::uint64_t>(selectedLevel.samplesPerBucket)));
        const auto lastBucket = juce::jlimit<int>(firstBucket, static_cast<int>(selectedLevel.buckets.size()) - 1, static_cast<int>(std::ceil(static_cast<double>(sourceEnd) / static_cast<double>(selectedLevel.samplesPerBucket))));

        float minValue = 1.0f;
        float maxValue = -1.0f;
        float maxRms = 0.0f;
        for (int bucketIndex = firstBucket; bucketIndex <= lastBucket; ++bucketIndex)
        {
            minValue = std::min(minValue, selectedLevel.buckets[static_cast<std::size_t>(bucketIndex)].minValue);
            maxValue = std::max(maxValue, selectedLevel.buckets[static_cast<std::size_t>(bucketIndex)].maxValue);
            maxRms = std::max(maxRms, selectedLevel.buckets[static_cast<std::size_t>(bucketIndex)].rms);
        }

        if (maxValue < minValue)
        {
            minValue = 0.0f;
            maxValue = 0.0f;
        }

        const auto rmsFloor = std::min(0.08f, maxRms * 0.35f);
        const auto topY = centreY - std::max(std::abs(maxValue), rmsFloor) * amplitudeScale;
        const auto bottomY = centreY + std::max(std::abs(minValue), rmsFloor) * amplitudeScale;
        geometry->lineVertices.push_back(static_cast<float>(x));
        geometry->lineVertices.push_back(topY);
        geometry->lineVertices.push_back(static_cast<float>(x));
        geometry->lineVertices.push_back(bottomY);
        const auto leftX = static_cast<float>(x) - 0.42f;
        const auto rightX = static_cast<float>(x) + 0.42f;
        appendQuad(geometry->gpuVertices, leftX, rightX, topY, bottomY);
    }

    return geometry;
}

void WaveformTileCache::pruneCache()
{
    std::scoped_lock lock(cacheMutex_);
    if (tiles_.size() <= kMaxCachedTiles && residentBytes_ <= kMaxTileBytes)
    {
        return;
    }

    const auto effectiveResidency = [currentUse = useCounter_](const TileEntry& entry)
    {
        const auto age = currentUse > entry.lastUse ? currentUse - entry.lastUse : 0;
        if (age > 256)
        {
            return ResidencyClass::Stale;
        }
        if (age > 48 && entry.residency == ResidencyClass::Visible)
        {
            return ResidencyClass::NearVisible;
        }
        if (age > 24 && entry.residency == ResidencyClass::NearVisible)
        {
            return ResidencyClass::Stale;
        }
        return entry.residency;
    };

    std::vector<std::tuple<std::size_t, ResidencyClass, std::uint64_t>> ordered;
    ordered.reserve(tiles_.size());
    for (const auto& [hash, entry] : tiles_)
    {
        ordered.emplace_back(hash, effectiveResidency(entry), entry.lastUse);
    }

    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
    {
        if (std::get<1>(left) != std::get<1>(right))
        {
            return static_cast<int>(std::get<1>(left)) < static_cast<int>(std::get<1>(right));
        }
        return std::get<2>(left) < std::get<2>(right);
    });

    std::size_t index = 0;
    while ((tiles_.size() > kMaxCachedTiles || residentBytes_ > kMaxTileBytes) && index < ordered.size())
    {
        auto it = tiles_.find(std::get<0>(ordered[index]));
        if (it != tiles_.end())
        {
            pendingPrewarm_.erase(std::get<0>(ordered[index]));
            residentBytes_ -= it->second.approxBytes;
            tiles_.erase(it);
        }
        ++index;
    }
}

std::size_t WaveformTileCache::estimateGeometryBytes(const std::shared_ptr<WaveformRendererOpenGL::Geometry>& geometry) const noexcept
{
    if (geometry == nullptr)
    {
        return 0;
    }

    return sizeof(WaveformRendererOpenGL::Geometry) +
           geometry->lineVertices.size() * sizeof(float) +
           geometry->gpuVertices.size() * sizeof(float);
}

int WaveformTileCache::prefetchTileCount(double pixelsPerSecond) const noexcept
{
    if (pixelsPerSecond >= 320.0)
    {
        return 3;
    }
    if (pixelsPerSecond >= 140.0)
    {
        return 2;
    }
    return 1;
}

int WaveformTileCache::backgroundPrefetchTileCount(double pixelsPerSecond) const noexcept
{
    if (pixelsPerSecond >= 320.0)
    {
        return 6;
    }
    if (pixelsPerSecond >= 140.0)
    {
        return 4;
    }
    return 2;
}

WaveformTileCache::PrewarmAggressiveness WaveformTileCache::updatePrewarmAggressiveness(int pendingAnalysisCount, int visibleTileCount)
{
    const auto analysisPending = pendingAnalysisCount > 0;
    debugStats_.analysisPending = analysisPending;
    debugStats_.pendingAnalysisCount = pendingAnalysisCount;
    auto nextMode = prewarmMode_;
    if (pendingAnalysisCount >= 2 || (analysisPending && visibleTileCount >= 12))
    {
        // Visible analysis work wins immediately; far-ring prewarm backs off and only recovers after a short cooldown.
        nextMode = PrewarmAggressiveness::Minimal;
        throttleCooldownTicks_ = 12;
    }
    else if (analysisPending)
    {
        nextMode = PrewarmAggressiveness::Reduced;
        throttleCooldownTicks_ = 8;
    }
    else if (throttleCooldownTicks_ > 0)
    {
        --throttleCooldownTicks_;
        nextMode = PrewarmAggressiveness::Reduced;
    }
    else
    {
        nextMode = PrewarmAggressiveness::Full;
    }

    if (nextMode != prewarmMode_)
    {
        prewarmMode_ = nextMode;
        debugStats_.throttleTransitions += 1;
    }
    debugStats_.prewarmMode = prewarmMode_;
    return prewarmMode_;
}

std::size_t WaveformTileCache::prewarmBudgetFor(PrewarmAggressiveness mode) const noexcept
{
    switch (mode)
    {
    case PrewarmAggressiveness::Full:
        return kMaxPendingPrewarm;
    case PrewarmAggressiveness::Reduced:
        return kMaxPendingPrewarm / 2;
    case PrewarmAggressiveness::Minimal:
        return juce::jmax<std::size_t>(16, kMaxPendingPrewarm / 6);
    }
    return kMaxPendingPrewarm;
}

void WaveformTileCache::queuePrewarmTile(const TileKey& key, std::shared_ptr<const moon::engine::WaveformData> waveform)
{
    if (waveform == nullptr)
    {
        return;
    }

    const auto hash = hashTileKey(key);
    {
        std::scoped_lock lock(cacheMutex_);
        const auto budget = prewarmBudgetFor(prewarmMode_);
        if (residentBytes_ >= kMaxTileBytes ||
            pendingPrewarm_.size() >= budget ||
            tiles_.find(hash) != tiles_.end() ||
            pendingPrewarm_.find(hash) != pendingPrewarm_.end())
        {
            debugStats_.skippedPrewarmJobs += 1;
            debugStats_.pendingPrewarmJobs = static_cast<int>(pendingPrewarm_.size());
            return;
        }
        pendingPrewarm_[hash] = true;
        debugStats_.pendingPrewarmJobs = static_cast<int>(pendingPrewarm_.size());
    }

    prewarmPool_.addJob(new PrewarmJob(*this, key, std::move(waveform)), true);
}

void WaveformTileCache::storePrewarmedTile(const TileKey& key, std::shared_ptr<WaveformRendererOpenGL::Geometry> geometry)
{
    const auto hash = hashTileKey(key);
    {
        std::scoped_lock lock(cacheMutex_);
        pendingPrewarm_.erase(hash);
        debugStats_.pendingPrewarmJobs = static_cast<int>(pendingPrewarm_.size());
        auto& entry = tiles_[hash];
        residentBytes_ -= entry.approxBytes;
        entry.key = key;
        entry.geometry = std::move(geometry);
        entry.approxBytes = estimateGeometryBytes(entry.geometry);
        residentBytes_ += entry.approxBytes;
        entry.residency = ResidencyClass::NearVisible;
        entry.lastUse = ++useCounter_;
    }
    pruneCache();
}
}
#endif
