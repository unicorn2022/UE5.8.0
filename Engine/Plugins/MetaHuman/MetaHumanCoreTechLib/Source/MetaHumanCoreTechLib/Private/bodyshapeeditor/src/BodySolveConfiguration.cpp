// Copyright Epic Games, Inc. All Rights Reserved.

#include <bodyshapeeditor/BodySolveConfiguration.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

BodySolveConfiguration::BodySolveConfiguration()
    : body(CreateBodySolveConfig())
    , face(CreateFaceSolveConfig())
    , refinement(CreateRefinementConfig())
{}

BodySolveConfiguration::BodySolveConfiguration(Configuration b, Configuration f, Configuration r)
    : body(std::move(b))
    , face(std::move(f))
    , refinement(std::move(r))
{}

Configuration CreateBodySolveConfig()
{
    // Defaults match the BODY step of the BSFT "combined" pipeline (the one
    // every preset variant — body_only / head_only / head_body / combined —
    // uses for the primary body solve). Keeps "new step" parity with the
    // baked combined preset so adding a fresh BodySolve step lands at known-
    // good values out of the box.
    return Configuration("BodySolve", {
        {"iterations",       ConfigurationParameter(13, 1, 100)},
        // ICP weight schedule
        {"icp",              ConfigurationParameter(70.0f, 0.0f, 100.0f)},
        {"icpEnd",           ConfigurationParameter(70.0f, 0.0f, 100.0f)},
        {"icpCurve",         ConfigurationParameter(0, 0, 3)},
        // ICP tolerance schedule (combined uses 50, so range bumped to 50).
        {"icpTol",           ConfigurationParameter(50.0f, 0.0f, 50.0f)},
        {"icpTolEnd",        ConfigurationParameter(50.0f, 0.0f, 50.0f)},
        {"icpTolCurve",      ConfigurationParameter(0, 0, 3)},
        // Normal compatibility threshold schedule
        {"normalCompat",      ConfigurationParameter(0.8f, 0.0f, 1.0f)},
        {"normalCompatEnd",   ConfigurationParameter(0.8f, 0.0f, 1.0f)},
        {"normalCompatCurve", ConfigurationParameter(0, 0, 3)},
        // Keypoint weight schedule
        {"keypoint",         ConfigurationParameter(1.0f, 0.0f, 200.0f)},
        {"keypointEnd",      ConfigurationParameter(1.0f, 0.0f, 200.0f)},
        {"keypointCurve",    ConfigurationParameter(0, 0, 3)},
        // Pose-driver chain consistency: penalises adjacent drivers in a
        // joint chain (e.g. neck_01 → neck_02 → head) rotating in opposite
        // directions on the same axis. Cost = Σ ||c_i − c_{i+1}||² per axis.
        // 0 = off; small (0.1–1.0) is enough since each pair contributes its
        // squared difference.
        {"poseChain",        ConfigurationParameter(2.0f, 0.0f, 100.0f)},
        // Landmark2D weight schedule
        {"landmark2D",       ConfigurationParameter(0.557f, 0.0f, 1.0f)},
        {"landmark2DEnd",    ConfigurationParameter(0.0f, 0.0f, 1.0f)},
        {"landmark2DCurve",  ConfigurationParameter(1, 0, 3)},
        // Joint2D weight schedule
        {"joint2D",          ConfigurationParameter(0.0f, 0.0f, 1.0f)},
        {"joint2DEnd",       ConfigurationParameter(0.0f, 0.0f, 1.0f)},
        {"joint2DCurve",     ConfigurationParameter(0, 0, 3)},
        // Regularization weight schedules
        {"regGlobal",        ConfigurationParameter(0.682f, 0.0f, 10.0f)},
        {"regGlobalEnd",     ConfigurationParameter(0.682f, 0.0f, 10.0f)},
        {"regGlobalCurve",   ConfigurationParameter(0, 0, 3)},
        {"regLocal",         ConfigurationParameter(1.0f, 0.0f, 10.0f)},
        {"regLocalEnd",      ConfigurationParameter(1.0f, 0.0f, 10.0f)},
        {"regLocalCurve",    ConfigurationParameter(0, 0, 3)},
        {"regProportions",   ConfigurationParameter(1.0f, 0.0f, 10.0f)},
        {"regProportionsEnd",ConfigurationParameter(1.0f, 0.0f, 10.0f)},
        {"regProportionsCurve", ConfigurationParameter(0, 0, 3)},
        {"regPose",          ConfigurationParameter(2.0f, 0.0f, 10.0f)},
        {"regPoseEnd",       ConfigurationParameter(0.5f, 0.0f, 10.0f)},
        {"regPoseCurve",     ConfigurationParameter(1, 0, 3)},
        {"curveResampling",  ConfigurationParameter(5, 1, 10)},
        {"symmetry",         ConfigurationParameter(false)},
        {"logCostBreakdown", ConfigurationParameter(false)},
    });
}

Configuration CreateFaceSolveConfig()
{
    // Defaults match the Face step of the BSFT "combined" pipeline.
    return Configuration("FaceSolve", {
        {"iterations",       ConfigurationParameter(7, 1, 20)},
        // ICP weight schedule
        {"icp",              ConfigurationParameter(30.0f, 0.0f, 100.0f)},
        {"icpEnd",           ConfigurationParameter(30.0f, 0.0f, 100.0f)},
        {"icpCurve",         ConfigurationParameter(0, 0, 3)},
        // ICP tolerance schedule
        {"icpTol",           ConfigurationParameter(30.0f, 0.0f, 50.0f)},
        {"icpTolEnd",        ConfigurationParameter(30.0f, 0.0f, 50.0f)},
        {"icpTolCurve",      ConfigurationParameter(0, 0, 3)},
        // Normal compatibility threshold schedule
        {"normalCompat",      ConfigurationParameter(0.8f, 0.0f, 1.0f)},
        {"normalCompatEnd",   ConfigurationParameter(0.8f, 0.0f, 1.0f)},
        {"normalCompatCurve", ConfigurationParameter(0, 0, 3)},
        // Keypoint weight schedule
        {"keypoint",         ConfigurationParameter(10.0f, 0.0f, 200.0f)},
        {"keypointEnd",      ConfigurationParameter(10.0f, 0.0f, 200.0f)},
        {"keypointCurve",    ConfigurationParameter(0, 0, 3)},
        // Landmark2D weight schedule
        {"landmark2D",       ConfigurationParameter(0.2f, 0.0f, 1.0f)},
        {"landmark2DEnd",    ConfigurationParameter(0.2f, 0.0f, 1.0f)},
        {"landmark2DCurve",  ConfigurationParameter(0, 0, 3)},
        // Regularization
        {"modelRegularization",    ConfigurationParameter(10.0f, 0.001f, 1000.0f)},
        {"modelRegularizationEnd", ConfigurationParameter(10.0f, 0.001f, 1000.0f)},
        {"modelRegularizationCurve", ConfigurationParameter(0, 0, 3)},
        {"patchSmoothness",        ConfigurationParameter(1.0f, 0.001f, 10.0f)},
        {"patchSmoothnessEnd",     ConfigurationParameter(1.0f, 0.001f, 10.0f)},
        {"patchSmoothnessCurve",   ConfigurationParameter(0, 0, 3)},
        {"lmDamping",        ConfigurationParameter(0.01f, 0.001f, 10.0f)},
        {"curveResampling",  ConfigurationParameter(5, 1, 10)},
        {"logCostBreakdown", ConfigurationParameter(false)},
    });
}

Configuration CreateRefinementConfig()
{
    // Defaults match the Refine step of the BSFT "combined" pipeline.
    return Configuration("Refinement", {
        {"iterations",       ConfigurationParameter(5, 0, 20)},
        // Constraints
        {"vertexWeight",     ConfigurationParameter(0.5f, 0.0f, 2.0f)},
        {"keypoint",         ConfigurationParameter(0.0f, 0.0f, 200.0f)},
        {"landmark2D",       ConfigurationParameter(0.0f, 0.0f, 1.0f)},
        // Regularization
        {"laplacian",        ConfigurationParameter(0.8f, 0.0f, 5.0f)},
        {"bending",          ConfigurationParameter(0.0f, 0.0f, 2.0f)},
        {"strain",           ConfigurationParameter(0.135f, 0.0f, 2.0f)},
        {"vertexOffsetReg",  ConfigurationParameter(0.0f, 0.0f, 1.0f)},
        {"vertexReg",        ConfigurationParameter(3e-3f, 1e-6f, 1e-1f)},
        {"icpTol",           ConfigurationParameter(5.95f, 0.0f, 50.0f)},
    });
}

JsonElement ControlGroupsToJson(const std::map<std::string, std::vector<int>>& groups)
{
    JsonElement result(JsonElement::JsonType::Object);
    for (const auto& [name, indices] : groups)
    {
        JsonElement arr(JsonElement::JsonType::Array);
        for (int idx : indices) arr.Append(JsonElement(idx));
        result.Insert(name, std::move(arr));
    }
    return result;
}

bool ControlGroupsFromJson(std::map<std::string, std::vector<int>>& groups, const JsonElement& json)
{
    if (!json.IsObject()) return false;
    for (const auto& [name, arr] : json.Map())
    {
        if (!arr.IsArray()) continue;
        std::vector<int> indices;
        for (const auto& idx : arr.Array())
            indices.push_back(idx.Get<int>());
        groups[name] = std::move(indices);
    }
    return true;
}


JsonElement SolveConfigurationToJson(const BodySolveConfiguration& cfg)
{
    JsonElement j(JsonElement::JsonType::Object);
    j.Insert("body", cfg.body.ToJson());
    j.Insert("face", cfg.face.ToJson());
    j.Insert("refinement", cfg.refinement.ToJson());
    return j;
}

bool SolveConfigurationFromJson(BodySolveConfiguration& cfg, const JsonElement& json)
{
    if (!json.IsObject()) return false;
    std::vector<std::string> unused, unknown;
    if (json.Contains("body")) cfg.body.FromJson(json["body"], unused, unknown);
    if (json.Contains("face")) cfg.face.FromJson(json["face"], unused, unknown);
    if (json.Contains("refinement")) cfg.refinement.FromJson(json["refinement"], unused, unknown);
    return true;
}

namespace {

BodyShapeEditorTarget::MeshSlot SlotFromLegacyString(const std::string& s)
{
    if (s == "head" || s == "face") return BodyShapeEditorTarget::MeshSlot::Head;
    if (s == "body") return BodyShapeEditorTarget::MeshSlot::Body;
    return BodyShapeEditorTarget::MeshSlot::Combined;  // "" / "combined" / unknown
}

JsonElement StepToJson(const SolveStep& step)
{
    JsonElement s(JsonElement::JsonType::Object);
    s.Insert("name", JsonElement(step.name));
    s.Insert("enabled", JsonElement(step.enabled));
    s.Insert("kind", JsonElement((int)step.kind));
    if (!step.config.Parameters().empty())
        s.Insert("config", step.config.ToJson());
    s.Insert("targetSlot", JsonElement(static_cast<int>(step.targetSlot)));
    if (step.kind == StepKind::AdaptNeck)
    {
        s.Insert("seamLockSide",  JsonElement((int)step.seamLockSide));
        s.Insert("seamRings",     JsonElement(step.seamRings));
        s.Insert("seamIterations",JsonElement(step.seamIterations));
        s.Insert("seamLaplacian", JsonElement(step.seamLaplacian));
    }
    if (step.kind == StepKind::BodySolve && step.lockScale)
        s.Insert("lockScale", JsonElement(step.lockScale));
    if (!step.maskNames.empty())
    {
        JsonElement arr2(JsonElement::JsonType::Array);
        for (const auto& m : step.maskNames) arr2.Append(JsonElement(m));
        s.Insert("maskNames", std::move(arr2));
    }
    JsonElement cgroups(JsonElement::JsonType::Array);
    for (const auto& c : step.controlGroupNames) cgroups.Append(JsonElement(c));
    s.Insert("controlGroups", std::move(cgroups));
    if (!step.initialControls.empty())
    {
        JsonElement controls(JsonElement::JsonType::Object);
        for (const auto& [name, value] : step.initialControls)
            controls.Insert(name, JsonElement(static_cast<double>(value)));
        s.Insert("initialControls", std::move(controls));
    }
    return s;
}

void StepFromJson(SolveStep& step, const JsonElement& s, int version)
{
    if (s.Contains("name")) step.name = s["name"].String();
    if (s.Contains("enabled")) step.enabled = s["enabled"].Boolean();

    // Configuration is parsed in ExpandStepJson once the step kind is known —
    // we need the kind-specific default schema in place before FromJson can
    // merge saved parameter values (FromJson only writes keys that already
    // exist in the Configuration).

    // Align no longer carries region / lockScale fields (region auto-detected, R=I always).
    // Legacy `alignRegion` / `alignLockScale` keys are silently ignored.

    // AdaptNeck-specific fields.
    if (s.Contains("seamLockSide"))   step.seamLockSide = (SeamLockSide)s["seamLockSide"].Value<int>();
    if (s.Contains("seamRings"))      step.seamRings    = s["seamRings"].Value<int>();
    if (s.Contains("seamIterations")) step.seamIterations = s["seamIterations"].Value<int>();
    if (s.Contains("seamLaplacian"))  step.seamLaplacian = s["seamLaplacian"].Value<float>();

    // BodySolve-specific: pin the uniform scaleVar during this step's solve.
    if (s.Contains("lockScale")) step.lockScale = s["lockScale"].Boolean();

    // maskNames: v2 array; legacy single maskName string or "masks" array.
    if (s.Contains("maskNames") && s["maskNames"].IsArray())
    {
        for (const auto& m : s["maskNames"].Array()) step.maskNames.push_back(m.String());
    }
    else if (s.Contains("maskName") && !s["maskName"].String().empty())
    {
        step.maskNames.push_back(s["maskName"].String());
    }
    else if (s.Contains("masks") && s["masks"].IsArray())
    {
        for (const auto& m : s["masks"].Array()) step.maskNames.push_back(m.String());
    }

    // targetSlot: v2 int enum; legacy "targetMesh" string.
    if (s.Contains("targetSlot"))
        step.targetSlot = static_cast<BodyShapeEditorTarget::MeshSlot>(s["targetSlot"].Value<int>());
    else if (s.Contains("targetMesh") && s["targetMesh"].IsString())
        step.targetSlot = SlotFromLegacyString(s["targetMesh"].String());

    if (s.Contains("controlGroups") && s["controlGroups"].IsArray())
        for (const auto& c : s["controlGroups"].Array()) step.controlGroupNames.push_back(c.String());
    if (s.Contains("initialControls") && s["initialControls"].IsObject())
        for (const auto& [name, val] : s["initialControls"].Map())
            step.initialControls[name] = val.Get<float>();
}

//! Pick the right sub-config from a legacy-nested `{body,face,refinement}`
//! object based on StepKind. Populates `out` if the matching subtree exists.
static void PopulateLegacyConfig(Configuration& out, const JsonElement& cfg, StepKind kind)
{
    const char* key = (kind == StepKind::FaceSolve) ? "face"
                    : (kind == StepKind::Refine)   ? "refinement"
                                                   : "body";
    if (!cfg.Contains(key) || !cfg[key].IsObject()) return;
    std::vector<std::string> unused, unknown;
    out.FromJson(cfg[key], unused, unknown);
}

//! Build the default Configuration for a given step kind (or leave empty for
//! kinds that don't have one, e.g. Align / AdaptNeck).
static Configuration MakeConfigForKind(StepKind kind)
{
    switch (kind)
    {
        case StepKind::BodySolve: return CreateBodySolveConfig();
        case StepKind::FaceSolve: return CreateFaceSolveConfig();
        case StepKind::Refine:    return CreateRefinementConfig();
        default:                  return Configuration("Empty", {});
    }
}

//! Parse a JSON step entry into one or more SolveSteps. New (v3+) format has a
//! `kind` field and produces one step. Legacy formats had `run*` booleans that
//! could combine multiple operations — they expand into multiple kind-specific
//! steps in order: Align → BodySolve → FaceSolve → Refine.
std::vector<SolveStep> ExpandStepJson(const JsonElement& s, int version)
{
    std::vector<SolveStep> out;
    SolveStep base;
    StepFromJson(base, s, version);

    if (s.Contains("kind"))
    {
        base.kind = static_cast<StepKind>(s["kind"].Value<int>());
        // Install the kind-specific defaults so FromJson has a schema to
        // write into, then merge saved values (flat) or the legacy
        // {body,face,refinement} sub-object over the top.
        base.config = MakeConfigForKind(base.kind);
        if (s.Contains("config") && s["config"].IsObject())
        {
            const auto& cfg = s["config"];
            const bool isLegacyNested = cfg.Contains("body") || cfg.Contains("face") || cfg.Contains("refinement");
            if (isLegacyNested)
            {
                PopulateLegacyConfig(base.config, cfg, base.kind);
            }
            else
            {
                std::vector<std::string> unused, unknown;
                base.config.FromJson(cfg, unused, unknown);
            }
        }
        out.push_back(std::move(base));
        return out;
    }

    // Legacy path.
    const bool runAlign = s.Contains("runAlign") && s["runAlign"].Boolean();
    const bool runBody  = s.Contains("runBody")  && s["runBody"].Boolean();
    const bool runFace  = s.Contains("runFace")  && s["runFace"].Boolean();
    const bool runRefine= s.Contains("runRefine") && s["runRefine"].Boolean();
    const bool runScale = version < 2 && s.Contains("runScale") && s["runScale"].Boolean();
    const bool anyAlign = runAlign || runScale;

    auto pushKind = [&](StepKind k) {
        SolveStep st = base;
        st.kind = k;
        st.config = MakeConfigForKind(k);
        if (s.Contains("config") && s["config"].IsObject())
            PopulateLegacyConfig(st.config, s["config"], k);
        out.push_back(std::move(st));
    };
    if (anyAlign) pushKind(StepKind::Align);
    if (runBody)  pushKind(StepKind::BodySolve);
    if (runFace)  pushKind(StepKind::FaceSolve);
    if (runRefine) pushKind(StepKind::Refine);

    if (out.empty())
    {
        // Legacy default was runBody=true; honour that if nothing was set.
        base.kind = StepKind::BodySolve;
        base.config = MakeConfigForKind(StepKind::BodySolve);
        if (s.Contains("config") && s["config"].IsObject())
            PopulateLegacyConfig(base.config, s["config"], StepKind::BodySolve);
        out.push_back(std::move(base));
    }
    return out;
}

} // namespace

JsonElement PipelineToJson(const std::vector<SolveStep>& steps)
{
    // Bare array form (no version header) — callers that want versioning use
    // PipelinePresetsToJson which wraps this with "version":2.
    JsonElement arr(JsonElement::JsonType::Array);
    for (const auto& step : steps) arr.Append(StepToJson(step));
    return arr;
}

bool PipelineFromJson(std::vector<SolveStep>& steps, const JsonElement& json)
{
    // Accept either a raw array or a {version, steps} object.
    const JsonElement* stepsArr = nullptr;
    int version = 1;
    if (json.IsArray())
    {
        stepsArr = &json;
    }
    else if (json.IsObject())
    {
        if (json.Contains("version")) version = json["version"].Value<int>();
        if (json.Contains("steps") && json["steps"].IsArray()) stepsArr = &json["steps"];
    }
    if (!stepsArr) return false;
    steps.clear();
    for (const auto& s : stepsArr->Array())
        for (auto& st : ExpandStepJson(s, version))
            steps.push_back(std::move(st));
    return true;
}

JsonElement PipelinePresetsToJson(const std::map<std::string, std::vector<SolveStep>>& pipelines)
{
    JsonElement result(JsonElement::JsonType::Object);
    result.Insert("version", JsonElement(2));
    JsonElement items(JsonElement::JsonType::Object);
    for (const auto& [name, steps] : pipelines)
        items.Insert(name, PipelineToJson(steps));
    result.Insert("pipelines", std::move(items));
    return result;
}

bool PipelinePresetsFromJson(std::map<std::string, std::vector<SolveStep>>& pipelines, const JsonElement& json)
{
    if (!json.IsObject()) return false;
    int version = 1;
    const JsonElement* items = nullptr;
    if (json.Contains("version")) version = json["version"].Value<int>();
    if (json.Contains("pipelines") && json["pipelines"].IsObject())
        items = &json["pipelines"];
    else
        items = &json;  // legacy: object is directly {pipelineName: [...]}
    if (!items->IsObject()) return false;

    for (const auto& [name, stepsJson] : items->Map())
    {
        std::vector<SolveStep> steps;
        // Thread version through; legacy multi-bool steps expand into multiple kinds.
        if (stepsJson.IsArray())
        {
            steps.clear();
            for (const auto& s : stepsJson.Array())
                for (auto& st : ExpandStepJson(s, version))
                    steps.push_back(std::move(st));
            pipelines[name] = std::move(steps);
        }
        else if (PipelineFromJson(steps, stepsJson))
        {
            pipelines[name] = std::move(steps);
        }
    }
    return true;
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
