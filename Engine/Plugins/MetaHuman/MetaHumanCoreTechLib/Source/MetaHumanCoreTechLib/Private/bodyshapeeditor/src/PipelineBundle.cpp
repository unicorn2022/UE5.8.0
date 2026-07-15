// Copyright Epic Games, Inc. All Rights Reserved.

#include <bodyshapeeditor/PipelineBundle.h>
#include <bodyshapeeditor/BodyShapeEditor.h>

#include <carbon/common/Log.h>
#include <carbon/io/JsonIO.h>
#include <carbon/io/Utils.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

namespace {

constexpr uint32_t kMaskMagic   = 0x31424C4D;  // 'MLB1' little-endian
constexpr uint32_t kMaskVersion = 1;

std::string MaskSidecarPath(const std::string& jsonPath)
{
    return std::filesystem::path(jsonPath).replace_extension(".masks.bin").string();
}

bool WriteMaskBinary(const std::string& path, const std::map<std::string, VertexWeights<float>>& masks)
{
    FILE* f = nullptr;
#ifdef _MSC_VER
    fopen_s(&f, path.c_str(), "wb");
#else
    f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) { LOG_WARNING("PipelineBundle:WriteMaskBinary: cannot open {}", path); return false; }

    // Every fwrite is checked — a short write mid-stream leaves a truncated sidecar,
    // which combined with masksStorage="binary" in the JSON makes the bundle unloadable.
    // Bail immediately and let the caller clear the "binary" marker.
    auto writeChecked = [&](const void* src, size_t elemSize, size_t n) -> bool {
        return std::fwrite(src, elemSize, n, f) == n;
    };
    bool ok = true;
    const uint32_t magic   = kMaskMagic;
    const uint32_t version = kMaskVersion;
    const uint32_t n       = static_cast<uint32_t>(masks.size());
    ok = ok && writeChecked(&magic,   sizeof(uint32_t), 1);
    ok = ok && writeChecked(&version, sizeof(uint32_t), 1);
    ok = ok && writeChecked(&n,       sizeof(uint32_t), 1);
    for (const auto& [name, w] : masks)
    {
        if (!ok) break;
        const uint32_t nameLen = static_cast<uint32_t>(name.size());
        ok = ok && writeChecked(&nameLen, sizeof(uint32_t), 1);
        if (ok && nameLen) ok = writeChecked(name.data(), 1, nameLen);
        const uint32_t count = static_cast<uint32_t>(w.NumVertices());
        ok = ok && writeChecked(&count, sizeof(uint32_t), 1);
        if (ok && count) ok = writeChecked(w.Weights().data(), sizeof(float), count);
    }
    std::fclose(f);
    if (!ok)
    {
        LOG_ERROR("PipelineBundle:WriteMaskBinary: short write on {} — removing truncated sidecar", path);
        std::error_code ec;
        std::filesystem::remove(path, ec);  // don't leave a half-written file on disk
        return false;
    }
    return true;
}

bool ReadMaskBinary(const std::string& path, std::map<std::string, VertexWeights<float>>& out)
{
    // Sanity caps — a corrupt or malicious sidecar could present absurd sizes that
    // cause OOM allocations. These bounds are generous for any real usage:
    // a rig has O(50k) verts, a mask name is short, and a bundle rarely holds >500 masks.
    constexpr uint32_t kMaxMasks    = 10000;
    constexpr uint32_t kMaxNameLen  = 1024;
    constexpr uint32_t kMaxVerts    = 10 * 1000 * 1000;  // 10M floats = 40MB per mask

    FILE* f = nullptr;
#ifdef _MSC_VER
    fopen_s(&f, path.c_str(), "rb");
#else
    f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;
    uint32_t magic = 0, version = 0, n = 0;
    if (std::fread(&magic,   sizeof(uint32_t), 1, f) != 1 ||
        std::fread(&version, sizeof(uint32_t), 1, f) != 1 ||
        std::fread(&n,       sizeof(uint32_t), 1, f) != 1 ||
        magic != kMaskMagic)
    {
        std::fclose(f);
        LOG_WARNING("PipelineBundle:ReadMaskBinary: bad header in {}", path);
        return false;
    }
    if (n > kMaxMasks)
    {
        std::fclose(f);
        LOG_WARNING("PipelineBundle:ReadMaskBinary: mask count {} exceeds cap {} in {}",
                    (int)n, (int)kMaxMasks, path);
        return false;
    }
    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t nameLen = 0;
        if (std::fread(&nameLen, sizeof(uint32_t), 1, f) != 1) { std::fclose(f); return false; }
        if (nameLen > kMaxNameLen)
        {
            std::fclose(f);
            LOG_WARNING("PipelineBundle:ReadMaskBinary: mask name length {} exceeds cap {} in {}",
                        (int)nameLen, (int)kMaxNameLen, path);
            return false;
        }
        std::string name(nameLen, '\0');
        if (nameLen && std::fread(name.data(), 1, nameLen, f) != nameLen) { std::fclose(f); return false; }
        uint32_t count = 0;
        if (std::fread(&count, sizeof(uint32_t), 1, f) != 1) { std::fclose(f); return false; }
        if (count > kMaxVerts)
        {
            std::fclose(f);
            LOG_WARNING("PipelineBundle:ReadMaskBinary: mask '{}' vert count {} exceeds cap {} in {}",
                        name, (int)count, (int)kMaxVerts, path);
            return false;
        }
        Eigen::VectorXf weights(count);
        if (count && std::fread(weights.data(), sizeof(float), count, f) != count) { std::fclose(f); return false; }
        out[name] = VertexWeights<float>(weights);
    }
    std::fclose(f);
    LOG_INFO("PipelineBundle:ReadMaskBinary: read {} masks from {} (version {})", (int)n, path, (int)version);
    return true;
}

// Pad smaller-than-target masks (e.g. head-only weights) up to the canonical
// body vertex count so Combine at solve time can union masks that originate
// from different meshes without hitting Eigen size-mismatch errors.
VertexWeights<float> PadToFull(VertexWeights<float> w, int fullN)
{
    if (fullN <= 0 || w.NumVertices() >= fullN) return w;
    Eigen::VectorXf padded = Eigen::VectorXf::Zero(fullN);
    padded.head(w.NumVertices()) = w.Weights();
    return VertexWeights<float>(padded);
}

int DeriveVertexCount(const BodyShapeEditor* bse)
{
    if (!bse) return 0;
    const auto partNames = bse->GetPartWeightNames();
    int maxN = 0;
    for (const auto& n : partNames)
        if (const auto* pw = bse->GetPartWeight(n))
            maxN = std::max(maxN, (int)pw->Weights().size());
    return maxN;
}

}  // namespace

bool SavePipelineBundle(const std::string& jsonPath, const PipelineBundle& bundle)
{
    if (jsonPath.empty()) return false;
    // error_code overload — the throwing overload would abort the process on e.g. a
    // read-only parent dir; a warning is the sane behaviour for a preset save.
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(jsonPath).parent_path(), ec);
        if (ec)
            LOG_WARNING("PipelineBundle:Save: create_directories failed for '{}': {}",
                        jsonPath, ec.message());
    }

    JsonElement root(JsonElement::JsonType::Object);

    // pipelines (new format: {active, pipelines: {presets via StepToJson}})
    JsonElement pipelinesSection(JsonElement::JsonType::Object);
    pipelinesSection.Insert("active", JsonElement(bundle.activePipeline));
    pipelinesSection.Insert("pipelines", PipelinePresetsToJson(bundle.pipelines));
    root.Insert("pipelines", std::move(pipelinesSection));

    // controlGroups
    if (!bundle.controlGroups.empty())
        root.Insert("controlGroups", ControlGroupsToJson(bundle.controlGroups));

    // maskGroups (tiny metadata; actual weights go to sidecar)
    if (!bundle.maskGroups.empty())
    {
        JsonElement mg(JsonElement::JsonType::Object);
        for (const auto& [name, entries] : bundle.maskGroups)
        {
            JsonElement arr(JsonElement::JsonType::Array);
            for (const auto& e : entries) arr.Append(JsonElement(e));
            mg.Insert(name, std::move(arr));
        }
        root.Insert("maskGroups", std::move(mg));
    }

    // masks: binary sidecar by default. Only stamp masksStorage="binary" in the JSON
    // if the sidecar actually wrote successfully — otherwise the JSON would point at
    // a missing/truncated file and LoadPipelineBundle would silently return 0 masks.
    if (!bundle.masks.empty())
    {
        if (WriteMaskBinary(MaskSidecarPath(jsonPath), bundle.masks))
            root.Insert("masksStorage", JsonElement(std::string("binary")));
        else
            LOG_ERROR("PipelineBundle:Save: mask sidecar write failed — JSON will not reference it");
    }

    WriteFile(jsonPath, WriteJson(root, 1));
    return true;
}

bool LoadPipelineBundle(const std::string& jsonPath, BodyShapeEditor* bse, PipelineBundle& bundle)
{
    bundle = PipelineBundle{};
    if (jsonPath.empty() || !std::filesystem::exists(jsonPath)) return false;

    const JsonElement root = ReadJson(ReadFile(jsonPath));
    if (!root.IsObject()) return false;

    // pipelines — accept both the titan_apps wrapper ({active, pipelines}) and a
    // flat shape where `pipelines` is directly the preset map.
    if (root.Contains("pipelines"))
    {
        const auto& p = root["pipelines"];
        if (p.IsObject() && p.Contains("pipelines"))
        {
            PipelinePresetsFromJson(bundle.pipelines, p["pipelines"]);
            if (p.Contains("active") && p["active"].IsString())
                bundle.activePipeline = p["active"].String();
        }
        else if (p.IsObject())
        {
            PipelinePresetsFromJson(bundle.pipelines, p);
        }
    }
    else if (root.Contains("stepPipelines"))
    {
        // Legacy scan-fitting migration path. Caller that needs the full legacy
        // migrator (config preset table, mask name list, etc.) should keep using
        // MigrateStepPipelinesJson; here we just pull the bare steps so the
        // bundle is usable.
        LOG_INFO("PipelineBundle:Load: encountered 'stepPipelines' (legacy) — skipping; use MigrateStepPipelinesJson if you need that format");
    }

    // controlGroups
    if (root.Contains("controlGroups") && root["controlGroups"].IsObject())
        ControlGroupsFromJson(bundle.controlGroups, root["controlGroups"]);

    // maskGroups (metadata)
    if (root.Contains("maskGroups") && root["maskGroups"].IsObject())
    {
        for (const auto& [name, arr] : root["maskGroups"].Map())
        {
            if (!arr.IsArray()) continue;
            std::vector<std::string> entries;
            for (const auto& e : arr.Array())
                if (e.IsString()) entries.push_back(e.String());
            bundle.maskGroups[name] = std::move(entries);
        }
    }

    // masks: prefer binary sidecar; fall back to JSON-embedded (legacy) if present.
    bool gotMasks = false;
    const std::string binPath = MaskSidecarPath(jsonPath);
    if (std::filesystem::exists(binPath))
        gotMasks = ReadMaskBinary(binPath, bundle.masks);

    if (!gotMasks)
    {
        const JsonElement* maskJson = nullptr;
        if (root.Contains("masks") && root["masks"].IsObject())           maskJson = &root["masks"];
        else if (root.Contains("externalMasks") && root["externalMasks"].IsObject())
                                                                          maskJson = &root["externalMasks"];
        if (maskJson)
        {
            const int numVerts = DeriveVertexCount(bse);
            for (const auto& [name, _] : maskJson->Map())
            {
                VertexWeights<float> vw;
                vw.Load(*maskJson, name, numVerts);
                bundle.masks[name] = std::move(vw);
            }
        }
    }

    // Ingest BSE part weights — mirrors MaskLibrary::AttachBSE on the app side
    // so a step's maskNames can reference part-weight names (e.g. "body",
    // "head") interchangeably with user-authored masks.
    if (bse)
    {
        const int fullN = DeriveVertexCount(bse);
        for (auto& [_, w] : bundle.masks)
            if ((int)w.NumVertices() < fullN)
                w = PadToFull(std::move(w), fullN);
        for (const auto& n : bse->GetPartWeightNames())
        {
            if (bundle.masks.count(n)) continue;  // user-authored mask wins
            if (const auto* pw = bse->GetPartWeight(n))
                bundle.masks[n] = PadToFull(*pw, fullN);
        }
    }

    LOG_INFO("PipelineBundle:Load: {} pipelines, {} controlGroups, {} masks, active='{}'",
             (int)bundle.pipelines.size(), (int)bundle.controlGroups.size(),
             (int)bundle.masks.size(), bundle.activePipeline);
    return true;
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
