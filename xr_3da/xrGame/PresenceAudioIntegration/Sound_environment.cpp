/*
====================================================================================================
  Presence Audio SDK Integration for X-Ray Engine
  File: Sound_environment.cpp
====================================================================================================

  This file is a bridge between the X‑Ray game engine and the Presence Audio SDK.
  It manages the lifetime of the audio system, passes the listener position to the
  SDK, retrieves the computed EAX parameters, and forwards them to the engine’s
  sound renderer.  Console variables allow runtime tuning of the acoustic simulation.
====================================================================================================

  Copyright (c) 2026 Presence Collaboratory, NSDeathman & Gemini 3

====================================================================================================

  Developed by: NSDeathman (Architecture & Core), Gemini 3 (Optimization & Math)
  Organization: Presence Collaboratory
====================================================================================================
*/

// -------------------------------------------------------------------------------------------------
// Includes
// -------------------------------------------------------------------------------------------------

#include <PresenceAudioSDK\Include\PresenceAudioAPI.h>
#include "stdafx.h"
#pragma hdrstop

#include "Sound_environment.h"
#include "Sound_environment_geometry_provider.h"
#include "../../IGame_Level.h"
#include "../../IGame_Persistent.h"
#include "../../Environment.h"
#include "../../../xrSound/SoundRender_Core.h"

#pragma comment(lib, "PresenceAudioSDK.lib")

// -------------------------------------------------------------------------------------------------
// Console Variables (Exposed to the engine’s console)
// -------------------------------------------------------------------------------------------------
// These can be changed at runtime to tweak the acoustic simulation.
// Defaults are chosen for a balance between quality and performance.
// -------------------------------------------------------------------------------------------------

float g_fPresenceRayDist = 300.0f;   // Maximum ray length (metres).
int   g_iPresenceBounces = 8;        // Maximum number of acoustic reflections.
bool  g_bPresenceThreads = true;     // Run the simulation on a background thread.
float g_fPresenceUpdateRate = 0.033f;   // Target update interval (~30 Hz).

// =================================================================================================
// Constructor / Destructor
// =================================================================================================

/**
 * @brief Loads the Presence Audio DLL, creates the AudioSystem and the X‑Ray geometry adapter.
 *
 * If the DLL cannot be loaded, the module remains inactive but does not crash the engine.
 */
CSoundEnvironment::CSoundEnvironment()
    : m_pAudioSystem(nullptr)
    , m_pGeometryAdapter(nullptr)
    , m_bLoaded(false)
    , m_bEnabled(false)
    , m_bPaused(false)
{
    Msg("[Presence Audio] Initializing SDK wrapper...");

    // Dynamically load the SDK library.
    LPCSTR LibName = "PresenceAudioSDK.dll";
    hPresenceAudioSDKLib = LoadLibrary(LibName);
    if (!hPresenceAudioSDKLib)
    {
        Msg("! [Presence Audio] Error: Can't load %s", LibName);
        return;
    }

    ZeroMemory(&m_CurrentData, sizeof(m_CurrentData));

    m_pAudioSystem = new Presence::AudioSystem();
    m_pGeometryAdapter = new XRayGeometryAdapter();

    Msg("- Presence Audio SDK loaded.");
}

/**
 * @brief Shuts down the simulation, releases resources and unloads the DLL.
 */
CSoundEnvironment::~CSoundEnvironment()
{
    if (m_bLoaded)
        OnLevelUnload();

    if (m_pAudioSystem)
    {
        m_pAudioSystem->Shutdown();
        delete m_pAudioSystem;
        m_pAudioSystem = nullptr;
    }
    if (m_pGeometryAdapter)
    {
        delete m_pGeometryAdapter;
        m_pGeometryAdapter = nullptr;
    }
    if (hPresenceAudioSDKLib)
    {
        FreeLibrary(hPresenceAudioSDKLib);
        hPresenceAudioSDKLib = 0;
    }
}

// =================================================================================================
// Lifecycle – Called by the engine when a level is loaded / unloaded
// =================================================================================================

/**
 * @brief Called when a new level is fully loaded.
 *
 * Builds the material cache, initialises the audio system with the current
 * console settings, and hooks into the engine's sound renderer.
 */
void CSoundEnvironment::OnLevelLoad()
{
    if (!m_pAudioSystem || !m_pGeometryAdapter)
        return;

    Msg("[Presence Audio] Starting simulation...");

    // Translate console variables to the SDK settings structure.
    Presence::Settings s;
    s.maxBounces = g_iPresenceBounces;
    s.maxRayDistance = g_fPresenceRayDist;
    s.useMultithreading = g_bPresenceThreads;
    s.updateInterval = g_fPresenceUpdateRate;

    // Cache material information from the current level.
    m_pGeometryAdapter->BuildMaterialCache(m_pAudioSystem);

    // Initialise the audio system (starts the background worker thread).
    m_pAudioSystem->Initialize(m_pGeometryAdapter, s);
    Msg("[Presence Audio] Core Version: %s", Presence::GetVersionString());

    m_bLoaded = true;
    m_bEnabled = true;

    // Hook into the engine's sound subsystem to provide occlusion data.
    if (::Sound)
    {
        CSoundRender_Core* pCore = (CSoundRender_Core*)::Sound;
        pCore->SetOcclusion(this);
        Msg("[Presence Audio] Hooked into xrSound.");
    }
}

/**
 * @brief Called before a level is unloaded.
 *
 * Unhooks from the sound renderer and shuts down the background simulation.
 */
void CSoundEnvironment::OnLevelUnload()
{
    // Important: unhook before destroying the audio system.
    if (::Sound)
    {
        CSoundRender_Core* pCore = (CSoundRender_Core*)::Sound;
        pCore->SetOcclusion(nullptr);
    }

    if (m_pAudioSystem)
    {
        m_pAudioSystem->Shutdown();
    }

    m_bLoaded = false;
    Msg("[Presence Audio] Simulation stopped.");
}

/**
 * @brief Reloads material data without restarting the simulation.
 *
 * Useful when material properties are modified at runtime.
 */
void CSoundEnvironment::ReloadMaterials()
{
    if (!m_bLoaded || !m_pAudioSystem || !m_pGeometryAdapter)
        return;

    Msg("[Presence Audio] Hot-reloading materials...");
    m_pGeometryAdapter->m_bCacheBuilt = false;
    m_pGeometryAdapter->BuildMaterialCache(m_pAudioSystem);
}

// =================================================================================================
// Occlusion Interface (ISoundOcclusionCalculator)
// =================================================================================================

/**
 * @brief Computes the direct‑path occlusion between two points.
 *
 * Called by the engine’s sound thread for every playable sound source.
 * @param listenerPos  Listener (camera) position.
 * @param sourcePos    Position of the sound emitter.
 * @return Occlusion factor: 1.0 = fully audible, 0.0 = completely blocked.
 */
float CSoundEnvironment::CalculateOcclusion(const Presence::float3& listenerPos,
    const Presence::float3& sourcePos)
{
    if (!m_bLoaded || !m_pAudioSystem || m_bPaused)
        return 1.0f;   // If the system is not active, assume no occlusion.

    return m_pAudioSystem->CalculateOcclusion(listenerPos, sourcePos);
}

// =================================================================================================
// Per‑Frame Update
// =================================================================================================

/**
 * @brief Called every frame by the engine.
 *
 * 1. Checks if the system should be active (console flag, level loaded, not paused).
 * 2. Reads the current camera position and fog density.
 * 3. Sends the data to the audio SDK.
 * 4. Retrieves the computed EAX parameters and pushes them to the sound driver.
 */
void CSoundEnvironment::Update()
{
    // --- State checks ---
    if (!psSoundFlags.test(ss_EAX) || !m_bLoaded || !m_pAudioSystem || m_bPaused)
    {
        if (m_bEnabled)
        {
            // The user turned off EAX at runtime – reset all effects to neutral.
            Presence::EAXResult emptyRes;
            ApplyToSoundDriver(emptyRes);
            m_bEnabled = false;
        }
        return;
    }

    if (!g_pGameLevel || !g_pGamePersistent)
        return;

    if (!m_bEnabled)
        m_bEnabled = true;

    // --- Gather environment data ---
    Fvector pos = Device.vCameraPosition;

    // Convert X‑Ray coordinates to Presence coordinates.
    // Both use Y‑up, so no transformation is needed.
    Presence::float3 camPos(pos.x, pos.y, pos.z);

    float dt = Device.fTimeDelta;

    // Retrieve fog density (0 = clear, 1 = thick fog).
    // This value can be used by the SDK to adjust high‑frequency absorption.
    float fog_density = 0.0f;
    // if (g_pGamePersistent)
    //     fog_density = g_pGamePersistent->Environment().CurrentEnv.fog_density;

    // --- Feed the SDK ---
    m_pAudioSystem->Update(camPos, dt, fog_density);

    // --- Retrieve and apply EAX ---
    Presence::EAXResult res = m_pAudioSystem->GetEAXResult();
    if (res.isValid)
    {
        ApplyToSoundDriver(res);
    }
}

// =================================================================================================
// Helper: push EAXResult to the engine’s sound renderer
// =================================================================================================

/**
 * @brief Copies the SDK result into the legacy EAX structure and commits it.
 *
 * The dwFlags member tells the driver which fields contain valid data.
 * Here we set almost all flags (0x3F) because the SDK fills every relevant field.
 */
void CSoundEnvironment::ApplyToSoundDriver(const Presence::EAXResult& res)
{
    m_CurrentData.lRoom = res.lRoom;
    m_CurrentData.lRoomHF = res.lRoomHF;
    m_CurrentData.flRoomRolloffFactor = res.flRoomRolloffFactor;
    m_CurrentData.flDecayTime = res.flDecayTime;
    m_CurrentData.flDecayHFRatio = res.flDecayHFRatio;
    m_CurrentData.lReflections = res.lReflections;
    m_CurrentData.flReflectionsDelay = res.flReflectionsDelay;
    m_CurrentData.lReverb = res.lReverb;
    m_CurrentData.flReverbDelay = res.flReverbDelay;
    m_CurrentData.flEnvironmentSize = res.flEnvironmentSize;
    m_CurrentData.flEnvironmentDiffusion = res.flEnvironmentDiffusion;
    m_CurrentData.flAirAbsorptionHF = res.flAirAbsorptionHF;
    m_CurrentData.dwFlags = 0x3F;   // EAX_ALL (most parameters active)

    if (::Sound)
        ::Sound->commit_eax(&m_CurrentData);
}
