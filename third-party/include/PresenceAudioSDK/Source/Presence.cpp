/*
====================================================================================================
  Presence Audio SDK - Main System Interface
  High-Performance Real-time Audio Path Tracing & EAX Simulation Library
====================================================================================================

  Copyright (c) 2026 Presence Collaboratory, NSDeathman & Gemini 3 & DeepSeek

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  1. The above copyright notice and this permission notice shall be included in all
     copies or substantial portions of the Software.

  2. Any project (commercial, free, open-source, or closed-source) using this Software
     must include attribution to "Presence Audio SDK by Presence Collaboratory" in its
     documentation, credits, or about screen.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

====================================================================================================
  Developed by: NSDeathman (Architecture & Core), Gemini 3 (Optimization & Math)
  Organization: Presence Collaboratory
====================================================================================================
*/

#include <vector>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <random>
#include <algorithm>
#include <condition_variable>
#include <unordered_map>
#include <memory>
#include <stdexcept>

#ifdef _WIN32
#   include <ppl.h>
#endif

#include "../Include/PresenceMacros.h"
#include "../Include/PresenceSystem.h"

PRESENCE_BEGIN

// =================================================================================================
// SECTION 1: CONSTANTS AND UTILITIES
// =================================================================================================

float GetVersion()
{
    return 0.4f;
}

const char* GetVersionString()
{
    return "Presence Audio ver. 0.4";
}

// Maximum number of different materials we can track acoustically.
static const int MAX_TRACKED_MATERIALS = 64;

// Speed of sound in m/s, used to convert MFP to reflection delay.
static const float SOUND_SPEED = 340.0f;

// =================================================================================================
// SECTION 2: THREAD-SAFE RANDOM NUMBER GENERATOR
// -------------------------------------------------------------------------------------------------
// Provides reproducible random directions for diffuse reflections.
// Each thread maintains its own Mersenne Twister engine.
// =================================================================================================
class ThreadSafeRandom
{
private:
    static std::mutex seedMutex;
    static uint32_t nextSeed;
    static thread_local std::unique_ptr<std::mt19937> tlsRng;

public:
    // Returns thread-local generator, initializing it with a unique seed on first access.
    static std::mt19937& Get()
    {
        if (!tlsRng)
        {
            uint32_t seed;
            {
                std::lock_guard<std::mutex> lock(seedMutex);
                seed = nextSeed++ + static_cast<uint32_t>(
                    std::chrono::high_resolution_clock::now().time_since_epoch().count());
            }
            tlsRng = std::make_unique<std::mt19937>(seed);
        }
        return *tlsRng;
    }

    // Random unit vector uniformly distributed over the sphere.
    static float3 Direction()
    {
        auto& rng = Get();
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        float3 v;
        do { v = float3(d(rng), d(rng), d(rng)); } while (v.length_sq() > 1.0f || v.length_sq() < 0.001f);
        return v.normalize();
    }

    // Random direction in the hemisphere defined by a surface normal.
    static float3 HemisphereDir(const float3& normal)
    {
        float3 d = Direction();
        if (d.dot(normal) < 0) d = d * -1.0f;
        return d;
    }
};

std::mutex ThreadSafeRandom::seedMutex;
uint32_t ThreadSafeRandom::nextSeed = 12345;
thread_local std::unique_ptr<std::mt19937> ThreadSafeRandom::tlsRng;

// =================================================================================================
// SECTION 3: PERFORMANCE PROFILER (OPTIONAL)
// -------------------------------------------------------------------------------------------------
// Lightweight instrumentation for measuring ray tracing cost.
// =================================================================================================
class PerformanceProfiler
{
    struct ThreadLocalData { uint64_t totalCalls = 0, totalTimeNS = 0, maxTimeNS = 0; };
    std::unordered_map<std::string, std::unordered_map<std::thread::id, ThreadLocalData>> data;
    std::mutex mutex;
    bool enabled = false;

public:
    void SetEnabled(bool e) { enabled = e; }
    bool IsEnabled() const { return enabled; }

    class ScopedTimer
    {
        PerformanceProfiler& profiler;
        const char* name;
        std::chrono::high_resolution_clock::time_point start;
        bool active;
    public:
        ScopedTimer(const char* n, PerformanceProfiler& p)
            : profiler(p), name(n), active(p.IsEnabled())
        {
            if (active) start = std::chrono::high_resolution_clock::now();
        }
        ~ScopedTimer()
        {
            if (active)
                profiler.Record(name,
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::high_resolution_clock::now() - start).count());
        }
    };

    void Record(const char* timerName, uint64_t ns)
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto& td = data[timerName][std::this_thread::get_id()];
        td.totalCalls++;
        td.totalTimeNS += ns;
        if (ns > td.maxTimeNS) td.maxTimeNS = ns;
    }
};

#define PROFILE_SCOPE(name) Presence::PerformanceProfiler::ScopedTimer __timer(name, profiler)

// =================================================================================================
// SECTION 4: MATERIAL SYSTEM
// -------------------------------------------------------------------------------------------------
// Stores acoustic parameters for each material type.
// =================================================================================================
class MaterialSystem
{
    std::vector<MaterialParams> materials;
    mutable std::shared_mutex mutex;

public:
    MaterialSystem()
    {
        materials.resize(static_cast<int>(MaterialType::Count));
        Set(static_cast<int>(MaterialType::Air), { 1.00f, 0.00f, 0.00f, 0.00f });
        Set(static_cast<int>(MaterialType::Stone), { 0.15f, 0.70f, 0.15f, 0.70f });
        Set(static_cast<int>(MaterialType::Metal), { 0.10f, 0.95f, 0.05f, 0.80f });
        Set(static_cast<int>(MaterialType::Wood), { 0.40f, 0.30f, 0.40f, 0.30f });
        Set(static_cast<int>(MaterialType::Soft), { 0.80f, 0.10f, 0.80f, 0.10f });
        Set(static_cast<int>(MaterialType::Glass), { 0.30f, 0.85f, 0.05f, 0.60f });
        Set(static_cast<int>(MaterialType::Absorber), { 0.00f, 0.01f, 0.99f, 0.01f });
    }

    bool Set(int id, const MaterialParams& p)
    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        if (id >= 0) {
            if (id >= static_cast<int>(materials.size())) materials.resize(id + 1);
            materials[id] = p;
            return true;
        }
        return false;
    }

    int Add(const MaterialParams& p)
    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        materials.push_back(p);
        return static_cast<int>(materials.size()) - 1;
    }

    MaterialParams Get(int id) const
    {
        std::shared_lock<std::shared_mutex> lock(mutex);
        return (id >= 0 && id < static_cast<int>(materials.size())) ? materials[id] : materials[0];
    }
};

// =================================================================================================
// SECTION 5: OCCLUSION CALCULATOR
// -------------------------------------------------------------------------------------------------
// Estimates direct sound occlusion between two points.
// =================================================================================================
class OcclusionCalculator
{
public:
    struct Config { float maxDistance = 80.0f; };

private:
    IGeometryProvider* provider;
    const MaterialSystem& materials;
    PerformanceProfiler& profiler;
    Config config;

    // Simple cache for recently computed occlusion values.
    struct CacheEntry { float occlusion; uint64_t lastFrame; float3 lPos, sPos; };
    mutable std::unordered_map<uint64_t, CacheEntry> cache;
    mutable std::unordered_map<uint64_t, float> history;
    mutable std::mutex cacheMutex;
    uint64_t currentFrame = 0;

public:
    OcclusionCalculator(IGeometryProvider* p, const MaterialSystem& m, PerformanceProfiler& pr)
        : provider(p), materials(m), profiler(pr) {}

    void SetConfig(const Config& c) { config = c; }

    // Periodic cache cleanup (called from the physics loop).
    void Tick()
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        currentFrame++;
        if (currentFrame % 1000 == 0)
        {
            for (auto it = cache.begin(); it != cache.end(); )
            {
                if (currentFrame - it->second.lastFrame > 30) it = cache.erase(it);
                else ++it;
            }
            if (history.size() > 2000) history.clear();
        }
    }

    // Returns occlusion factor [0..1] between two positions.
    float Calculate(const float3& listenerPos, const float3& sourcePos) const
    {
        float3 dir = sourcePos - listenerPos;
        float dist = dir.magnitude();
        if (dist < 0.5f) return 1.0f;
        if (dist > config.maxDistance) return 0.0f;
        dir = dir.normalize();

        RayHit hit = provider->CastRay(listenerPos, dir, dist);
        if (!hit.isHit) return 1.0f;

        float energy = materials.Get(hit.materialID).transmission;
        if (energy > 0.1f)
        {
            float3 p = listenerPos;
            p.mad(dir, hit.distance + 0.5f);
            float remaining = dist - hit.distance - 0.5f;
            if (remaining > 0)
            {
                RayHit h2 = provider->CastRay(p, dir, remaining);
                if (h2.isHit) energy *= materials.Get(h2.materialID).transmission;
            }
        }
        return std::max(energy, 0.0f);
    }
};

// =================================================================================================
// SECTION 6: MAIN SYSTEM IMPLEMENTATION (AudioSystem::Impl)
// -------------------------------------------------------------------------------------------------
// Contains the full acoustic simulation: ray tracing, statistical analysis, EAX mapping.
// =================================================================================================
class AudioSystem::Impl
{
public:
    // External dependencies
    IGeometryProvider* provider = nullptr;
    Settings settings;
    PerformanceProfiler profiler;
    MaterialSystem materials;

    // Occlusion sub-system
    std::unique_ptr<OcclusionCalculator> occlusion;

    // Background worker for asynchronous tracing
    std::unique_ptr<std::thread> workerThread;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> stopThread{ false };
    std::atomic<bool> workPending{ false };
    std::atomic<bool> initialized{ false };

    // Listener state
    float3 targetPos;
    float3 lastPos;
    float fog = 0.0f;                // 0 = clear, 1 = dense fog

    // Fibonacci sphere directions (64 rays)
    std::vector<float3> directions;

    // Exponentially smoothed acoustic metrics
    float smoothedMFP = 5.0f;
    float smoothedHitRatio = 0.5f;
    float smoothedReflectivity = 0.3f;

    // Per-frame context for ray tracing statistics
    struct AnalysisContext
    {
        float3 cameraPosition;
        int frameCount = 0;

        // Total geometric distance (for diagnostics)
        float rawTotalDist = 0.0f;
        int totalSegments = 0;

        // Energy-weighted distance (legacy, not used in final model)
        float energyWeightedDist = 0.0f;

        // First-bounce statistics
        int firstHitCount = 0;        // how many rays hit geometry on the first bounce
        float firstHitDistSum = 0.0f; // sum of those distances

        // Material hit counters (per frame, cleared each Reset())
        int materialHits[MAX_TRACKED_MATERIALS] = { 0 };

        // Computed physical quantities (updated after smoothing)
        float physicalVolume = 100.0f;
        float meanFreePath = 5.0f;
        float physicalReflectivity = 0.3f;
        float geometricOpenness = 0.5f;
        float geometricEnclosedness = 0.5f;
        float reverbTime = 1.0f;
        float perceivedReflectivity = 0.0f;

        void Reset()
        {
            frameCount = 0;
            rawTotalDist = 0.0f;
            totalSegments = 0;
            energyWeightedDist = 0.0f;
            firstHitCount = 0;
            firstHitDistSum = 0.0f;
            std::memset(materialHits, 0, sizeof(materialHits));
        }

        static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
        static float Clamp(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }
    } context;

    // EAX results
    EAXResult targetEAX;
    EAXResult currentEAX;

    ~Impl() { Shutdown(); }

    // ---------------------------------------------------------------------------------------------
    // Initialization and shutdown
    // ---------------------------------------------------------------------------------------------
    void Init(IGeometryProvider* p, const Settings& s)
    {
        provider = p;
        settings = s;
        occlusion = std::make_unique<OcclusionCalculator>(p, materials, profiler);
        initialized = true;
        context.Reset();

        // Generate 64 uniformly distributed directions using Fibonacci sphere.
        constexpr int N = 64;
        directions.resize(N);
        const float goldenRatio = (1.0f + std::sqrt(5.0f)) / 2.0f;
        for (int i = 0; i < N; ++i)
        {
            float theta = 2.0f * PI * i / goldenRatio;   // azimuth
            float phi = std::acos(1.0f - 2.0f * (i + 0.5f) / N); // polar angle
            directions[i] = float3(std::sin(phi) * std::cos(theta),
                std::cos(phi),
                std::sin(phi) * std::sin(theta));
        }

        if (settings.useMultithreading)
            workerThread = std::make_unique<std::thread>(&Impl::PhysicsLoop, this);
    }

    void Shutdown()
    {
        initialized = false;
        if (workerThread)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                stopThread = true;
            }
            cv.notify_all();
            if (workerThread->joinable()) workerThread->join();
            workerThread.reset();
        }
    }

    // ---------------------------------------------------------------------------------------------
    // EAX Parameter Mapping
    // ---------------------------------------------------------------------------------------------
    // Translates the physically computed metrics (MFP, reflectivity, openness) into EAX parameters.
    // All transitions are continuous – no hard thresholds.
    // ---------------------------------------------------------------------------------------------
    void ComputeEAX()
    {
        EAXResult eax;
        eax.isValid = true;

        // Debug outputs
        eax.debugEnclosedness = context.geometricEnclosedness;
        eax.debugOpenness = context.geometricOpenness;
        eax.debugMeanFreePath = context.meanFreePath;
        eax.debugPhysicalVolume = context.physicalVolume;

        // Environment size = cubic root of the estimated physical volume
        eax.flEnvironmentSize = std::pow(context.physicalVolume, 1.0f / 3.0f);

        // Convenient local aliases
        const float fogVal = AnalysisContext::Clamp(fog, 0.0f, 1.0f);
        const float open = context.geometricOpenness;          // 0 = fully enclosed, 1 = open field
        const float enclosed = context.geometricEnclosedness;      // 1 = fully enclosed
        const float percRefl = context.perceivedReflectivity;      // sqrt(physicalReflectivity)
        const float mfp = context.meanFreePath;
        const float rt60 = context.reverbTime;

        // Smooth suppression factor: 1.0 when enclosed, 0.15 when completely open.
        const float opennessSuppression = 1.0f - 0.85f * open;

        // ---- 1. Room Level (overall reverb volume) ----
        float roomInt = (0.3f + 0.7f * enclosed) * percRefl;  // base intensity
        roomInt *= opennessSuppression;                        // continuous reduction by openness
        roomInt = AnalysisContext::Clamp(roomInt - fogVal * 0.4f, 0.0f, 1.1f);

        float room_mB = AnalysisContext::Lerp(-4000.0f, 400.0f, roomInt);

        // Boost for very small, enclosed rooms
        if (context.physicalVolume < 70.0f && enclosed > 0.8f)
            room_mB += 500.0f;

        eax.lRoom = static_cast<int32_t>(AnalysisContext::Clamp(room_mB, -10000.0f, 600.0f));
        eax.lRoom = std::min(eax.lRoom, 0);   // EAX allows 0 as maximum

        // ---- 2. Decay Time (RT60) ----
        eax.flDecayTime = rt60;
        if (fogVal > 0.5f) eax.flDecayTime *= 0.8f;

        // ---- 3. Early Reflections ----
        float reflVolume = context.physicalReflectivity * opennessSuppression;
        float refl_mB = AnalysisContext::Lerp(-2500.0f, 200.0f, reflVolume);
        eax.lReflections = static_cast<int32_t>(AnalysisContext::Clamp(refl_mB, -10000.0f, 500.0f));

        float reflDelay = mfp / SOUND_SPEED;
        if (open > 0.6f) reflDelay *= 1.5f;
        eax.flReflectionsDelay = AnalysisContext::Clamp(reflDelay, 0.0f, 0.3f);

        // ---- 4. Late Reverberation Tail ----
        float reverb_mB = AnalysisContext::Lerp(-3000.0f, 0.0f, roomInt);
        if (open > 0.7f) reverb_mB -= 600.0f;
        eax.lReverb = static_cast<int32_t>(AnalysisContext::Clamp(reverb_mB, -10000.0f, 200.0f));
        eax.flReverbDelay = AnalysisContext::Clamp(eax.flReflectionsDelay + 0.02f, 0.0f, 0.1f);

        // ---- 5. High-frequency behaviour ----
        const float hfLoss = -100.0f;
        const float outdoorHF = open * -1200.0f;
        float roomHF_mB = static_cast<float>(eax.lRoom) + hfLoss + outdoorHF;
        eax.lRoomHF = static_cast<int32_t>(AnalysisContext::Clamp(roomHF_mB, -10000.0f, -100.0f));

        eax.flDecayHFRatio = AnalysisContext::Lerp(0.83f, 0.5f, open);
        eax.flEnvironmentDiffusion = AnalysisContext::Lerp(1.0f, 0.6f,
            AnalysisContext::Clamp(open * 1.5f, 0.0f, 1.0f));
        eax.flAirAbsorptionHF = -5.0f + open * -8.0f - fogVal * 5.0f;
        eax.flRoomRolloffFactor = 0.0f;

        // Thread-safe publication
        {
            std::lock_guard<std::mutex> lock(mutex);
            targetEAX = eax;
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Background Physics Loop
    // ---------------------------------------------------------------------------------------------
    // 1. Traces 64 rays with up to settings.maxBounces reflections.
    // 2. Computes per-frame statistics (hit ratio, MFP, reflectivity).
    // 3. Applies exponential smoothing to stabilise metrics.
    // 4. Derives physical volume and reverb time (simplified Eyring formula).
    // 5. Calls ComputeEAX to map to EAX parameters.
    // ---------------------------------------------------------------------------------------------
    void PhysicsLoop()
    {
        bool firstFrame = true;

        while (!stopThread)
        {
            // Wait for a new listener position
            float3 curPos;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this] { return workPending || stopThread; });
                if (stopThread) break;
                curPos = targetPos;
                workPending = false;
            }

            // Start fresh per-frame context
            context.Reset();
            context.cameraPosition = curPos;

            // ---- Ray Tracing ----
            {
                PROFILE_SCOPE("EAX::Trace");

                const int numRays = static_cast<int>(directions.size());
                for (int i = 0; i < numRays; ++i)
                {
                    float energy = 1.0f;
                    float3 pos = curPos;
                    float3 dir = directions[i];

                    for (int bounce = 0; bounce < settings.maxBounces; ++bounce)
                    {
                        RayHit hit = provider->CastRay(pos, dir, settings.maxRayDistance);
                        if (!hit.isHit)
                        {
                            // Ray escaped – treat as infinite distance (capped by maxRayDistance)
                            context.rawTotalDist += settings.maxRayDistance;
                            context.totalSegments++;
                            context.energyWeightedDist += settings.maxRayDistance * energy;
                            break;
                        }

                        context.rawTotalDist += hit.distance;
                        context.totalSegments++;
                        context.energyWeightedDist += hit.distance * energy;

                        if (bounce == 0)
                        {
                            context.firstHitCount++;
                            context.firstHitDistSum += hit.distance;
                        }

                        if (hit.materialID >= 0 && hit.materialID < MAX_TRACKED_MATERIALS)
                            context.materialHits[hit.materialID]++;

                        energy *= (1.0f - materials.Get(hit.materialID).absorption);
                        if (energy < 0.05f) break;   // negligible contribution

                        pos.mad(dir, hit.distance - 0.05f);
                        dir = ThreadSafeRandom::HemisphereDir(hit.normal);   // diffuse reflection
                    }
                }
            }

            // ---- Per-frame statistics ----
            const float numRaysF = static_cast<float>(directions.size());
            const float hitCountF = static_cast<float>(context.firstHitCount);

            // Hit ratio (fraction of rays that immediately hit geometry)
            float rawHitRatio = hitCountF / numRaysF;

            // Hybrid Mean Free Path: treats misses as maxRayDistance.
            float rawMFP = (context.firstHitDistSum + (numRaysF - hitCountF) * settings.maxRayDistance) / numRaysF;

            // Material-weighted average reflectivity
            float totalHits = 0.0f, accumRefl = 0.0f;
            for (int i = 0; i < MAX_TRACKED_MATERIALS; ++i)
            {
                if (context.materialHits[i] > 0)
                {
                    float h = static_cast<float>(context.materialHits[i]);
                    totalHits += h;
                    accumRefl += h * materials.Get(i).rt60_weight;
                }
            }
            float rawReflectivity = (totalHits > 0.0f) ? (accumRefl / totalHits) : 0.1f;
            float rawOpenness = 1.0f - rawHitRatio;
            rawReflectivity *= (1.0f - rawOpenness * 0.8f);
            rawReflectivity = AnalysisContext::Clamp(rawReflectivity, 0.05f, 0.95f);

            // ---- Exponential Smoothing ----
            const float smoothing = 0.2f;  // time constant ~5 frames
            if (firstFrame)
            {
                smoothedMFP = rawMFP;
                smoothedHitRatio = rawHitRatio;
                smoothedReflectivity = rawReflectivity;
                firstFrame = false;
            }
            else
            {
                smoothedMFP = smoothedMFP * (1.0f - smoothing) + rawMFP * smoothing;
                smoothedHitRatio = smoothedHitRatio * (1.0f - smoothing) + rawHitRatio * smoothing;
                smoothedReflectivity = smoothedReflectivity * (1.0f - smoothing) + rawReflectivity * smoothing;
            }

            // ---- Apply smoothed values to context ----
            context.meanFreePath = smoothedMFP;
            context.geometricEnclosedness = smoothedHitRatio;
            context.geometricOpenness = 1.0f - smoothedHitRatio;
            context.physicalReflectivity = smoothedReflectivity;
            context.perceivedReflectivity = std::sqrt(smoothedReflectivity);

            // ---- Physical Volume Estimation ----
            // Using a cubic approximation: V = 8 * MFP^3  (works for typical convex rooms).
            float estVol = 8.0f * std::pow(smoothedMFP, 3.0f);
            if (context.geometricOpenness > 0.6f)
                estVol = std::min(estVol, 50000.0f);   // cap for open areas
            context.physicalVolume = AnalysisContext::Clamp(estVol, 10.0f, 500000.0f);

            // ---- Reverberation Time (RT60) ----
            // Based on simplified Eyring formula: RT60 = 0.04025 * MFP / (-ln(1 - α))
            // where α = 1 - physicalReflectivity.
            float absCoeff = AnalysisContext::Clamp(1.0f - smoothedReflectivity, 0.01f, 0.99f);
            float rt60 = 0.04025f * smoothedMFP / (-std::log(1.0f - absCoeff));
            // Openness reduces RT60 (energy escapes).
            rt60 *= (1.0f - 0.95f * context.geometricOpenness);
            context.reverbTime = AnalysisContext::Clamp(rt60, 0.05f, 8.0f);

            // ---- Generate EAX parameters ----
            ComputeEAX();
            if (occlusion) occlusion->Tick();

            std::this_thread::sleep_for(std::chrono::milliseconds(33));   // ~30 updates/sec
        }
    }
};

// =================================================================================================
// SECTION 7: PUBLIC API (AudioSystem)
// -------------------------------------------------------------------------------------------------
// Thin wrappers forwarding to the Impl.
// =================================================================================================

AudioSystem::AudioSystem() : m_Impl(new Impl()) {}
AudioSystem::~AudioSystem() { Shutdown(); delete m_Impl; }

void AudioSystem::Initialize(IGeometryProvider* p, const Settings& s)
{
    if (m_Impl) m_Impl->Init(p, s);
}

void AudioSystem::Shutdown()
{
    if (m_Impl) m_Impl->Shutdown();
}

// Linear interpolation of EAX parameters with per-frame smoothing.
template <typename T>
static T LerpEAX(T current, T target, float step)
{
    float diff = static_cast<float>(target - current);
    if (std::abs(diff) < 0.1f) return target;
    return static_cast<T>(static_cast<float>(current) + diff * step);
}

void AudioSystem::Update(const float3& pos, float dt, float fogVal)
{
    if (!m_Impl || !m_Impl->initialized) return;
    m_Impl->fog = fogVal;

    // Send position to background worker.
    if (m_Impl->settings.useMultithreading)
    {
        {
            std::lock_guard<std::mutex> lock(m_Impl->mutex);
            m_Impl->targetPos = pos;
            m_Impl->workPending = true;
        }
        m_Impl->cv.notify_one();
    }

    EAXResult target;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        target = m_Impl->targetEAX;
    }

    // Smoothly interpolate current EAX values towards target.
    if (target.isValid)
    {
        float speed = 2.0f * dt;
        if (std::abs(target.lRoom - m_Impl->currentEAX.lRoom) > 1000) speed = 5.0f * dt;

        auto& c = m_Impl->currentEAX;
        c.lRoom = LerpEAX(c.lRoom, target.lRoom, speed);
        c.lRoomHF = LerpEAX(c.lRoomHF, target.lRoomHF, speed);
        c.lReflections = LerpEAX(c.lReflections, target.lReflections, speed);
        c.lReverb = LerpEAX(c.lReverb, target.lReverb, speed);
        c.flDecayTime = LerpEAX(c.flDecayTime, target.flDecayTime, speed);
        c.flEnvironmentSize = LerpEAX(c.flEnvironmentSize, target.flEnvironmentSize, speed);

        c.flDecayHFRatio = target.flDecayHFRatio;
        c.flReflectionsDelay = target.flReflectionsDelay;
        c.flReverbDelay = target.flReverbDelay;
        c.flAirAbsorptionHF = target.flAirAbsorptionHF;
        c.flEnvironmentDiffusion = target.flEnvironmentDiffusion;
        c.debugEnclosedness = target.debugEnclosedness;
        c.debugOpenness = target.debugOpenness;
        c.debugMeanFreePath = target.debugMeanFreePath;
        c.debugPhysicalVolume = target.debugPhysicalVolume;
        c.isValid = true;
    }
}

EAXResult AudioSystem::GetEAXResult() const
{
    return m_Impl ? m_Impl->currentEAX : EAXResult();
}

bool AudioSystem::SetMaterialProperties(int id, const MaterialParams& p)
{
    return m_Impl && m_Impl->materials.Set(id, p);
}

int AudioSystem::CreateCustomMaterial(const MaterialParams& p)
{
    return m_Impl ? m_Impl->materials.Add(p) : -1;
}

float AudioSystem::CalculateOcclusion(const float3& listenerPos, const float3& sourcePos)
{
    if (!m_Impl || !m_Impl->occlusion) return 1.0f;
    return m_Impl->occlusion->Calculate(listenerPos, sourcePos);
}

PRESENCE_END
