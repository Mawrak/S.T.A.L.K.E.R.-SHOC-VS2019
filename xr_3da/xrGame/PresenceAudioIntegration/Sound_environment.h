/*
====================================================================================================
  Presence Audio SDK Integration for X-Ray Engine
  File: Sound_environment.h
====================================================================================================

  This header declares the CSoundEnvironment class, which acts as the main
  bridge between the X-Ray engine and the Presence Audio SDK. It implements
  the ISoundOcclusionCalculator interface so the engine's sound renderer can
  query occlusion values, and it owns the SDK's AudioSystem object as well as
  a geometry adapter that translates the game world into a format the SDK
  understands.

  Console variables (defined in Sound_environment.cpp) allow runtime tuning
  of the acoustic simulation.  The class also provides pause/resume controls
  and an on-the-fly material reload command.

====================================================================================================

  Copyright (c) 2026 Presence Collaboratory, NSDeathman & Gemini 3 & DeepSeek

====================================================================================================

  Developed by: NSDeathman (Architecture & Core), Gemini 3 (Optimization & Math)
  Organization: Presence Collaboratory
====================================================================================================
*/
#pragma once

#include <PresenceAudioSDK\Include\PresenceAudioAPI.h>
#include "Sound_environment_common.h"

class XRayGeometryAdapter;   // Forward declaration – implemented in Sound_environment_geometry_provider.cpp

// -------------------------------------------------------------------------------------------------
// Console Variables (Exposed to the engine's console)
// -------------------------------------------------------------------------------------------------
// These allow designers and testers to tune the audio simulation at runtime.
// Default values are set in Sound_environment.cpp.
// -------------------------------------------------------------------------------------------------
extern float g_fPresenceRayDist;       // Maximum length of an acoustic ray (metres)
extern int   g_iPresenceBounces;       // Maximum number of ray bounces (reflections)
extern bool  g_bPresenceThreads;       // Run simulation on a background thread
extern float g_fPresenceUpdateRate;    // Target simulation frequency (~30 Hz)

/**
 * @brief Main integration class that owns the Presence Audio SDK instance.
 *
 * CSoundEnvironment is created by the engine once and lives for the entire
 * game session.  It:
 *   - Loads/unloads the SDK DLL
 *   - Starts and stops the real‑time acoustic simulation on level transitions
 *   - Provides per‑frame listener position to the SDK and retrieves EAX parameters
 *   - Implements ISoundOcclusionCalculator so the engine can query direct‑path occlusion
 *   - Exposes pause/play controls for debugging or cutscenes
 */
class CSoundEnvironment : public Presence::ISoundOcclusionCalculator
{
private:
    // Handle to the dynamically loaded PresenceAudioSDK.dll
    HMODULE hPresenceAudioSDKLib;

    // Legacy EAX data structure used by the engine's sound driver
    SEAXEnvironmentData m_CurrentData;

    // Core SDK components
    Presence::AudioSystem* m_pAudioSystem;       // Acoustic simulation engine
    XRayGeometryAdapter* m_pGeometryAdapter;   // Translates X-Ray geometry to SDK format

    // State flags
    bool m_bLoaded;   // True when a level is loaded and the simulation is initialized
    bool m_bEnabled;  // True when the user has not disabled EAX via console
    bool m_bPaused;   // True when the simulation is temporarily paused (cutscenes, menus)

public:
    CSoundEnvironment();
    ~CSoundEnvironment();

    // ---------------------------------------------------------------------------------------------
    // Lifecycle – called by the engine on level load/unload and every frame
    // ---------------------------------------------------------------------------------------------

    void Update();         // Called once per frame to feed listener data and apply EAX
    void OnLevelLoad();    // Called after a level is fully loaded
    void OnLevelUnload();  // Called before a level is destroyed

    // ---------------------------------------------------------------------------------------------
    // Runtime control
    // ---------------------------------------------------------------------------------------------

    /** @brief Pause the acoustic simulation (e.g. during a cutscene). */
    void Pause() { m_bPaused = true; }

    /** @brief Resume the simulation after a pause. */
    void Play() { m_bPaused = false; }

    /**
     * @brief Force a rebuild of the material cache.
     *
     * This can be invoked from a console command after editing material properties,
     * avoiding a full level restart.
     */
    void ReloadMaterials();

    // ---------------------------------------------------------------------------------------------
    // EAX data transfer
    // ---------------------------------------------------------------------------------------------

    /**
     * @brief Copies the SDK's EAXResult into the legacy SEaxEnvironmentData and
     *        commits it to the engine's sound driver.
     */
    void ApplyToSoundDriver(const Presence::EAXResult& res);

    // ---------------------------------------------------------------------------------------------
    // ISoundOcclusionCalculator interface
    // ---------------------------------------------------------------------------------------------

    /**
     * @brief Computes the occlusion factor for a given sound source.
     * @param listenerPos  Position of the listener (camera).
     * @param sourcePos    Position of the sound emitter.
     * @return Occlusion factor in [0.0, 1.0]: 1.0 = fully audible, 0.0 = completely blocked.
     */
    virtual float CalculateOcclusion(const Presence::float3& listenerPos,
        const Presence::float3& sourcePos) override;

    // ---------------------------------------------------------------------------------------------
    // Debug helpers
    // ---------------------------------------------------------------------------------------------

    /** @brief Returns true when the simulation is active and ready to provide valid data. */
    bool IsReady() const { return m_bLoaded && m_pAudioSystem != nullptr; }
};
