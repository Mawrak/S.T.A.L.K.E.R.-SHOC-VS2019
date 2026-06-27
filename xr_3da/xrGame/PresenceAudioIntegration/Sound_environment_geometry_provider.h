/*
====================================================================================================
  Presence Audio SDK Integration for X-Ray Engine
  File: Sound_environment_geometry_provider.h
====================================================================================================

  This header implements the XRayGeometryAdapter class, which bridges the
  engine's geometry and material systems with the Presence Audio SDK.
  It implements the IGeometryProvider interface, allowing the SDK to trace
  acoustic rays against the static level geometry and to query material
  properties for every hit point.

  The adapter also manages a material cache that maps engine material
  identifiers to Presence material types, using both an external LTX
  configuration file and heuristic name‑based rules.

====================================================================================================

  Copyright (c) 2026 Presence Collaboratory, NSDeathman & Gemini 3

  Permission is hereby granted … [full license text preserved from original]
====================================================================================================

  Developed by: NSDeathman (Architecture & Core), Gemini 3 (Optimization & Math)
  Organization: Presence Collaboratory
====================================================================================================
*/
#pragma once

#include "stdafx.h"
#include <PresenceAudioSDK\Include\PresenceAudioAPI.h>
#include "../../igame_level.h"
#include "../../xr_area.h"
#include "..\GameMtlLib.h"

/**
 * @brief Adapter that translates X‑Ray engine geometry into the format required by the Presence SDK.
 *
 * Responsibilities:
 *   - Build a material cache that maps every engine material to a Presence material ID.
 *   - Implement CastRay() using the engine's ObjectSpace.RayPick, providing hit distance,
 *     surface normal and material identifier for each acoustic ray.
 */
class XRayGeometryAdapter : public Presence::IGeometryProvider
{
public:
    /**
     * @brief Material cache: element i stores the Presence material ID for engine material i.
     */
    xr_vector<int> m_MaterialCache;

    /** True after BuildMaterialCache has successfully completed. */
    bool m_bCacheBuilt;

    XRayGeometryAdapter() : m_bCacheBuilt(false)
    {
    }

    // =============================================================================================
    // Material defaults
    // =============================================================================================

    /**
     * @brief Returns sensible default acoustic parameters for the basic material types.
     *
     * These values are used when no custom LTX configuration is provided.
     */
    Presence::MaterialParams GetDefaultParams(Presence::MaterialType type)
    {
        switch (type)
        {
        case Presence::MaterialType::Stone:    return { 0.05f, 0.60f, 0.10f, 0.8f };
        case Presence::MaterialType::Metal:    return { 0.00f, 0.85f, 0.05f, 0.9f };
        case Presence::MaterialType::Wood:     return { 0.15f, 0.25f, 0.30f, 0.5f };
        case Presence::MaterialType::Soft:     return { 0.50f, 0.05f, 0.90f, 0.1f };
        case Presence::MaterialType::Glass:    return { 0.70f, 0.40f, 0.05f, 0.2f };
        case Presence::MaterialType::Absorber: return { 0.01f, 0.00f, 1.00f, 0.0f };
        default:                               return { 0.99f, 0.00f, 0.00f, 0.0f }; // Air
        }
    }

    // =============================================================================================
    // Material cache construction
    // =============================================================================================

    /**
     * @brief Builds (or rebuilds) the material cache.
     *
     * 1. Registers default parameters for all basic material types.
     * 2. Optionally loads an LTX configuration file ("presence_audio_materials.ltx")
     *    that can override or extend material properties.
     * 3. Maps every engine material (from GMLib) to a Presence material ID,
     *    using either the LTX settings or a set of heuristic rules based on the
     *    material name.
     */
    void BuildMaterialCache(Presence::AudioSystem* pSystem)
    {
        if (!pSystem) return;

        // Clear any previous cache data (allows hot reload).
        m_MaterialCache.clear();

        Msg("[Presence Audio] Building material cache...");

        // ---- 1. Register the basic material types ----
        for (int i = 0; i < static_cast<int>(Presence::MaterialType::Count); i++)
        {
            pSystem->SetMaterialProperties(i,
                GetDefaultParams(static_cast<Presence::MaterialType>(i)));
        }

        // ---- 2. Attempt to load the optional LTX config ----
        string_path configPath;
        FS.update_path(configPath, "$game_config$", "presence_audio_materials.ltx");
        CInifile* pConfig = FS.exist(configPath)
            ? new CInifile(configPath, TRUE, TRUE, FALSE)
            : nullptr;

        if (pConfig)
            Msg("[Presence Audio] Loaded config: %s", configPath);
        else
            Msg("! [Presence Audio] Config not found, using heuristics.");

        // ---- 3. Map every engine material ----
        u32 mtlCount = GMLib.CountMaterial();
        m_MaterialCache.resize(mtlCount);

        int customCount = 0;

        for (u32 i = 0; i < mtlCount; i++)
        {
            SGameMtl* mtl = GMLib.GetMaterialByIdx(i);
            if (!mtl)
            {
                m_MaterialCache[i] = static_cast<int>(Presence::MaterialType::Stone);
                continue;
            }

            LPCSTR name = mtl->m_Name.c_str();
            int finalID = static_cast<int>(Presence::MaterialType::Stone); // default fallback

            // --- A. Check for an explicit LTX section ---
            if (pConfig && pConfig->section_exist(name))
            {
                Presence::MaterialParams p;
                p.transmission = pConfig->r_float(name, "transmission");
                p.reflectivity = pConfig->r_float(name, "reflectivity");
                p.absorption = pConfig->r_float(name, "absorption");
                p.rt60_weight = pConfig->line_exist(name, "rt60_weight")
                    ? pConfig->r_float(name, "rt60_weight")
                    : p.reflectivity;

                finalID = pSystem->CreateCustomMaterial(p);
                customCount++;
            }
            // --- B. Heuristic based on material name ---
            else
            {
                // Special handling for invisible / non‑physical walls
                if (strstr(name, "fake") || strstr(name, "invisible") || strstr(name, "setka"))
                    finalID = static_cast<int>(Presence::MaterialType::Air);
                else if (strstr(name, "wood") || strstr(name, "plank"))
                    finalID = static_cast<int>(Presence::MaterialType::Wood);
                else if (strstr(name, "metal") || strstr(name, "pipe") || strstr(name, "door"))
                    finalID = static_cast<int>(Presence::MaterialType::Metal);
                else if (strstr(name, "glass") || strstr(name, "window"))
                    finalID = static_cast<int>(Presence::MaterialType::Glass);
                else if (strstr(name, "grass") || strstr(name, "earth") || strstr(name, "cloth"))
                    finalID = static_cast<int>(Presence::MaterialType::Soft);
                else if (strstr(name, "asphalt") || strstr(name, "concrete"))
                    finalID = static_cast<int>(Presence::MaterialType::Stone);
            }
            m_MaterialCache[i] = finalID;
        }

        if (pConfig) xr_delete(pConfig);
        m_bCacheBuilt = true;
        Msg("[Presence Audio] Cache built: %d materials (%d custom).",
            m_MaterialCache.size(), customCount);
    }

    // =============================================================================================
    // Ray tracing (IGeometryProvider interface)
    // =============================================================================================

    /**
     * @brief Traces a single acoustic ray against the static level geometry.
     *
     * This function is called from the Presence SDK's background worker thread.
     * It is safe to read static geometry while a level is loaded, but dynamic
     * objects are intentionally ignored for reverb calculation.
     *
     * @param start   Ray origin (world coordinates).
     * @param dir     Normalised ray direction.
     * @param maxDist Maximum trace distance.
     * @return RayHit containing hit status, distance, surface normal and material ID.
     */
    virtual Presence::RayHit CastRay(const Presence::float3& start,
        const Presence::float3& dir,
        float maxDist) override
    {
        Presence::RayHit result;
        result.isHit = false;
        result.distance = maxDist;
        result.materialID = 0;

        // Guard against a level that has been unloaded.
        if (!g_pGameLevel) return result;

        // Convert Presence vectors to X‑Ray format.
        Fvector xStart, xDir;
        xStart.set(start.x, start.y, start.z);
        xDir.set(dir.x, dir.y, dir.z);

        // Re‑normalise to avoid NaN propagation.
        float mag = xDir.magnitude();
        if (mag < EPS_S) return result;
        xDir.div(mag);

        // Offset the ray slightly to avoid self‑intersection.
        const float K_BIAS = 0.05f;
        xStart.mad(xDir, K_BIAS);
        float traceDist = maxDist - K_BIAS;
        if (traceDist <= EPS_S) return result;

        collide::rq_result rq;

        // Trace against static geometry only (rqtStatic) for speed and stability.
        // Dynamic objects (NPCs, props) are not considered for reverb.
        BOOL hit = g_pGameLevel->ObjectSpace.RayPick(
            xStart, xDir, traceDist, collide::rqtStatic, rq, NULL);

        if (hit)
        {
            result.isHit = true;
            result.distance = rq.range + K_BIAS;

            // Retrieve the triangle and its vertices to compute the surface normal.
            CDB::TRI* tri = g_pGameLevel->ObjectSpace.GetStaticTris() + rq.element;
            Fvector* verts = g_pGameLevel->ObjectSpace.GetStaticVerts();

            Fvector xNorm;
            xNorm.mknormal(verts[tri->verts[0]], verts[tri->verts[1]], verts[tri->verts[2]]);
            result.normal = Presence::float3(xNorm.x, xNorm.y, xNorm.z);

            // Look up the material ID from the cache.
            u16 mtl_idx = static_cast<u16>(tri->material);
            if (mtl_idx < m_MaterialCache.size())
                result.materialID = m_MaterialCache[mtl_idx];
            else
                result.materialID = static_cast<int>(Presence::MaterialType::Stone); // safe fallback
        }

        return result;
    }
};
