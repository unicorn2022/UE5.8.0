// Copyright Epic Games, Inc. All Rights Reserved.

#include <bodyshapeeditor/BodyShapeEditorTarget.h>
#include <bodyshapeeditor/BodyShapeEditor.h>  // for ViewportConstraints2D + Constraint2D

#include <carbon/common/Log.h>
#include <nls/geometry/GeometryHelpers.h>     // for geoutils::CalculateMaskBasedOnMeshTopology
#include <nls/geometry/Mesh.h>
#include <nrr/landmarks/LandmarkConstraints2D.h>

#include <array>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

namespace
{
// Wrap geoutils::CalculateMaskBasedOnMeshTopology with the normals-prep that
// it requires. Same helper Actor Creation API uses (see
// MetaHumanCreatorBodyAPI.cpp ~line 170) — flags border verts, zero-normal
// verts, AND triangles with extreme edge-ratio. We compute normals on a
// non-const copy so the public Mesh contract (immutable shared_ptr<const>)
// stays intact.
Eigen::VectorXf ComputeNonDegenerateTargetMask(const Mesh<float>& mesh)
{
    if (mesh.NumVertices() == 0)
        return Eigen::VectorXf();

    Mesh<float> withNormals = mesh;
    if (withNormals.VertexNormals().cols() != withNormals.NumVertices())
        withNormals.CalculateVertexNormals();

    bool invalid = false;
    Eigen::VectorXf mask = geoutils::CalculateMaskBasedOnMeshTopology<float>(withNormals, invalid);
    if (invalid)
        LOG_WARNING("BodyShapeEditorTarget: target mesh topology is completely degenerate (mask all-zero)");
    else
        LOG_INFO("BodyShapeEditorTarget: non-degenerate mask — {} verts masked / {}",
                 (int)(mask.array() == 0.0f).count(), (int)mask.size());
    return mask;
}
} // namespace

struct BodyShapeEditorTarget::Impl
{
    static constexpr size_t kNumSlots = 3;
    std::array<std::shared_ptr<const Mesh<float>>, kNumSlots> meshes{};
    // Per-slot non-degenerate target mask, computed lazily on SetMesh and
    // refreshed when the mesh changes. Empty when no mesh has been set for
    // the slot. Re-uses geoutils::CalculateMaskBasedOnMeshTopology — same
    // mask Actor Creation API uses for its solve targets.
    std::array<Eigen::VectorXf, kNumSlots> nonDegenMasks{};
    // -1 = no slot set. Otherwise, slot index of the first SetMesh call
    // (used as the primary-mesh fallback).
    int primarySlot = -1;

    std::vector<std::pair<int, Eigen::Vector3f>> keypoints;
    std::vector<std::pair<int, Eigen::Vector3f>> joints;
    std::vector<ViewportConstraints2D> viewports;
    std::shared_ptr<LandmarkConstraints2D<float>> landmarks2D;
    std::vector<float> jointRotations;
};

BodyShapeEditorTarget::BodyShapeEditorTarget() : m(std::make_unique<Impl>()) {}
BodyShapeEditorTarget::~BodyShapeEditorTarget() = default;

BodyShapeEditorTarget::BodyShapeEditorTarget(const BodyShapeEditorTarget& other)
    // Moved-from objects have a null `m`; dereferencing unconditionally crashes.
    // Copy into a fresh empty Impl in that case so the resulting target is usable.
    : m(other.m ? std::make_unique<Impl>(*other.m) : std::make_unique<Impl>()) {}

BodyShapeEditorTarget& BodyShapeEditorTarget::operator=(const BodyShapeEditorTarget& other)
{
    if (this != &other)
        m = other.m ? std::make_unique<Impl>(*other.m) : std::make_unique<Impl>();
    return *this;
}

BodyShapeEditorTarget::BodyShapeEditorTarget(BodyShapeEditorTarget&&) noexcept = default;
BodyShapeEditorTarget& BodyShapeEditorTarget::operator=(BodyShapeEditorTarget&&) noexcept = default;

BodyShapeEditorTarget& BodyShapeEditorTarget::SetMesh(MeshSlot slot, std::shared_ptr<const Mesh<float>> mesh)
{
    const auto idx = static_cast<size_t>(slot);
    if (idx >= Impl::kNumSlots) return *this;
    if (!m->meshes[idx] && mesh && m->primarySlot < 0)
        m->primarySlot = static_cast<int>(idx);
    m->meshes[idx] = std::move(mesh);
    // Recompute the non-degenerate mask for this slot's new mesh; clear it
    // if the mesh is null or empty.
    if (m->meshes[idx] && m->meshes[idx]->NumVertices() > 0)
        m->nonDegenMasks[idx] = ComputeNonDegenerateTargetMask(*m->meshes[idx]);
    else
        m->nonDegenMasks[idx] = Eigen::VectorXf();
    // Recompute primary if we just cleared the primary slot.
    if (!m->meshes[idx] && m->primarySlot == static_cast<int>(idx))
    {
        m->primarySlot = -1;
        for (size_t i = 0; i < Impl::kNumSlots; ++i)
            if (m->meshes[i]) { m->primarySlot = static_cast<int>(i); break; }
    }
    return *this;
}

BodyShapeEditorTarget& BodyShapeEditorTarget::AddKeypoint(int vertex, Eigen::Vector3f pos)
{
    m->keypoints.emplace_back(vertex, std::move(pos));
    return *this;
}

BodyShapeEditorTarget& BodyShapeEditorTarget::AddJointCorrespondence(int vertex, Eigen::Vector3f pos)
{
    m->joints.emplace_back(vertex, std::move(pos));
    return *this;
}

BodyShapeEditorTarget& BodyShapeEditorTarget::SetLandmarks2D(std::shared_ptr<LandmarkConstraints2D<float>> lm)
{
    m->landmarks2D = std::move(lm);
    return *this;
}

BodyShapeEditorTarget& BodyShapeEditorTarget::AddViewportConstraints(Eigen::Matrix4f mvp, std::vector<Constraint2D> c)
{
    m->viewports.emplace_back(std::move(mvp), std::move(c));
    return *this;
}

BodyShapeEditorTarget& BodyShapeEditorTarget::AddViewportConstraints(std::vector<ViewportConstraints2D> list)
{
    for (auto& vc : list) m->viewports.push_back(std::move(vc));
    return *this;
}

BodyShapeEditorTarget& BodyShapeEditorTarget::SetJointRotations(av::ConstArrayView<float> jointRotations)
{
    m->jointRotations.assign(jointRotations.begin(), jointRotations.end());
    return *this;
}

std::shared_ptr<const Mesh<float>> BodyShapeEditorTarget::MeshFor(MeshSlot slot) const
{
    const auto idx = static_cast<size_t>(slot);
    if (idx < Impl::kNumSlots && m->meshes[idx]) return m->meshes[idx];
    return PrimaryMesh();
}

std::shared_ptr<const Mesh<float>> BodyShapeEditorTarget::PrimaryMesh() const
{
    if (m->primarySlot < 0) return nullptr;
    return m->meshes[static_cast<size_t>(m->primarySlot)];
}

bool BodyShapeEditorTarget::HasMesh(MeshSlot slot) const
{
    const auto idx = static_cast<size_t>(slot);
    return idx < Impl::kNumSlots && m->meshes[idx] != nullptr;
}

bool BodyShapeEditorTarget::HasAnyMesh() const { return m->primarySlot >= 0; }

const std::vector<std::pair<int, Eigen::Vector3f>>& BodyShapeEditorTarget::Keypoints() const { return m->keypoints; }
const std::vector<std::pair<int, Eigen::Vector3f>>& BodyShapeEditorTarget::Joints() const { return m->joints; }
const std::vector<ViewportConstraints2D>& BodyShapeEditorTarget::Viewports() const { return m->viewports; }
std::shared_ptr<LandmarkConstraints2D<float>> BodyShapeEditorTarget::Landmarks2D() const { return m->landmarks2D; }
av::ConstArrayView<float> BodyShapeEditorTarget::JointRotations() const { return {m->jointRotations.data(), m->jointRotations.size()}; }

const Eigen::VectorXf& BodyShapeEditorTarget::NonDegenerateMaskFor(MeshSlot slot) const
{
    static const Eigen::VectorXf kEmpty;
    const auto idx = static_cast<size_t>(slot);
    if (idx >= Impl::kNumSlots) return kEmpty;
    return m->nonDegenMasks[idx];
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
