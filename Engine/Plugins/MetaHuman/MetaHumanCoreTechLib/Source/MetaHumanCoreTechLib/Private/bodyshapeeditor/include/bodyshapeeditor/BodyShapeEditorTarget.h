// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <carbon/common/Defs.h>
#include <Eigen/Core>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include <arrayview/ArrayView.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

template <class T> class Mesh;
template <class T> class LandmarkConstraints2D;

struct Constraint2D;

// Declared in BodyShapeEditor.h — redeclare the alias here so this header is standalone.
using ViewportConstraints2D = std::pair<Eigen::Matrix4f, std::vector<Constraint2D>>;

// Domain-owned fit target for all BSE solve entry points. Replaces the
// legacy POD `SolveTarget`. Holds up to three mesh slots (Head/Body/Combined)
// plus sparse keypoint/joint correspondences, 2D viewport constraints, and
// face landmark constraints. Accessors only — fields are pimpl'd.
class BodyShapeEditorTarget
{
public:
    enum class MeshSlot : uint8_t { Head = 0, Body = 1, Combined = 2 };

    BodyShapeEditorTarget();
    ~BodyShapeEditorTarget();
    BodyShapeEditorTarget(const BodyShapeEditorTarget&);
    BodyShapeEditorTarget& operator=(const BodyShapeEditorTarget&);
    BodyShapeEditorTarget(BodyShapeEditorTarget&&) noexcept;
    BodyShapeEditorTarget& operator=(BodyShapeEditorTarget&&) noexcept;

    BodyShapeEditorTarget& SetMesh(MeshSlot slot, std::shared_ptr<const Mesh<float>> mesh);
    BodyShapeEditorTarget& AddKeypoint(int vertex, Eigen::Vector3f pos);
    BodyShapeEditorTarget& AddJointCorrespondence(int vertex, Eigen::Vector3f pos);
    BodyShapeEditorTarget& SetLandmarks2D(std::shared_ptr<LandmarkConstraints2D<float>> lm);
    BodyShapeEditorTarget& AddViewportConstraints(Eigen::Matrix4f mvp, std::vector<Constraint2D> c);
    BodyShapeEditorTarget& AddViewportConstraints(std::vector<ViewportConstraints2D> list);
    BodyShapeEditorTarget& SetJointRotations(av::ConstArrayView<float> jointRotations);

    // Returns the mesh set on `slot` if present, otherwise the first-set slot's mesh
    // (the "primary" mesh). Returns nullptr if no slot is set.
    std::shared_ptr<const Mesh<float>> MeshFor(MeshSlot slot) const;
    // Shortcut: first-set slot's mesh, or nullptr if no slot set.
    std::shared_ptr<const Mesh<float>> PrimaryMesh() const;
    bool HasMesh(MeshSlot slot) const;
    bool HasAnyMesh() const;

    const std::vector<std::pair<int, Eigen::Vector3f>>& Keypoints() const;
    const std::vector<std::pair<int, Eigen::Vector3f>>& Joints() const;
    av::ConstArrayView<float> JointRotations() const;
    const std::vector<ViewportConstraints2D>& Viewports() const;
    std::shared_ptr<LandmarkConstraints2D<float>> Landmarks2D() const;

    // Per-vertex weight mask (0 / 1) flagging non-degenerate target vertices.
    // Computed and cached automatically when SetMesh is called: a vertex's
    // mask is 0 iff every incident triangle has area < minArea (default 1e-8
    // in mesh units), 1 otherwise. Solvers can feed this straight into
    // ICPConstraints::SetTargetWeights or LandmarkConstraints to skip pinch
    // points / collapsed verts that survived a noisy reconstruction.
    // Returns an empty vector when the slot has no mesh.
    const Eigen::VectorXf& NonDegenerateMaskFor(MeshSlot slot) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
