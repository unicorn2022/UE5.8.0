// Copyright Epic Games, Inc. All Rights Reserved.

#include <nrr/CompareDNAs.h>
#include <carbon/common/Log.h>
#include <nls/geometry/EulerAngles.h>
#include <cmath>


CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

// Geodesic angle (degrees) between two rotations given as the same XYZ Euler
// convention used by EulerXYZ() throughout the rig code — see
// nls/geometry/EulerAngles.h ("Maya XYZ order = Rz * Ry * Rx"). Inputs are in
// degrees (the DNA convention).
//
// Component-wise Euler diff is not safe near ry = ±90° gimbal lock: tiny FP
// perturbations on different platforms pick mathematically-equivalent but
// distinct (rx, rz) branches, producing a large apparent Euler diff for the
// same physical rotation. Comparing rotation matrices avoids that.
static float GeodesicAngleDegXYZ(float rx_a, float ry_a, float rz_a,
                                  float rx_b, float ry_b, float rz_b)
{
    constexpr float kDeg = 3.14159265358979323846f / 180.0f;
    const Eigen::Matrix3f A = EulerXYZ<float>(rx_a * kDeg, ry_a * kDeg, rz_a * kDeg);
    const Eigen::Matrix3f B = EulerXYZ<float>(rx_b * kDeg, ry_b * kDeg, rz_b * kDeg);
    // Stable form: ||A - B||_F = 2*sqrt(2)*sin(theta/2). asin doesn't lose
    // precision near 0, unlike acos((trace - 1)/2) which catastrophically
    // cancels for near-identical rotations.
    const float fro = (A - B).norm();
    float halfChord = fro * 0.35355339059327376f; // 1 / (2*sqrt(2))
    if (halfChord > 1.0f) halfChord = 1.0f;
    return 2.0f * std::asin(halfChord) / kDeg;
}

bool CompareDNAs(dna::BinaryStreamReader* goldDataReader, dna::BinaryStreamReader* outputReader, float positionTolerance, float angleTolerance, float weightTolerance, bool bCompareSkinningWeights)
{
    const uint16_t jointCount = outputReader->getJointCount();
    const uint16_t jointCount2 = goldDataReader->getJointCount();
    bool bResult = true;

    if (jointCount != jointCount2)
    {
        LOG_ERROR("Different number of joints for current DNA and gold data DNA: {} vs {}", jointCount, jointCount2);
        bResult = false;
    }
    else
    {
        for (uint16_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
        {
            const auto jointPosition = outputReader->getNeutralJointTranslation(jointIndex);
            const auto jointPositionGold = goldDataReader->getNeutralJointTranslation(jointIndex);
            auto diff = jointPosition - jointPositionGold;
            if (std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z) >= positionTolerance)
            {
                LOG_ERROR("Joint {} has different neutral translation values, current = {}, {}, {}, gold data = {}, {}, {}", jointIndex,
                    jointPosition.x, jointPosition.y, jointPosition.z, jointPositionGold.x, jointPositionGold.y, jointPositionGold.z);
                bResult = false;
            }

            const auto jointRotation = outputReader->getNeutralJointRotation(jointIndex);
            const auto jointRotationGold = goldDataReader->getNeutralJointRotation(jointIndex);

            const float geodesicDeg = GeodesicAngleDegXYZ(
                jointRotation.x, jointRotation.y, jointRotation.z,
                jointRotationGold.x, jointRotationGold.y, jointRotationGold.z);
            if (geodesicDeg >= angleTolerance)
            {
                LOG_ERROR("Joint {} has different neutral rotation values (geodesic {} deg), current = {}, {}, {}, gold data = {}, {}, {}",
                    jointIndex, geodesicDeg,
                    jointRotation.x, jointRotation.y, jointRotation.z, jointRotationGold.x, jointRotationGold.y, jointRotationGold.z);
                bResult = false;
            }
        }
    }


    const int meshCount = outputReader->getMeshCount();
    for (int meshIndex = 0; meshIndex < meshCount; meshIndex++)
    {
        const int vertexCount = outputReader->getVertexPositionCount(uint16_t(meshIndex));
        const int vertexNormalCount = outputReader->getVertexNormalCount(uint16_t(meshIndex));
        const int vertexCount2 = goldDataReader->getVertexPositionCount(uint16_t(meshIndex));
        const int vertexNormalCount2 = goldDataReader->getVertexNormalCount(uint16_t(meshIndex));
        if (!(vertexCount == vertexCount2 && vertexNormalCount == vertexNormalCount2 && vertexCount == vertexNormalCount))
        {
            LOG_ERROR("Mesh {} has different number of vertices or vertex normals than gold data: current = {}, {}, gold data = {}, {}", meshIndex, vertexCount, vertexNormalCount, vertexCount2, vertexNormalCount2);
            bResult = false;
        }
        else
        {    
            for (int dnaVertexIndex = 0; dnaVertexIndex < vertexCount; dnaVertexIndex++)
            {
                const auto positionOutput = outputReader->getVertexPosition(uint16_t(meshIndex), uint32_t(dnaVertexIndex));
                const auto positionGoldData = goldDataReader->getVertexPosition(uint16_t(meshIndex), uint32_t(dnaVertexIndex));
                auto diff = positionOutput - positionGoldData;
                if (std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z) >= positionTolerance)
                {
                    LOG_ERROR("Mesh {} vertex {} has different values, current = {}, {}, {}, gold data = {}, {}, {}", meshIndex, vertexCount,
                        positionOutput.x, positionOutput.y, positionOutput.z, positionGoldData.x, positionGoldData.y, positionGoldData.z);
                    bResult = false;
                }

                const auto normalOutput = outputReader->getVertexNormal(uint16_t(meshIndex), uint32_t(dnaVertexIndex));
                const auto normalGoldData = goldDataReader->getVertexNormal(uint16_t(meshIndex), uint32_t(dnaVertexIndex));
                diff = normalOutput - normalGoldData;
                if (std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z) >= positionTolerance) // TODO could be done better
                {
                    LOG_ERROR("Mesh {} normal {} has different values, current = {}, {}, {}, gold data = {}, {}, {}", meshIndex, vertexCount,
                        normalOutput.x, normalOutput.y, normalOutput.z, normalGoldData.x, normalGoldData.y, normalGoldData.z);
                    bResult = false;
                }

                if (bCompareSkinningWeights)
                {
                    const auto skinWeightValuesOutput = outputReader->getSkinWeightsValues(uint16_t(meshIndex), uint32_t(dnaVertexIndex));
                    const auto skinWeightValuesGoldData = goldDataReader->getSkinWeightsValues(uint16_t(meshIndex), uint32_t(dnaVertexIndex));
                    if (skinWeightValuesOutput.size() != skinWeightValuesGoldData.size())
                    {
                        LOG_ERROR("Mesh {} skin weights for vertex {} have different numbers of values, current = {}, gold = {}", meshIndex, dnaVertexIndex, skinWeightValuesOutput.size(), skinWeightValuesGoldData.size());
                        bResult = false;
                    }
                    else
                    {
                        for (size_t i = 0; i < skinWeightValuesOutput.size(); ++i)
                        {
                            if (std::fabs(skinWeightValuesGoldData[i] - skinWeightValuesOutput[i]) >= weightTolerance)
                            {
                                LOG_ERROR("Mesh {} skin weights for vertex {}, value {} have different values", meshIndex, dnaVertexIndex, i);
                                bResult = false;
                            }
                        }
                    }
                }
            }
        }
    }
    return bResult;
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)