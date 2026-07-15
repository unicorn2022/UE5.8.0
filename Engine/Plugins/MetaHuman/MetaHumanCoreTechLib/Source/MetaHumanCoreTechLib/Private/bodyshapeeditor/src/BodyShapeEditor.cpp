// Copyright Epic Games, Inc. All Rights Reserved.

#include <bodyshapeeditor/BodyShapeEditor.h>
#include <bodyshapeeditor/WeightSchedule.h>
#include <bodyshapeeditor/BodyMeasurement.h>
#include <bodyshapeeditor/SerializationHelper.h>
#include <bodyshapeeditor/BodyJointEstimator.h>
#include <rig/RigLogic.h>
#include <rig/TwistSwingLogic.h>
#include <trio/Stream.h>
#include <Eigen/src/Core/Map.h>
#include <arrayview/ArrayView.h>
#include <carbon/Algorithm.h>
#include <carbon/geometry/AABBTree.h>
#include <carbon/common/Log.h>
#include <carbon/utils/ObjectPool.h>
#include <carbon/utils/StringReplace.h>
#include <carbon/utils/StringUtils.h>
#include <carbon/utils/Timer.h>
#include <filesystem>
#include <optional>
#include <regex>
#include <terse/archives/binary/InputArchive.h>
#include <terse/archives/binary/OutputArchive.h>
#include <rig/BodyGeometry.h>
#include <rig/BodyLogic.h>
#include <rig/RBFLogic.h>
#include <rig/SymmetricControls.h>
#include <rig/SkinningWeightUtils.h>
#include <carbon/common/Defs.h>
#include <nls/Context.h>
#include <nls/Cost.h>
#include <nrr/deformation_models/DeformationModelVertex.h>
#include <nrr/ICPConstraints.h>
#include <nrr/CollisionConstraints.h>
#include <nls/geometry/EulerAngles.h>
#include <nls/geometry/Procrustes.h>
#include <nrr/landmarks/LandmarkTriangulation.h>
#include <nls/geometry/Quaternion.h>
#include <nls/BoundedVectorVariable.h>
#include <nls/solver/GaussNewtonSolver.h>
#include <nls/DiffData.h>
#include <nls/math/Math.h>
#include <nls/math/ParallelBLAS.h>
#include <nls/VectorVariable.h>
#include <rig/LimitConstraintFunction.h>
#include <nls/functions/ProjectionConstraintFunction.h>
#include <nls/functions/AxisConstraintFunction.h>
#include <nls/functions/LengthConstraintFunction.h>
#include <nls/functions/SubtractFunction.h>
#include <nls/functions/PointPointConstraintFunction.h>
#include <nls/geometry/Mesh.h>
#include <nls/geometry/TriangleBending.h>
#include <nls/geometry/VertexLaplacian.h>
#include <nrr/landmarks/LandmarkConstraints2D.h>

#include <Eigen/src/Core/Matrix.h>
#include <carbon/io/JsonIO.h>


#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

namespace
{
struct SparseMatrixPCA
{
    SparseMatrix<float> mean;
    std::vector<Eigen::MatrixXf> mods;
    std::vector<std::vector<int>> rowsPerPart;
    std::vector<std::vector<int>> colIndicesPerRow;
    Eigen::MatrixXf globalToMods;


    int pcaModCount()
    {
        int i = 0;
        for (const auto& m : mods)
        {
            i += m.cols();
        }
        return i;
    }

    int numColsForRows(const std::vector<int>& rows)
    {
        int totalCols = 0;
        for (int ri : rows)
        {
            totalCols += static_cast<int>(colIndicesPerRow[ri].size());
        }
        return totalCols;
    }

    void ReadFromDNA(const dna::Reader* reader, std::string model_name)
    {
        auto modelStr64 = reader->getMetaDataValue(model_name.c_str());
        if (modelStr64.size() == 0u)
        {
            return;
        }
        auto modelStr = TITAN_NAMESPACE::Base64Decode(std::string { modelStr64.begin(), modelStr64.end() });
        auto stream = pma::makeScoped<trio::MemoryStream>(modelStr.size());
        stream->open();
        stream->write(modelStr.data(), modelStr.size());
        stream->seek(0);
        modelStr.clear();
        terse::BinaryInputArchive<trio::MemoryStream> archive { stream.get() };

        std::vector<std::uint32_t> rows;
        std::vector<std::uint32_t> cols;
        std::vector<float> values;

        SparseMatrix<float>::Index colCount;
        SparseMatrix<float>::Index rowCount;
        archive(colCount);
        archive(rowCount);
        archive(rows);
        archive(cols);
        archive(values);
        CARBON_ASSERT(rows.size() == cols.size(), "Model matrix has wrong entries");
        CARBON_ASSERT(rows.size() == values.size(), "Model matrix has wrong entries");
        std::vector<Eigen::Triplet<float>> triplets;
        triplets.reserve(values.size());
        for (size_t j = 0; j < rows.size(); j++)
        {
            triplets.push_back(Eigen::Triplet<float>(rows[j], cols[j], values[j]));
        }
        mean.resize(rowCount, colCount);
        mean.setFromTriplets(triplets.begin(), triplets.end());

        auto archiveDynMatrix = [&](Eigen::MatrixXf& matrix)
        {
            Eigen::MatrixXf::Index colCount;
            Eigen::MatrixXf::Index rowCount;
            archive(colCount);
            archive(rowCount);
            std::vector<float> values;
            archive(values);
            matrix = Eigen::Map<Eigen::MatrixXf> { values.data(), rowCount, colCount };
        };

        decltype(mods)::size_type modCount;
        archive(modCount);

        mods.resize(modCount);
        for (std::size_t mi = 0u; mi < modCount; ++mi)
        {
            archiveDynMatrix(mods[mi]);
        }

        archive(rowsPerPart);
        archive(colIndicesPerRow);
        archiveDynMatrix(globalToMods);
    }

    void WriteToStream(trio::BoundedIOStream* stream)
    {
        terse::BinaryOutputArchive<trio::BoundedIOStream> archive { stream };
        std::string serializationVersion = ("0.0.2");
        archive(serializationVersion);
        std::vector<std::uint32_t> rows;
        std::vector<std::uint32_t> cols;
        std::vector<float> values;
        for (int k = 0; k < mean.outerSize(); ++k)
        {
            for (SparseMatrix<float>::InnerIterator it(mean, k); it; ++it)
            {
                rows.push_back(it.row());
                cols.push_back(it.col());
                values.push_back(it.value());
            }
        }
        archive(mean.cols());
        archive(mean.rows());
        archive(rows);
        archive(cols);
        archive(values);
        archive(mods.size());
        for (const auto& mod : mods)
        {
            archive(mod.cols());
            archive(mod.rows());
            archive(std::vector<float>(mod.data(), mod.data() + mod.size()));
        }

        archive(rowsPerPart);
        archive(colIndicesPerRow);
        archive(globalToMods.cols());
        archive(globalToMods.rows());
        archive(std::vector<float>(globalToMods.data(), globalToMods.data() + globalToMods.size()));
    }

    void ReadFromStream(trio::BoundedIOStream* stream)
    {
        terse::BinaryInputArchive<trio::BoundedIOStream> archive { stream };
        std::string serializationVersion;
        archive(serializationVersion);

        if (serializationVersion != "0.0.2")
        {
            CARBON_CRITICAL("Serialization version mismatch: expected 0.0.2, got {}", serializationVersion.c_str());
        }

        std::vector<std::uint32_t> rows;
        std::vector<std::uint32_t> cols;
        std::vector<float> values;

        SparseMatrix<float>::Index colCount;
        SparseMatrix<float>::Index rowCount;
        archive(colCount);
        archive(rowCount);
        archive(rows);
        archive(cols);
        archive(values);
        CARBON_ASSERT(rows.size() == cols.size(), "Model matrix has wrong entries");
        std::vector<Eigen::Triplet<float>> triplets;
        triplets.reserve(values.size());
        CARBON_ASSERT(rows.size() == values.size(), "Model matrix has wrong entries");
        for (size_t j = 0; j < rows.size(); j++)
        {
            triplets.push_back(Eigen::Triplet<float>(rows[j], cols[j], values[j]));
        }
        {
            mean.resize(rowCount, colCount);
            mean.setFromTriplets(triplets.begin(), triplets.end());
        }

        auto archiveDynMatrix = [&](Eigen::MatrixXf& matrix)
        {
            Eigen::MatrixXf::Index colCount;
            Eigen::MatrixXf::Index rowCount;
            archive(colCount);
            archive(rowCount);
            std::vector<float> values;
            archive(values);
            matrix = Eigen::Map<Eigen::MatrixXf> { values.data(), rowCount, colCount };
        };

        decltype(mods)::size_type modCount;
        archive(modCount);

        mods.resize(modCount);
        for (std::size_t mi = 0u; mi < modCount; ++mi)
        {
            archiveDynMatrix(mods[mi]);
        }

        archive(rowsPerPart);
        archive(colIndicesPerRow);
        archiveDynMatrix(globalToMods);
    }

    SparseMatrix<float> calculateResult(const Eigen::VectorXf& global)
    {
        auto pcaCoeffAllRegions = globalToMods * global;
        std::size_t inputOffset = 0;
        auto result = mean;
        for (int ri = 0; ri < mods.size(); ++ri)
        {
            const auto& mod = mods[ri];
            auto pcaCoeff = pcaCoeffAllRegions.middleRows(inputOffset, mod.cols()).transpose().array();
            Eigen::VectorXf regionResult = mod.col(0) * pcaCoeff[0];

            for (int mi = 1; mi < mod.cols(); ++mi)
            {
                regionResult += mod.col(mi) * pcaCoeff[mi];
            }

            inputOffset += mod.cols();

            int jOffset = 0;
            for (int i = 0; i < rowsPerPart[ri].size(); ++i)
            {
                const auto rowIndex = rowsPerPart[ri][i];
                for (std::uint16_t j = 0; j < colIndicesPerRow[rowIndex].size(); ++j)
                {
                    const auto ji = colIndicesPerRow[rowIndex][j];
                    result.coeffRef(rowIndex, ji) += regionResult[jOffset];
                    jOffset++;
                }
            }
        }
        return result;
    }
};

template <typename T>
std::shared_ptr<BodyLogic<T>> PoseLogicFromString(const std::vector<std::string>& lines,
    const std::shared_ptr<BodyLogic<T>>& rl,
    const std::shared_ptr<BodyGeometry<T>>& bg,
    const std::vector<int>& coreJoints,
    const std::string controlNamePrefix = "pose_")
{
    // create a copy of the input logic
    std::shared_ptr<BodyLogic<T>> logic = rl->Clone();

    // dof lookup table
    static const std::array<std::string, 6> dofNames = { "tx", "ty", "tz", "rx", "ry", "rz" };
    auto getDofIndex = [&](const std::string& name)
    {
        for (int id = 0; id < 6; id++)
        {
            if (dofNames[id] == name)
            {
                return id;
            }
        }
        return -1;
    };

    // regex splitting line up into [parameter name] [limit min] [limit max] [limit weight] [joint group]
    std::regex parameterEx("(\\S*)\\s*\\[\\s*([+-]?[0-9]*[.]?[0-9]+)\\s*,\\s*([+-]?[0-9]*[.]?[0-9]+),\\s*([+-]?[0-9]*[.]?[0-9]+)\\s*\\]\\s*->\\s*(.*)");
    std::regex jointEx("\\s*([+-]?[0-9]*[.]?[0-9]+)\\s*\\*\\s*(\\w*).([tr][xyz])\\s*,*");

    // go through all lines of the string
    std::smatch res;

    std::vector<Eigen::Triplet<T>> jointTriplets;

    SparseMatrix<T> jointMatrix = logic->GetJointMatrix(0);
    auto replaceAllLR = [](std::string s, char side)
    {
        const std::string from = "_LR";
        const std::string to(1, side);
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos)
        {
            s.replace(pos + 1, from.length() - 1, to);
            pos += to.length();
        }
        return s;
    };

    struct DriverControlDef {
        std::pair<T, T> bounds;
        std::vector<std::tuple<int, int, T>> jointMappings; // jointId, dofId, weight
    };
    std::map<std::string, DriverControlDef> driverControlDefs;

    const auto parseLine = [&](const std::string& line)
    {
        if (std::regex_search(line, res, parameterEx) && (res.size() == 6))
        {
            std::string controlName = res[1].str();
            const T limitMin = static_cast<T>(std::stod(res[2]));
            const T limitMax = static_cast<T>(std::stod(res[3]));
            const std::string jointData = res[5];

            if (controlName.starts_with("pelvis") || controlName.starts_with("root"))
            {
                // Root: translation only. All rotation goes through pelvis.
                if (controlName.starts_with("root.r"))
                    return;

                std::string name = controlNamePrefix + "rigid_" + controlName;

                const int guiIndex = logic->NumGUIControls();
                const int rawIndex = logic->NumRawControls();
                logic->GuiControlNames().push_back(name);
                logic->RawControlNames().push_back(name);
                logic->GuiToRawMapping().push_back({ guiIndex, rawIndex, limitMin, limitMax, 1.0f, 0.0f });
                logic->GuiControlRanges().conservativeResize(2, logic->GuiControlRanges().cols() + 1);
                logic->GuiControlRanges()(0, guiIndex) = limitMin;
                logic->GuiControlRanges()(1, guiIndex) = limitMax;

                jointMatrix.conservativeResize(jointMatrix.rows(), jointMatrix.cols() + 1);

                for (std::sregex_iterator i = std::sregex_iterator(jointData.begin(), jointData.end(), jointEx); i != std::sregex_iterator(); i++)
                {
                    std::smatch jmap = *i;
                    if (jmap.size() == 4)
                    {
                        const int jointId = bg->GetJointIndex(jmap[2]);
                        const int dofId = getDofIndex(jmap[3]);
                        const T weight = static_cast<T>(std::stod(jmap[1]));

                        if ((jointId < 0) || (dofId < 0))
                        {
                            continue;
                        }

                        jointMatrix.coeffRef(jointId * 9 + dofId, rawIndex) = weight;
                    }
                }
            }
            else
            {
                DriverControlDef def;
                def.bounds = {limitMin, limitMax};

                for (std::sregex_iterator i = std::sregex_iterator(jointData.begin(), jointData.end(), jointEx); i != std::sregex_iterator(); i++)
                {
                    std::smatch jmap = *i;
                    if (jmap.size() == 4)
                    {
                        const int jointId = bg->GetJointIndex(jmap[2]);
                        const int dofId = getDofIndex(jmap[3]);
                        const T weight = static_cast<T>(std::stod(jmap[1]));

                        if ((jointId >= 0) && (dofId >= 0))
                        {
                            def.jointMappings.push_back({jointId, dofId, weight});
                        }
                    }
                }

                driverControlDefs[controlName] = def;
            }
        }
    };

    for (const auto& line : lines)
    {
        if (line.empty() || (line[0] == '#'))
        {
            continue;
        }

        if (line.find("_LR") != std::string::npos)
        {
            parseLine(replaceAllLR(line, 'l'));
            parseLine(replaceAllLR(line, 'r'));
        } else
        {
            parseLine(line);
        }
    }

    // Ensure pelvis has all 3 rotation DOFs for Procrustes alignment.
    {
        const int pelvisJointId = bg->GetJointIndex("pelvis");
        if (pelvisJointId >= 0)
        {
            for (int dof = 0; dof < 3; ++dof)
            {
                std::string name = controlNamePrefix + "rigid_pelvis.r" + std::string(1, "xyz"[dof]);
                if (std::find(logic->GuiControlNames().begin(), logic->GuiControlNames().end(), name) != logic->GuiControlNames().end())
                    continue;
                const int guiIndex = logic->NumGUIControls();
                const int rawIndex = logic->NumRawControls();
                logic->GuiControlNames().push_back(name);
                logic->RawControlNames().push_back(name);
                logic->GuiToRawMapping().push_back({ guiIndex, rawIndex, T(-180), T(180), T(1), T(0) });
                logic->GuiControlRanges().conservativeResize(2, logic->GuiControlRanges().cols() + 1);
                logic->GuiControlRanges()(0, guiIndex) = T(-180);
                logic->GuiControlRanges()(1, guiIndex) = T(180);
                jointMatrix.conservativeResize(jointMatrix.rows(), jointMatrix.cols() + 1);
                jointMatrix.coeffRef(pelvisJointId * 9 + 3 + dof, rawIndex) = T(1);
            }
        }
    }

    for (int ji : coreJoints)
    {
        if (ji < 2)
        {
            continue;
        }
        int i = 0;
        const std::string jointName = bg->GetJointNames()[ji];

        T limitMin = static_cast<T>(-CARBON_PI / 8.0);
        T limitMax = static_cast<T>(CARBON_PI / 8.0);

        if(jointName.find("toe_", 0) != std::string::npos)
        {
            continue;
        }

        // thumb_01 is the base joint and stands in for the missing metacarpal, so
        // it gets all three rotation axes (abduction/opposition + curl). thumb_02
        // and thumb_03, like the other fingers' phalanges, stay rz-only. Default
        // limits stay at ±π/8 for rx/ry; driverControlDefs can override per-axis.
        const bool isThumbBase = jointName.starts_with("thumb_01");
        if (!isThumbBase)
        {
            for (const auto& prefix : { "pinky_0", "ring_0", "index_0", "thumb_0", "middle_0" })
            {
                if (jointName.starts_with(prefix))
                {
                    limitMin = static_cast<T>(-0.1);
                    limitMax = static_cast<T>(1.2);
                    i = 2;
                    break;
                }
            }
        }

        for (; i < 3; ++i)
        {
            const int guiIndex = logic->NumGUIControls();
            const int rawIndex = logic->NumRawControls();
            std::string name = controlNamePrefix + "driver_" + jointName + "." + dofNames[3 + i];

            std::string controlKey = jointName + "." + dofNames[3 + i];
            T finalLimitMin = limitMin;
            T finalLimitMax = limitMax;

            auto defIt = driverControlDefs.find(controlKey);
            if (defIt != driverControlDefs.end())
            {
                finalLimitMin = defIt->second.bounds.first;
                finalLimitMax = defIt->second.bounds.second;
            }

            logic->GuiControlNames().push_back(name);
            logic->RawControlNames().push_back(name);
            logic->GuiToRawMapping().push_back({ guiIndex, rawIndex, finalLimitMin, finalLimitMax, 1.0f, 0.0f });
            logic->GuiControlRanges().conservativeResize(2, logic->GuiControlRanges().cols() + 1);
            logic->GuiControlRanges()(0, guiIndex) = finalLimitMin;
            logic->GuiControlRanges()(1, guiIndex) = finalLimitMax;
            jointMatrix.conservativeResize(jointMatrix.rows(), jointMatrix.cols() + 1);

            if (defIt != driverControlDefs.end() && !defIt->second.jointMappings.empty())
            {
                for (const auto& [mappedJointId, mappedDofId, weight] : defIt->second.jointMappings)
                {
                    jointMatrix.coeffRef(mappedJointId * 9 + mappedDofId, rawIndex) = weight;
                }
            }
            else
            {
                jointMatrix.coeffRef(ji * 9 + 3 + i, rawIndex) = 1.0f;
            }
        }
    }

    logic->GetJointMatrix(0) = jointMatrix;

    return logic;
}

void AddKeypointConstraints(
    Cost<float>& cost,
    const DiffDataMatrix<float, 3, -1>& vertices,
    const std::vector<std::pair<int, Eigen::Vector3f>>& keypointCorrespondences,
    const Eigen::VectorXf& vertexMask,
    float weight,
    int maxVertexIndex = -1,
    const std::string& tag = "keypoints")
{
    if (keypointCorrespondences.empty() || weight <= 0.0f)
    {
        return;
    }

    std::vector<std::pair<int, Eigen::Vector3f>> filtered;
    if (maxVertexIndex >= 0)
    {
        for (const auto& kp : keypointCorrespondences)
        {
            if (kp.first >= 0 && kp.first < maxVertexIndex)
            {
                filtered.push_back(kp);
            }
        }
    }
    const auto& keypoints = (maxVertexIndex >= 0) ? filtered : keypointCorrespondences;
    if (keypoints.empty())
    {
        return;
    }

    const int numKeypoints = static_cast<int>(keypoints.size());
    Eigen::VectorXi keypointIndices(numKeypoints);
    Eigen::Matrix<float, 3, -1> keypointTargets(3, numKeypoints);
    Vector<float> keypointWeights = Vector<float>::Ones(numKeypoints);

    // Keypoints are explicit user-placed constraints — they apply at full
    // weight regardless of vertexMask. The mask gates the data term (ICP /
    // refinement correspondence) but not these. Earlier behaviour multiplied
    // keypoint weights by the mask value, which silently disabled keypoints
    // sitting in masked regions; that's almost never what the user wants
    // when they bothered placing a keypoint there.
    for (int i = 0; i < numKeypoints; ++i)
    {
        keypointIndices[i] = keypoints[i].first;
        keypointTargets.col(i) = keypoints[i].second;
    }
    (void)vertexMask;

    cost.Add(PointPointConstraintFunction<float, 3>::Evaluate(
                 vertices, keypointIndices, keypointTargets, keypointWeights, weight),
        1.0f, tag);
}

void AddJointConstraints(
    Cost<float>& cost,
    const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& worldMatrices,
    const Eigen::Matrix<float, -1, -1, Eigen::RowMajor>& worldMatricesJacobian,
    const std::vector<std::pair<int, Eigen::Vector3f>>& jointCorrespondences,
    const Eigen::VectorXf& jointMask,
    float weight)
{
    if (jointCorrespondences.empty() || weight <= 0.0f)
    {
        return;
    }

    const int numJointConstraints = static_cast<int>(jointCorrespondences.size());
    Eigen::VectorXf jointResiduals(numJointConstraints * 3);
    auto jointJacobian = std::make_shared<Eigen::Matrix<float, -1, -1, Eigen::RowMajor>>(
        numJointConstraints * 3, worldMatricesJacobian.cols());

    for (int i = 0; i < numJointConstraints; ++i)
    {
        const int jointIndex = jointCorrespondences[i].first;
        if (jointIndex >= 0 && jointIndex < static_cast<int>(worldMatrices.size()))
        {
            const Eigen::Vector3f jointPos = worldMatrices[jointIndex].translation();
            const Eigen::Vector3f targetPos = jointCorrespondences[i].second;
            float jointWeight = weight;
            if (jointMask.size() > 0 && jointIndex < static_cast<int>(jointMask.size()))
            {
                jointWeight *= jointMask[jointIndex];
            }
            jointResiduals.segment<3>(i * 3) = jointWeight * (jointPos - targetPos);

            if (worldMatricesJacobian.rows() >= (jointIndex + 1) * 12)
            {
                const int baseRow = jointIndex * 12;
                jointJacobian->row(i * 3 + 0) = jointWeight * worldMatricesJacobian.row(baseRow + 9);
                jointJacobian->row(i * 3 + 1) = jointWeight * worldMatricesJacobian.row(baseRow + 10);
                jointJacobian->row(i * 3 + 2) = jointWeight * worldMatricesJacobian.row(baseRow + 11);
            }
        }
    }
    cost.Add(DiffData<float>(jointResiduals, std::make_shared<DenseJacobian<float>>(jointJacobian, 0)), 1.0f, "joints");
}

void AddViewport2DJointConstraints(
    Cost<float>& cost,
    const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& worldMatrices,
    const Eigen::Matrix<float, -1, -1, Eigen::RowMajor>& worldMatricesJacobian,
    const Eigen::Matrix4f& projectionMatrix,
    const std::vector<Constraint2D>& constraints2D,
    const Eigen::VectorXf& jointMask,
    float weight,
    const std::string& tag)
{
    int numJointConstraints = 0;
    for (const auto& c : constraints2D)
    {
        if (c.isJoint && c.index >= 0 && c.index < static_cast<int>(worldMatrices.size()))
        {
            ++numJointConstraints;
        }
    }
    if (numJointConstraints == 0)
    {
        return;
    }

    Eigen::VectorXf residuals2D(numJointConstraints * 2);
    std::shared_ptr<Eigen::Matrix<float, -1, -1, Eigen::RowMajor>> jacobian2D;

    const bool hasJointJacobian = worldMatricesJacobian.cols() > 0;
    if (hasJointJacobian)
    {
        jacobian2D = std::make_shared<Eigen::Matrix<float, -1, -1, Eigen::RowMajor>>(
            numJointConstraints * 2, worldMatricesJacobian.cols());
        jacobian2D->setZero();
    }

    int constraintIdx = 0;
    for (const auto& c : constraints2D)
    {
        if (!c.isJoint || c.index < 0 || c.index >= static_cast<int>(worldMatrices.size()))
        {
            continue;
        }

        const Eigen::Vector3f pos3D = worldMatrices[c.index].translation();
        const Eigen::Vector4f clipPos = projectionMatrix * Eigen::Vector4f(pos3D.x(), pos3D.y(), pos3D.z(), 1.0f);
        const float w = clipPos.w();
        if (w <= 0.0f)
        {
            residuals2D.segment<2>(constraintIdx * 2).setZero();
            ++constraintIdx;
            continue;
        }

        const Eigen::Vector2f screenPos(clipPos.x() / w, clipPos.y() / w);
        float constraintWeight = c.weight * weight;
        if (jointMask.size() > 0 && c.index < static_cast<int>(jointMask.size()))
        {
            constraintWeight *= jointMask[c.index];
        }
        residuals2D.segment<2>(constraintIdx * 2) = constraintWeight * (screenPos - c.screenPosition);

        if (jacobian2D)
        {
            const float w2 = w * w;
            Eigen::Matrix<float, 2, 3> dScreen_dPos3D;
            dScreen_dPos3D.row(0) = (projectionMatrix.row(0).head<3>() * w - clipPos.x() * projectionMatrix.row(3).head<3>()) / w2;
            dScreen_dPos3D.row(1) = (projectionMatrix.row(1).head<3>() * w - clipPos.y() * projectionMatrix.row(3).head<3>()) / w2;

            const int baseRow = c.index * 12;
            if (worldMatricesJacobian.rows() >= baseRow + 12)
            {
                Eigen::Matrix<float, 3, -1> dPos3D_dParams(3, worldMatricesJacobian.cols());
                dPos3D_dParams.row(0) = worldMatricesJacobian.row(baseRow + 9);
                dPos3D_dParams.row(1) = worldMatricesJacobian.row(baseRow + 10);
                dPos3D_dParams.row(2) = worldMatricesJacobian.row(baseRow + 11);

                jacobian2D->row(constraintIdx * 2 + 0) = constraintWeight * (dScreen_dPos3D.row(0) * dPos3D_dParams);
                jacobian2D->row(constraintIdx * 2 + 1) = constraintWeight * (dScreen_dPos3D.row(1) * dPos3D_dParams);
            }
        }
        ++constraintIdx;
    }

    if (jacobian2D)
    {
        cost.Add(DiffData<float>(residuals2D, std::make_shared<DenseJacobian<float>>(jacobian2D, 0)), 1.0f, tag);
    }
    else
    {
        cost.Add(DiffData<float>(residuals2D), 1.0f, tag);
    }
}

void AddViewport2DVertexConstraints(
    Cost<float>& cost,
    const DiffDataMatrix<float, 3, -1>& vertices,
    const Eigen::Matrix4f& projectionMatrix,
    const std::vector<Constraint2D>& constraints2D,
    const Eigen::VectorXf& vertexMask,
    float weight,
    int maxVertexIndex = -1,
    const std::string& tag = "constraints_2d_vertices")
{
    const int numVertices = static_cast<int>(vertices.Cols());
    const int vertexLimit = (maxVertexIndex >= 0) ? maxVertexIndex : numVertices;

    int numVertexConstraints = 0;
    for (const auto& c : constraints2D)
    {
        if (!c.isJoint && c.index >= 0 && c.index < vertexLimit)
        {
            ++numVertexConstraints;
        }
    }
    if (numVertexConstraints == 0)
    {
        return;
    }

    Eigen::VectorXf residuals2D(numVertexConstraints * 2);
    std::shared_ptr<Eigen::Matrix<float, -1, -1, Eigen::RowMajor>> jacobian2D;

    const bool hasVertexJacobian = vertices.HasJacobian();
    const int numParams = hasVertexJacobian ? vertices.Jacobian().Cols() : 0;
    jacobian2D = std::make_shared<Eigen::Matrix<float, -1, -1, Eigen::RowMajor>>(
        numVertexConstraints * 2, numParams);
    jacobian2D->setZero();

    int constraintIdx = 0;
    for (const auto& c : constraints2D)
    {
        if (c.isJoint || c.index < 0 || c.index >= vertexLimit)
        {
            continue;
        }

        const Eigen::Vector3f pos3D = vertices.Matrix().col(c.index);
        const Eigen::Vector4f clipPos = projectionMatrix * Eigen::Vector4f(pos3D.x(), pos3D.y(), pos3D.z(), 1.0f);
        const float w = clipPos.w();
        if (w <= 0.0f)
        {
            residuals2D.segment<2>(constraintIdx * 2).setZero();
            ++constraintIdx;
            continue;
        }

        const Eigen::Vector2f screenPos(clipPos.x() / w, clipPos.y() / w);
        float constraintWeight = c.weight * weight;
        if (vertexMask.size() > 0 && c.index < static_cast<int>(vertexMask.size()))
        {
            constraintWeight *= vertexMask[c.index];
        }
        residuals2D.segment<2>(constraintIdx * 2) = constraintWeight * (screenPos - c.screenPosition);

        if (hasVertexJacobian)
        {
            const float w2 = w * w;
            Eigen::Matrix<float, 2, 3> dScreen_dPos3D;
            dScreen_dPos3D.row(0) = (projectionMatrix.row(0).head<3>() * w - clipPos.x() * projectionMatrix.row(3).head<3>()) / w2;
            dScreen_dPos3D.row(1) = (projectionMatrix.row(1).head<3>() * w - clipPos.y() * projectionMatrix.row(3).head<3>()) / w2;

            const int baseRow = c.index * 3;
            Eigen::Matrix<float, 3, -1> dPos3D_dParams(3, numParams);
            for (int r = 0; r < 3; ++r)
            {
                const Eigen::SparseVector<float> row = vertices.Jacobian().Row(baseRow + r);
                dPos3D_dParams.row(r).setZero();
                for (Eigen::SparseVector<float>::InnerIterator it(row); it; ++it)
                {
                    dPos3D_dParams(r, static_cast<int>(it.index())) = it.value();
                }
            }

            jacobian2D->row(constraintIdx * 2 + 0) = constraintWeight * (dScreen_dPos3D.row(0) * dPos3D_dParams);
            jacobian2D->row(constraintIdx * 2 + 1) = constraintWeight * (dScreen_dPos3D.row(1) * dPos3D_dParams);
        }
        ++constraintIdx;
    }

    if (jacobian2D)
    {
        cost.Add(DiffData<float>(residuals2D, std::make_shared<DenseJacobian<float>>(jacobian2D, 0)), 1.0f, tag);
    }
    else
    {
        cost.Add(DiffData<float>(residuals2D), 1.0f, tag);
    }
}

void AddViewport2DConstraints(
    Cost<float>& cost,
    const DiffDataMatrix<float, 3, -1>& vertices,
    const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>* worldMatrices,
    const Eigen::Matrix<float, -1, -1, Eigen::RowMajor>* worldMatricesJacobian,
    const std::vector<ViewportConstraints2D>& viewportConstraints2D,
    const Eigen::VectorXf& vertexMask,
    const Eigen::VectorXf& jointMask,
    float weight,
    int maxVertexIndex = -1)
{
    for (size_t viewportIdx = 0; viewportIdx < viewportConstraints2D.size(); ++viewportIdx)
    {
        const auto& [projectionMatrix, constraints2D] = viewportConstraints2D[viewportIdx];
        if (constraints2D.empty() || projectionMatrix.isIdentity())
        {
            continue;
        }

        const std::string viewportSuffix = "_v" + std::to_string(viewportIdx);

        if (worldMatrices && worldMatricesJacobian)
        {
            AddViewport2DJointConstraints(
                cost, *worldMatrices, *worldMatricesJacobian,
                projectionMatrix, constraints2D, jointMask, weight,
                "constraints_2d_joints" + viewportSuffix);
        }

        AddViewport2DVertexConstraints(
            cost, vertices, projectionMatrix, constraints2D,
            vertexMask, weight, maxVertexIndex,
            "constraints_2d_vertices" + viewportSuffix);
    }
}

void AddLandmark2DConstraints(
    Cost<float>& cost,
    const DiffDataMatrix<float, 3, -1>& vertices,
    const Eigen::Matrix<float, 3, -1>& normals,
    const LandmarkConstraints2D<float>* landmarkConstraints,
    float weight)
{
    if (!landmarkConstraints || weight <= 0.0f)
    {
        return;
    }
    Cost<float> lmCost = landmarkConstraints->Evaluate(vertices, normals);
    const int numResiduals = lmCost.Value().size();
    const float normalizedWeight = (numResiduals > 0) ? weight / std::sqrt(static_cast<float>(numResiduals)) : weight;
    cost.Add(std::move(lmCost), normalizedWeight, true);
}
	
void UpdateLandmarkConstraintsConfig(LandmarkConstraints2D<float>* landmarkConstraints, const BodySolveConfiguration& options)
{
	if (landmarkConstraints)
	{
		Configuration currentConfig = landmarkConstraints->GetConfiguration();
		currentConfig["landmarksWeight"] = options.body["landmark2D"].Value<float>();
		currentConfig["innerLipWeight"] = 0.0f;
		currentConfig["curveResampling"] = options.body["curveResampling"].Value<int>();
		landmarkConstraints->SetConfiguration(currentConfig);
	}
}

static WeightSchedule LoadWeightSchedule(const Configuration& cfg, const std::string& prefix)
{
    WeightSchedule s;
    s.start = cfg[prefix].Value<float>();
    s.end   = cfg[prefix + "End"].Value<float>();
    s.curve = static_cast<ScheduleCurve>(cfg[prefix + "Curve"].Value<int>());
    if (s.curve == ScheduleCurve::Static) s.end = s.start;
    return s;
}

constexpr float scaleTolerence = 1e-9f;

Eigen::Matrix<float, 3, -1> ScaleVertices(const Eigen::Matrix<float, 3, -1>& vertices, float scale)
{
    Eigen::Matrix<float, 3, -1> scaledVertices = vertices;
    
    if (std::abs(scale - 1.0f) >= scaleTolerence)
    {
        scaledVertices *= scale;
    }
    return scaledVertices;
}

DiffDataMatrix<float, 3, -1> ScaleVertices(const DiffDataMatrix<float, 3, -1>& vertices, float scale)
{
    if (std::abs(scale - 1.0f) < scaleTolerence)
    {
        return DiffDataMatrix<float, 3, -1>(vertices.Rows(), vertices.Cols(), vertices.Clone());
    }

    if (vertices.HasJacobian()) {
        return DiffDataMatrix<float, 3, -1>(
            vertices.Rows(),
            vertices.Cols(),
            DiffData<float>(scale * vertices.Value(), vertices.Jacobian().Scale(scale))
        );
    } else {
        return DiffDataMatrix<float, 3, -1>(
            vertices.Rows(),
            vertices.Cols(),
            DiffData<float>(scale * vertices.Value())
        );
    }
}

// Scale vertices with scale as an optimizable DiffData parameter.
// Product rule: d(s*v)/d[params, s] = [s * dv/dparams, v]
DiffDataMatrix<float, 3, -1> ScaleVerticesDiff(
    const DiffDataMatrix<float, 3, -1>& vertices,
    const DiffData<float>& scaleDiff)
{
    const float s = scaleDiff.Value()[0];
    const Eigen::VectorXf scaledValue = s * vertices.Value();
    const int numRows = static_cast<int>(vertices.Value().size()); // 3 * numVertices

    if (!vertices.HasJacobian() && !scaleDiff.HasJacobian())
    {
        return DiffDataMatrix<float, 3, -1>(vertices.Rows(), vertices.Cols(),
            DiffData<float>(scaledValue));
    }

    const int totalCols = std::max(
        vertices.HasJacobian() ? vertices.Jacobian().Cols() : 0,
        scaleDiff.HasJacobian() ? scaleDiff.Jacobian().Cols() : 0);

    auto denseJ = std::make_shared<Eigen::Matrix<float, -1, -1, Eigen::RowMajor>>(numRows, totalCols);
    denseJ->setZero();

    // d(s*v)/d[params] = s * dv/d[params]
    if (vertices.HasJacobian())
    {
        const auto& vJ = vertices.Jacobian();
        const int startCol = vJ.StartCol();
        const int numCols = vJ.Cols() - startCol;
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> vJDense(numRows, numCols);
        vJ.CopyToDenseMatrix(vJDense);
        denseJ->block(0, startCol, numRows, numCols) = s * vJDense;
    }

    // d(s*v)/ds = v
    if (scaleDiff.HasJacobian())
    {
        const int scaleCol = scaleDiff.Jacobian().StartCol();
        denseJ->col(scaleCol) += vertices.Value();
    }

    return DiffDataMatrix<float, 3, -1>(vertices.Rows(), vertices.Cols(),
        DiffData<float>(scaledValue, std::make_shared<DenseJacobian<float>>(denseJ, 0)));
}

void ScaleJointMatrices(std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices, float scale)
{
    if (std::abs(scale - 1.0f) < scaleTolerence) 
    {
        return;
    }
    
    for (int i = 0; i < jointMatrices.size(); i++)
    {
        jointMatrices[i].translation() *= scale;
    }
}

void ScaleJointMatrices(std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices, Eigen::Matrix<float, -1, -1, Eigen::RowMajor>& jointMatricesJacobian, float scale)
{
    if (std::abs(scale - 1.0f) < scaleTolerence) {
        return;
    }

    for (auto& mat : jointMatrices)
    {
        mat.translation() *= scale;
    }

    const int numJoints = static_cast<int>(jointMatrices.size());
    for (int jointIndex = 0; jointIndex < numJoints; ++jointIndex)
    {
        const int baseRow = jointIndex * 12;
        if (jointMatricesJacobian.rows() >= baseRow + 12)
        {
            jointMatricesJacobian.row(baseRow + 9) *= scale;
            jointMatricesJacobian.row(baseRow + 10) *= scale;
            jointMatricesJacobian.row(baseRow + 11) *= scale;
        }
    }
};

// Scale joint matrices with scale as an optimizable parameter.
// d(s*t)/ds = t (translation), d(R)/ds = 0 (rotation)
void ScaleJointMatricesDiff(
    std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices,
    Eigen::Matrix<float, -1, -1, Eigen::RowMajor>& jointMatricesJacobian,
    const DiffData<float>& scaleDiff)
{
    const float s = scaleDiff.Value()[0];

    // Expand Jacobian columns if scale parameter is beyond current column count
    int scaleCol = -1;
    if (scaleDiff.HasJacobian())
    {
        scaleCol = scaleDiff.Jacobian().StartCol();
        const int requiredCols = scaleCol + 1;
        if (jointMatricesJacobian.cols() < requiredCols)
        {
            jointMatricesJacobian.conservativeResize(Eigen::NoChange, requiredCols);
            jointMatricesJacobian.rightCols(requiredCols - jointMatricesJacobian.cols() + (requiredCols - jointMatricesJacobian.cols())).setZero();
        }
    }

    const int numJoints = static_cast<int>(jointMatrices.size());
    for (int jointIndex = 0; jointIndex < numJoints; ++jointIndex)
    {
        const int baseRow = jointIndex * 12;

        // d(s*t)/ds = t (before scaling, so use current translation)
        if (scaleCol >= 0 && jointMatricesJacobian.rows() >= baseRow + 12)
        {
            const auto& t = jointMatrices[jointIndex].translation();
            jointMatricesJacobian(baseRow + 9, scaleCol) = t.x();
            jointMatricesJacobian(baseRow + 10, scaleCol) = t.y();
            jointMatricesJacobian(baseRow + 11, scaleCol) = t.z();
        }

        // Scale translation
        jointMatrices[jointIndex].translation() *= s;

        // Scale existing Jacobian rows for translation: d(s*t)/d[params] = s * dt/d[params]
        if (jointMatricesJacobian.rows() >= baseRow + 12)
        {
            // Scale only the pose-parameter columns (not the scale column we just set)
            const int poseCols = scaleCol >= 0 ? scaleCol : static_cast<int>(jointMatricesJacobian.cols());
            jointMatricesJacobian.block(baseRow + 9, 0, 3, poseCols) *= s;
        }
    }
}

} // namespace

Camera<float> BodyShapeEditor::ComputeCameraFromLandmarks(const std::vector<Eigen::Vector3f>& positions, int imageWidth, int imageHeight)
{
    return ComputeCameraFromLandmarksEx(positions, imageWidth, imageHeight).camera;
}

BodyShapeEditor::FacePlaneResult BodyShapeEditor::ComputeCameraFromLandmarksEx(const std::vector<Eigen::Vector3f>& positions, int imageWidth, int imageHeight)
{
    FacePlaneResult result;
    result.positions = positions;

    if (positions.size() < 3)
        return result;

    for (const auto& p : positions) result.centroid += p;
    result.centroid /= static_cast<float>(positions.size());

    Eigen::Matrix3f cov = Eigen::Matrix3f::Zero();
    for (const auto& p : positions)
    {
        Eigen::Vector3f d = p - result.centroid;
        cov += d * d.transpose();
    }
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3f> solver(cov);
    result.normal = solver.eigenvectors().col(0);
    if (result.normal.z() < 0) result.normal = -result.normal;

    for (const auto& p : positions)
        result.radius = std::max(result.radius, (p - result.centroid).norm());

    const float fovY = 30.0f * 3.14159265f / 180.0f;
    const float distance = (result.radius * 1.5f) / std::tan(fovY * 0.5f);

    Eigen::Vector3f camPos = result.centroid + result.normal * distance;
    Eigen::Vector3f forward = (result.centroid - camPos).normalized();
    Eigen::Vector3f worldUp(0, 1, 0);
    Eigen::Vector3f right = forward.cross(worldUp).normalized();
    if (right.norm() < 0.001f) { worldUp = Eigen::Vector3f(0, 0, 1); right = forward.cross(worldUp).normalized(); }
    Eigen::Vector3f up = right.cross(forward).normalized();

    Eigen::Matrix3f R;
    R.row(0) = right;
    R.row(1) = -up;
    R.row(2) = forward;

    Affine<float, 3, 3> extrinsics;
    extrinsics.SetLinear(R);
    extrinsics.SetTranslation(-R * camPos);

    result.camera.SetExtrinsics(extrinsics);
    const float fx = static_cast<float>(imageHeight) / (2.0f * std::tan(fovY * 0.5f));
    result.camera.SetIntrinsics(Eigen::Matrix3f{{fx, 0, imageWidth * 0.5f}, {0, fx, imageHeight * 0.5f}, {0, 0, 1}});
    result.camera.SetWidth(imageWidth);
    result.camera.SetHeight(imageHeight);
    result.valid = true;
    return result;
}

struct BodyShapeEditor::State::Private
{
    Eigen::VectorXf RawControls;
    Eigen::Matrix<float, 3, -1> VertexDeltas;
    Eigen::Matrix<float, 3, -1> BodySeamDelta;
    Eigen::Matrix<float, 3, -1> JointDeltas;
    float VertexDeltaScale { 1.0f };

    Eigen::VectorXf GuiControls;
    std::vector<Mesh<float>> RigMeshes;
    std::vector<Eigen::Transform<float, 3, Eigen::Affine>> JointBindMatrices;
    std::vector<BodyMeasurement> Constraints;
    Eigen::VectorXf ConstraintMeasurements;
    //! user specified target measurements
    std::vector<std::pair<int, float>> TargetMeasurements;
    bool UseSymmetry = true;
    float SemanticWeight = 10.0f;
    bool FloorOffsetApplied = true;
    std::string ModelVersion;
    bool EvaluatePose = false;

    // tmp
    //! gui controls prior (e.g. from blending or from template2MH)
    Eigen::VectorXf GuiControlsPrior;
    SparseMatrix<float> CustomSkinning;

    //! locked control indices (controls that should not be modified during solving)
    std::vector<int> LockedControlIndices;

    //! world matrices from last evaluation
    std::vector<Eigen::Transform<float, 3, Eigen::Affine>> WorldMatrices;

    //! face state for joint face/body solve
    std::optional<PatchBlendModel<float>::State> faceState;

    //! face blend amount (0 = no face deformation, 1 = full face deformation)
    float faceBlend = 1.0f;

    //! pose blend amount (0 = full pose, 1 = rest pose)
    float poseBlend = 0.0f;

    //! seam blend amount (0 = ignore BodySeamDelta, 1 = full seam correction).
    //! Not serialized — always starts at 1.0 on load, same as poseBlend.
    float seamBlend = 1.0f;

    float ScaleFactor = 1.0f;
};

BodyShapeEditor::State::State()
    : m { new Private() }
{
}

BodyShapeEditor::State::~State()
{
}

BodyShapeEditor::State::State(const State& other)
    : m(new Private(*other.m))
{
}

void BodyShapeEditor::State::SetSymmetry(const bool sym) { m->UseSymmetry = sym; }
bool BodyShapeEditor::State::GetSymmetric() const { return m->UseSymmetry; }
float BodyShapeEditor::State::GetSemanticWeight() { return m->SemanticWeight; }
void BodyShapeEditor::State::SetSemanticWeight(float weight) { m->SemanticWeight = weight; }
bool BodyShapeEditor::State::GetApplyFloorOffset() const { return m->FloorOffsetApplied; }
void BodyShapeEditor::State::SetApplyFloorOffset(bool floorOffset) { m->FloorOffsetApplied = floorOffset; }
void BodyShapeEditor::State::SetVertexInfluenceWeights(const SparseMatrix<float>& vertexInfluenceWeights) {m->CustomSkinning = vertexInfluenceWeights;}
float BodyShapeEditor::State::VertexDeltaScale() const { return m->VertexDeltaScale; }
void BodyShapeEditor::State::SetVertexDeltaScale(float VertexDeltaScale) { m->VertexDeltaScale = VertexDeltaScale; }
bool BodyShapeEditor::State::GetEvaluatePose() const { return m->EvaluatePose; }
// SetEvaluatePose is on BodyShapeEditor (it needs to re-derive VertexDeltas).

void BodyShapeEditor::State::SetGuiControls(const Eigen::VectorXf& guiControls) { m->GuiControls = guiControls; }
const Eigen::VectorXf& BodyShapeEditor::State::GetGuiControls() const { return m->GuiControls; }
const Eigen::VectorXf& BodyShapeEditor::State::GetRawControls() const { return m->RawControls; }

float BodyShapeEditor::State::GetUniformScale() const { return m->ScaleFactor; }
void BodyShapeEditor::State::SetUniformScale(float scale) { m->ScaleFactor = scale; }

void BodyShapeEditor::State::SetLockedControlIndices(const std::vector<int>& indices)
{
    m->LockedControlIndices = indices;
}

void BodyShapeEditor::State::ClearLockedControls()
{
    m->LockedControlIndices.clear();
}

const std::vector<int>& BodyShapeEditor::State::GetLockedControlIndices() const { return m->LockedControlIndices; }

PatchBlendModel<float>::State* BodyShapeEditor::State::GetFaceState() { return m->faceState ? &*m->faceState : nullptr; }
const PatchBlendModel<float>::State* BodyShapeEditor::State::GetFaceState() const { return m->faceState ? &*m->faceState : nullptr; }

float BodyShapeEditor::State::GetFaceBlend() const { return m->faceBlend; }
void BodyShapeEditor::State::SetFaceBlend(float blend) { m->faceBlend = std::clamp(blend, 0.0f, 1.0f); }

float BodyShapeEditor::State::GetPoseBlend() const { return m->poseBlend; }
void BodyShapeEditor::State::SetPoseBlend(float blend) { m->poseBlend = std::clamp(blend, 0.0f, 1.0f); }

float BodyShapeEditor::State::GetSeamBlend() const { return m->seamBlend; }
void BodyShapeEditor::State::SetSeamBlend(float blend) { m->seamBlend = std::clamp(blend, 0.0f, 1.0f); }

const Eigen::Matrix<float, 3, -1>& BodyShapeEditor::State::GetVertexDeltas() const { return m->VertexDeltas; }
void BodyShapeEditor::State::SetVertexDeltas(const Eigen::Matrix<float, 3, -1>& vertexDeltas) { m->VertexDeltas = vertexDeltas; }
const Eigen::Matrix<float, 3, -1>& BodyShapeEditor::State::GetBodySeamDelta() const { return m->BodySeamDelta; }
void BodyShapeEditor::State::SetBodySeamDelta(const Eigen::Matrix<float, 3, -1>& delta) { m->BodySeamDelta = delta; }

const Mesh<float>& BodyShapeEditor::State::GetMesh(int lod) const
{
    return m->RigMeshes[static_cast<size_t>(lod)];
}

const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& BodyShapeEditor::State::GetJointBindMatrices() const
{
    return m->JointBindMatrices;
}

const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& BodyShapeEditor::State::GetWorldMatrices() const
{
    return m->WorldMatrices;
}

const Eigen::VectorXf& BodyShapeEditor::State::GetNamedConstraintMeasurements() const
{
    if (m->ConstraintMeasurements.size() == 0)
    {
        m->ConstraintMeasurements = BodyMeasurement::GetBodyMeasurements(m->Constraints, m->RigMeshes[0].Vertices(), m->RawControls);
    }
    
    return m->ConstraintMeasurements;
}

Eigen::Matrix3Xf BodyShapeEditor::State::GetContourVertices(int ConstraintIndex) const
{
    if (m->Constraints.size() <= ConstraintIndex)
    {
        CARBON_CRITICAL("Invalid ConstraintIndex");
    }
    return m->Constraints[ConstraintIndex].GetMeasurementPoints();
}

Eigen::Matrix3Xf BodyShapeEditor::State::GetContourDebugVertices(int ConstraintIndex) const
{
    if (m->Constraints.size() <= ConstraintIndex)
    {
        CARBON_CRITICAL("Invalid ConstraintIndex");
    }
    return m->Constraints[ConstraintIndex].GetMeasurementDebugPoints(m->RigMeshes[0].Vertices());
}

void BodyShapeEditor::State::Reset()
{
    m->RawControls.setZero();
    m->VertexDeltas.setZero();
    m->BodySeamDelta.resize(0, 0);
    m->GuiControls.setZero();
    m->TargetMeasurements.clear();
    m->VertexDeltaScale = 1.0f;
    m->GuiControlsPrior.setZero();
    m->faceState.reset();
}

int BodyShapeEditor::State::GetConstraintNum() const
{
    return static_cast<int>(m->Constraints.size());
}

const std::string& BodyShapeEditor::State::GetConstraintName(int ConstraintIndex) const
{
    if (m->Constraints.size() <= ConstraintIndex)
    {
        CARBON_CRITICAL("Invalid ConstraintIndex");
    }
    return m->Constraints[ConstraintIndex].GetName();
}

bool BodyShapeEditor::State::GetConstraintTarget(int ConstraintIndex, float& OutTarget) const
{
    auto it = std::find_if(m->TargetMeasurements.begin(), m->TargetMeasurements.end(),
        [ConstraintIndex](const std::pair<int, float>& el)
        {
            return el.first == ConstraintIndex;
        });

    if (it != m->TargetMeasurements.end())
    {
        OutTarget = it->second;
        if (ConstraintIndex < m->Constraints.size() && m->Constraints[ConstraintIndex].GetType() != BodyMeasurement::Semantic && m->ScaleFactor != 0.f)
        {
            OutTarget = it->second * m->ScaleFactor;
        }
        return true;
    }

    return false;
}

void BodyShapeEditor::State::SetConstraintTarget(int ConstraintIndex, float Target)
{
    if (m->Constraints.size() <= ConstraintIndex)
    {
        CARBON_CRITICAL("Invalid ConstraintIndex");
    }

    // Scale constraint target back to model space if not semantic
    float scaledTarget = Target;
    if (ConstraintIndex < m->Constraints.size() && m->Constraints[ConstraintIndex].GetType() != BodyMeasurement::Semantic && m->ScaleFactor != 0.f)
    {
        scaledTarget = Target / m->ScaleFactor;
    }

    std::pair<int, float> TargetMeasurement { ConstraintIndex, scaledTarget };
    auto it = std::lower_bound(m->TargetMeasurements.begin(), m->TargetMeasurements.end(), TargetMeasurement,
        [](const std::pair<int, float>& elA, const std::pair<int, float>& elB)
        {
            return elA.first < elB.first;
        });

    if (it != m->TargetMeasurements.end())
    {
        if (it->first == ConstraintIndex)
        {
            it->second = scaledTarget;
            return;
        }
    }
    m->TargetMeasurements.insert(it, TargetMeasurement);
}

void BodyShapeEditor::State::RemoveConstraintTarget(int ConstraintIndex)
{
    if (m->Constraints.size() <= ConstraintIndex)
    {
        CARBON_CRITICAL("Invalid ConstraintIndex");
    }
    auto it = std::find_if(m->TargetMeasurements.begin(), m->TargetMeasurements.end(),
        [ConstraintIndex](const std::pair<int, float>& el)
        {
            return el.first == ConstraintIndex;
        });

    if (it != m->TargetMeasurements.end())
    {
        m->TargetMeasurements.erase(it);
    }
}

struct BodyShapeEditor::Private
{
    std::unique_ptr<SymmetricControls<float>> symControls;
    std::shared_ptr<BodyLogic<float>> poseLogic;
    int numBaseRawControls = 0;
    int numBaseGuiControls = 0;
    std::shared_ptr<RBFLogic<float>> rbfLogic;
    std::shared_ptr<TwistSwingLogic<float>> twistSwingLogic;
    std::shared_ptr<BodyGeometry<float>> rigGeometry;
    std::shared_ptr<BodyGeometry<float>> combinedBodyArchetypeRigGeometry;
    std::vector<Eigen::VectorX<std::uint16_t>> jointGroupInputIndices;
    std::vector<Eigen::VectorX<std::uint16_t>> jointGroupOutputIndices;
    std::vector<Eigen::VectorX<std::uint16_t>> jointGroupLODs;
    std::string ModelVersion;
    std::vector<BodyMeasurement> Constraints;
    std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> solveSteps;
    std::vector<int> localIndices;
    std::vector<int> localShapeIndices;
    std::vector<int> localSkeletonIndices;
    std::vector<int> globalIndices;
    std::vector<int> poseIndices;
    std::vector<int> rawLocalIndices;
    std::vector<int> rawPoseIndices;
    std::vector<std::vector<int>> bodyToCombinedMapping;
    std::vector<std::map<int, int>> combinedToBodyMapping;
    std::shared_ptr<const LodGeneration<float>> combinedLodGenerationData;
    std::vector<Eigen::Matrix<int, 3, -1>> meshTriangles;
    SparseMatrix<float> gwm;
    ObjectPool<BodyGeometry<float>::State> StatePool;
    ObjectPool<BodyGeometry<float>::State> StatePoolJacobian;
    std::shared_ptr<Mesh<float>> triTopology;
    std::shared_ptr<HalfEdgeMesh<float>> heTopology;
    std::shared_ptr<TaskThreadPool> threadPool;
    std::vector<float> minMeasurementInput;
    std::vector<float> maxMeasurementInput;
	std::vector<std::pair<float, float>> variableMinMeasurementInput;
	std::vector<std::pair<float, float>> variableMaxMeasurementInput;
	int heightConstraintIndex = -1;

    std::vector<int> combinedFittingIndices;
    std::vector<std::vector<int>> neckSeamIndices;
    std::vector<BodyShapeEditor::Keypoint> keypoints;
    std::vector<int> fitToTargetIndices;

    std::map<std::string, SparseMatrixPCA> skinningModels; 

    SparseMatrixPCA rbfPCA;
    SparseMatrixPCA skinWeightsPCA;
    //! region names
    std::vector<std::string> regionNames;

    //! map of skeleton pca region to affectedJoints
    std::map<std::string, std::set<int>> regionToJoints;
    //! map of skeleton pca region to raw controls
    std::map<std::string, std::vector<int>> skeletonPcaControls;
    //! map of shape pca region to raw controls
    std::map<std::string, std::vector<int>> shapePcaControls;
    //! symmetric mapping of pca regions
    std::map<std::string, std::string> symmetricPartMapping;
    //! mapping from raw to gui controls
    std::vector<int> rawToGuiControls;
    //! mapping from gui to raw controls
    std::vector<int> guiToRawControls;
    //! linear matrix mapping gui to raw controls: rawControls = guiToRawMapping * guiControls
    Eigen::SparseMatrix<float, Eigen::RowMajor> guiToRawMappingMatrix;
    // Eigen::MatrixXf guiToRawMappingMatrix;
    //! matrix to solve from raw to global gui controls
    Eigen::MatrixX<float> rawToGlobalGuiControlsSolveMatrix;
    //! vertex mask for each pca part
    std::map<std::string, VertexWeights<float>> partWeights;

    //! Optional body-blend mask (alpha = 1 pure body, 0 pure face, soft ramp across seam).
    //! Consumed by AdaptNeckSeam to set pinned ring-band boundary values.
    VertexWeights<float> bodyBlendMask;

    //! identity vertex evaluation matrix from raw controls
    Eigen::SparseMatrix<float, Eigen::RowMajor> identityVertexEvaluationMatrix;
    //! identity joint evlauation matrix from raw controls
    Eigen::SparseMatrix<float, Eigen::RowMajor> identityJointEvaluationMatrix;
    //! identity vertex evaluation matrix from symmetric controls
    Eigen::SparseMatrix<float, Eigen::RowMajor> symmetricIdentityVertexEvaluationMatrix;

    BodyJointEstimator jointEstimator;

    int floorIndex { -1 };
    int rbfControlOffset { 0 };

    void CalculateCombinedLods(BodyShapeEditor::State& state) const;
    std::vector<int> maxSkinWeights = { 12, 8, 8, 4 };
    std::vector<std::map<std::string, std::map<std::string, float>>> jointSkinningWeightLodPropagationMap;
    std::vector<SnapConfig<float>> skinningWeightSnapConfigs;

    //! calculate the skinning weight snap config for the specified lod
    SnapConfig<float> CalcNeckSeamSkinningWeightsSnapConfig(int lod) const;

    //! face PatchBlendModel for joint head/body solve (pre-reduced to face mesh vertices only)
    std::shared_ptr<PatchBlendModel<float>> facePatchBlendModel;
    int numFaceVertices = 0;

    public:
        static constexpr int32_t MagicNumber = 0x8c3b5f5e;
	void SetMinMaxMeasurements(const State& State);
	void ScaleMinMaxMeasurements(const State& State);
};

BodyShapeEditor::~BodyShapeEditor()
{
    delete m;
}

BodyShapeEditor::BodyShapeEditor()
    : m { new Private() }
{
}

int BodyShapeEditor::GetNumLOD0MeshVertices(bool bInCombined) const
{
	if (bInCombined)
	{
		return m->rigGeometry->GetMesh(0).NumVertices();
	}
	else
	{
		return static_cast<int>(m->bodyToCombinedMapping[0].size());
	}
}

const Eigen::Matrix<int, 3, -1>& BodyShapeEditor::GetMeshTriangles(int lod) const
{
    return m->meshTriangles[lod];
}

std::vector<std::string> BodyShapeEditor::GetPartWeightNames() const
{
    std::vector<std::string> names;
    names.reserve(m->partWeights.size());
    for (const auto& [name, weights] : m->partWeights)
    {
        names.push_back(name);
    }
    return names;
}

const VertexWeights<float>* BodyShapeEditor::GetPartWeight(const std::string& name) const
{
    auto it = m->partWeights.find(name);
    if (it != m->partWeights.end())
    {
        return &it->second;
    }
    return nullptr;
}

void BodyShapeEditor::SetBodyBlendMask(const VertexWeights<float>& mask)
{
    m->bodyBlendMask = mask;
}

const VertexWeights<float>* BodyShapeEditor::GetBodyBlendMask() const
{
    return m->bodyBlendMask.NumVertices() > 0 ? &m->bodyBlendMask : nullptr;
}

const std::vector<BodyShapeEditor::Keypoint>& BodyShapeEditor::GetKeypoints() const
{
    return m->keypoints;
}

const std::vector<int>& BodyShapeEditor::GetMaxSkinWeights() const
{
    return m->maxSkinWeights;
}

void BodyShapeEditor::SetMaxSkinWeights(const std::vector<int>& maxSkinWeights) { m->maxSkinWeights = maxSkinWeights; }


void BodyShapeEditor::SetThreadPool(const std::shared_ptr<TaskThreadPool>& threadPool) { m->threadPool = threadPool; }
int BodyShapeEditor::GetNumFaceVertices() const { return m->numFaceVertices; }

Camera<float> BodyShapeEditor::ComputeOptimalLandmarkCamera(
    const LandmarkConstraints2D<float>& landmarks,
    const Mesh<float>& targetMesh,
    const Eigen::Vector3f& targetOffset)
{
    const auto& targetLandmarks = landmarks.GetTargetLandmarks();
    if (targetLandmarks.empty())
        return Camera<float>();

    // Offset scan vertices to match where they appear in the viewport
    Eigen::Matrix<float, 3, -1> verts = targetMesh.Vertices();
    verts.colwise() += targetOffset;
    const auto& cam = targetLandmarks[0].second;
    const Affine<float, 3, 3>& extr = cam.Extrinsics();
    const Eigen::Matrix3f& K = cam.Intrinsics();
    const Eigen::Matrix3f Rinv = extr.Linear().transpose();
    const Eigen::Vector3f origin = -Rinv * extr.Translation();

    std::vector<Eigen::Vector3f> positions;
    const int vertStep = std::max(1, static_cast<int>(verts.cols()) / 2000);
    for (const auto& [instance, camera] : targetLandmarks)
    {
        const auto& pts = instance.Points();
        for (int j = 0; j < pts.cols(); ++j)
        {
            Eigen::Vector3f rayDir;
            rayDir.x() = (pts(0, j) - K(0, 2)) / K(0, 0);
            rayDir.y() = (pts(1, j) - K(1, 2)) / K(1, 1);
            rayDir.z() = 1.0f;
            Eigen::Vector3f dir = (Rinv * rayDir).normalized();

            float minPerpDist = std::numeric_limits<float>::max();
            for (int i = 0; i < verts.cols(); i += vertStep)
            {
                Eigen::Vector3f ov = verts.col(i) - origin;
                float t = ov.dot(dir);
                if (t < 0) continue;
                float perpDist = (ov - t * dir).squaredNorm();
                if (perpDist < minPerpDist) minPerpDist = perpDist;
            }
            const float threshold = minPerpDist * 100.0f + 1e-4f;
            float bestT = std::numeric_limits<float>::max();
            Eigen::Vector3f bestPos = Eigen::Vector3f::Zero();
            for (int i = 0; i < verts.cols(); i += vertStep)
            {
                Eigen::Vector3f ov = verts.col(i) - origin;
                float t = ov.dot(dir);
                if (t < 0) continue;
                float perpDist = (ov - t * dir).squaredNorm();
                if (perpDist < threshold && t < bestT) { bestT = t; bestPos = verts.col(i); }
            }
            if (bestT < std::numeric_limits<float>::max())
                positions.push_back(bestPos);
        }
    }

    LOG_INFO("ComputeOptimalLandmarkCamera: {} 3D positions raycasted onto scan", positions.size());
    return ComputeCameraFromLandmarks(positions, cam.Width(), cam.Height());
}

BodyShapeEditor::FacePlaneResult BodyShapeEditor::ComputeOptimalLandmarkCameraEx(
    const LandmarkConstraints2D<float>& landmarks,
    const Mesh<float>& targetMesh,
    const Eigen::Vector3f& targetOffset,
    const AABBTree<float>* aabbTree)
{
    const auto& targetLandmarks = landmarks.GetTargetLandmarks();
    if (targetLandmarks.empty())
        return FacePlaneResult();

    Eigen::Matrix<float, 3, -1> verts = targetMesh.Vertices();
    verts.colwise() += targetOffset;
    const auto& cam = targetLandmarks[0].second;
    const Affine<float, 3, 3>& extr = cam.Extrinsics();
    const Eigen::Matrix3f Rinv = extr.Linear().transpose();
    const Eigen::Vector3f origin = -Rinv * extr.Translation();

    // Use provided AABB tree or build one
    std::unique_ptr<AABBTree<float>> localTree;
    if (!aabbTree)
    {
        Mesh<float> triMesh;
        triMesh.SetVertices(verts);
        if (targetMesh.NumTriangles() > 0)
            triMesh.SetTriangles(targetMesh.Triangles());
        else if (targetMesh.NumQuads() > 0)
        {
            triMesh.SetQuads(targetMesh.Quads());
            triMesh.Triangulate();
        }
        if (triMesh.NumTriangles() == 0)
        {
            LOG_WARNING("ComputeOptimalLandmarkCameraEx: mesh has no triangles");
            return FacePlaneResult();
        }
        localTree = std::make_unique<AABBTree<float>>(triMesh.Vertices().transpose(), triMesh.Triangles().transpose());
        aabbTree = localTree.get();
    }

    const auto& instance = targetLandmarks[0].first;
    const auto& pts = instance.Points();
    const auto& meshTris = targetMesh.Triangles();
    const auto& lmConfig = instance.GetLandmarkConfiguration();

    // Raycast a 2D point to 3D via AABB
    auto raycast = [&](int j) -> std::pair<Eigen::Vector3f, bool> {
        const Eigen::Vector3f direction = cam.Unproject(pts.col(j), 1.0f, true) - origin;
        const auto [triIdx, bary, dist] = aabbTree->intersectRay(origin.transpose(), direction.transpose());
        if (triIdx >= 0)
        {
            const Eigen::Vector3i tri = meshTris.col(triIdx);
            return { bary[0] * verts.col(tri[0]) + bary[1] * verts.col(tri[1]) + bary[2] * verts.col(tri[2]), true };
        }
        return { Eigen::Vector3f::Zero(), false };
    };

    // Group landmarks into 3 regions: left eye (+brow), right eye (+brow), mouth
    // Match by: name contains keyword AND ends with correct side suffix
    auto classifyRegion = [](const std::string& name) -> std::string {
        const bool isLeft = name.size() >= 2 && name.substr(name.size() - 2) == "_l";
        const bool isRight = name.size() >= 2 && name.substr(name.size() - 2) == "_r";

        const bool isEyeOrBrow = name.find("eye") != std::string::npos || // this check includes names with eyelid in
                                  name.find("brow") != std::string::npos ||
                                  name.find("pupil") != std::string::npos ||
                                  name.find("iris") != std::string::npos;

        const bool isMouth = name.find("lip") != std::string::npos ||
                              name.find("mouth") != std::string::npos ||
                              name.find("tooth") != std::string::npos ||
                              name.find("chin") != std::string::npos;

        if (isEyeOrBrow && isLeft) return "eye_left";
        if (isEyeOrBrow && isRight) return "eye_right";
        if (isMouth) return "mouth";
        return "";
    };

    std::vector<Eigen::Vector3f> positions;

    if (lmConfig)
    {
        // Collect all 3D hits grouped by region
        std::map<std::string, std::vector<Eigen::Vector3f>> regionHits;

        std::set<std::string> unmatchedNames;

        // Process individual landmarks
        for (const auto& [name, idx] : lmConfig->LandmarkMapping())
        {
            auto [pos, hit] = raycast(idx);
            if (!hit) continue;
            std::string region = classifyRegion(name);
            if (!region.empty())
                regionHits[region].push_back(pos);
            else
                unmatchedNames.insert(name);
        }

        // Process curve points
        for (const auto& [name, indices] : lmConfig->CurvesMapping())
        {
            std::string region = classifyRegion(name);
            if (region.empty()) { unmatchedNames.insert(name); continue; }
            for (int idx : indices)
            {
                auto [pos, hit] = raycast(idx);
                if (hit) regionHits[region].push_back(pos);
            }
        }

        // Compute centroid per region
        for (const auto& [regionName, hits] : regionHits)
        {
            if (hits.empty()) continue;
            Eigen::Vector3f centroid = Eigen::Vector3f::Zero();
            for (const auto& h : hits) centroid += h;
            centroid /= static_cast<float>(hits.size());
            positions.push_back(centroid);
        }
    }
    else
    {
        // Fallback: raycast all points directly
        for (int j = 0; j < pts.cols(); ++j)
        {
            auto [pos, hit] = raycast(j);
            if (hit) positions.push_back(pos);
        }
    }

    return ComputeCameraFromLandmarksEx(positions, cam.Width(), cam.Height());
}

void BodyShapeEditor::SetFacePatchBlendModel(std::shared_ptr<PatchBlendModel<float>> facePatchModel)
{
    m->facePatchBlendModel = std::move(facePatchModel);
    // Only overwrite numFaceVertices when we have a face PCA model to source
    // it from. Setting it to 0 here would wipe the Init-time fallback (which
    // derives it from combined - body vertex counts) and break AdaptNeckSeam.
    if (m->facePatchBlendModel)
        m->numFaceVertices = m->facePatchBlendModel->NumVertices();
}

Eigen::Vector3f BodyShapeEditor::GetModelMeshExtents() const
{
    Eigen::Vector3f ModelMeshExtents(100.f, 170.f, 40.f);
    if (m->rigGeometry) 
    {
        const Eigen::Matrix3Xf& mv = m->rigGeometry->GetMesh(0).Vertices();
        ModelMeshExtents = mv.rowwise().maxCoeff() - mv.rowwise().minCoeff();
    }
    return ModelMeshExtents;
}

void BodyShapeEditor::Private::CalculateCombinedLods(BodyShapeEditor::State& state) const
{
    if (combinedLodGenerationData)
    {
        std::map<std::string, Eigen::Matrix<float, 3, -1>> lod0Vertices, higherLodVertices;
        const auto baseMeshes = combinedLodGenerationData->Lod0MeshNames();
        if (baseMeshes.size() != 1)
        {
            CARBON_CRITICAL("There should be 1 lod 0 mesh for the combined body model");
        }
        lod0Vertices[baseMeshes[0]] = state.m->RigMeshes[0].Vertices();

        bool bCalculatedLods = combinedLodGenerationData->Apply(lod0Vertices, higherLodVertices);
        if (!bCalculatedLods)
        {
            CARBON_CRITICAL("Failed to generate lods for the combined body model");
        }
        for (const auto& lodVertices : higherLodVertices)
        {
            int lod = combinedLodGenerationData->LodForMesh(lodVertices.first);
            state.m->RigMeshes[static_cast<size_t>(lod)].SetVertices(lodVertices.second);
            state.m->RigMeshes[static_cast<size_t>(lod)].CalculateVertexNormals(true, VertexNormalComputationType::AreaWeighted, /*stableNormalize=*/true);
        }
    }
}

std::shared_ptr<BodyShapeEditor::State> BodyShapeEditor::CreateState() const
{
    auto state = std::shared_ptr<State>(new State());
    state->m->GuiControls = Eigen::VectorX<float>::Zero(m->poseLogic->NumGUIControls());
    state->m->Constraints = m->Constraints;
    state->m->JointBindMatrices = m->rigGeometry->GetBindMatrices();
    state->m->JointDeltas = Eigen::Matrix<float, 3, -1>::Zero(3,m->rigGeometry->NumJoints());
    state->m->VertexDeltas = Eigen::Matrix<float,3 , -1>::Zero(3, m->rigGeometry->GetMesh(0).Vertices().cols());
    state->m->ModelVersion = m->ModelVersion;

    if (m->facePatchBlendModel)
    {
        state->m->faceState = m->facePatchBlendModel->CreateState();
    }

    EvaluateState(*state);
    return state;
}

void BodyShapeEditor::EvaluateState(State& State) const
{
    // Default entry point: read both axes off the state.
    EvaluateState(State, CalcStrategy::Auto, State.m->EvaluatePose);
}

void BodyShapeEditor::EvaluateState(State& State, CalcStrategy strategy, bool isEvaluatingPose) const
{
    // Guard the downstream RigMeshes[0] accesses — if the BSE pimpl has no
    // mesh topology (e.g. model not yet loaded / corrupted state), bail with a
    // warning instead of crashing with a vector-out-of-bounds inside the
    // std::vector::operator[] call on line ~2005.
    if (m->meshTriangles.empty())
    {
        LOG_WARNING("EvaluateState: meshTriangles empty (model not loaded?) — skipping");
        return;
    }
    State.m->CustomSkinning.resize(0, 0);
    Eigen::VectorXf GuiControls = State.m->GuiControls;
    // useNonlinearPath: which shape evaluator to run (skeletal vs linear).
    // Auto pairs it with isEvaluatingPose (legacy behavior); explicit Linear
    // / Nonlinear force the path regardless of pose, so callers can mix —
    // e.g. Nonlinear + isEvaluatingPose=false is "skeletal shape eval, no
    // pose values applied".
    const bool useNonlinearPath =
        (strategy == CalcStrategy::Auto)      ? isEvaluatingPose
      : (strategy == CalcStrategy::Nonlinear) ? true
                                              : false;
    if (!isEvaluatingPose)
    {
        const auto& names = m->poseLogic->GuiControlNames();
        for (int i = 0; i < (int)names.size(); ++i)
        {
            if (names[i].starts_with("pose_driver") || names[i].starts_with("pose_rigid_pelvis"))
            {
                GuiControls[i] = 0.0f;
            }
        }
    }
    else if (State.m->poseBlend > 0.0f)
    {
        // Blend pose toward rest (0 = full pose, 1 = rest pose)
        const float factor = 1.0f - State.m->poseBlend;
        for (int poseIndex : m->poseIndices)
        {
            if (poseIndex < GuiControls.size())
            {
                GuiControls[poseIndex] *= factor;
            }
        }
    }
    State.m->RawControls = m->poseLogic->EvaluateRawControls(GuiControls).Value();
    Eigen::Matrix3Xf vertices;

    // Compute face offset if face PatchBlendModel is present and has state
    Eigen::Matrix<float, 3, -1> faceOffset;
    bool hasFaceOffset = false;
    if (m->facePatchBlendModel && State.m->faceState)
    {
        Eigen::Matrix<float, 3, -1> faceVertices = m->facePatchBlendModel->DeformedVertices(*State.m->faceState);
        const Eigen::Matrix<float, 3, -1>& baseVertices = m->facePatchBlendModel->BaseVertices();
        faceOffset = faceVertices - baseVertices;
        hasFaceOffset = true;
    }

    if (useNonlinearPath)
    {
        // evaluate using riglogic when poses are activated
        BodyGeometry<float>::State geometryState;
        const DiffData<float> joints = m->poseLogic->EvaluateJoints(0, State.m->RawControls);
        const DiffData<float> rbfPsd = m->rbfLogic->EvaluatePoseControlsFromJoints(joints, true);
        const DiffData<float> rbfJoints = m->poseLogic->EvaluateRbfJoints(0, rbfPsd);
        const DiffData<float> twistedJoints = m->twistSwingLogic->EvaluateJointsFromJoints(joints + rbfJoints);

        // Build combined offset from vertex deltas and face offset
        const int totalVertices = m->rigGeometry->GetMesh(0).NumVertices();
        Eigen::Matrix<float, 3, -1> fullOffset = Eigen::Matrix<float, 3, -1>::Zero(3, totalVertices);

        if (State.m->VertexDeltas.size() > 0)
        {
            fullOffset = State.m->VertexDeltaScale * State.m->VertexDeltas;
        }
        if (State.m->BodySeamDelta.cols() == totalVertices && State.m->faceBlend > 0.0f && State.m->seamBlend > 0.0f)
        {
            fullOffset += (State.m->faceBlend * State.m->seamBlend) * State.m->BodySeamDelta;
        }
        if (hasFaceOffset && State.m->faceBlend > 0.0f)
        {
            fullOffset.leftCols(m->numFaceVertices) += State.m->faceBlend * faceOffset;
        }

        if (fullOffset.squaredNorm() > 0)
        {
            m->rigGeometry->EvaluateBodyGeometryWithOffset(0, fullOffset, twistedJoints, State.m->RawControls, geometryState);
        }
        else
        {
            m->rigGeometry->EvaluateBodyGeometry(0, twistedJoints, State.m->RawControls, geometryState);
        }
        vertices = geometryState.Vertices().Matrix();
        State.m->WorldMatrices = geometryState.GetWorldMatrices();
    }
    else
    {
        // use linear matrix for activation
        const int numVertices = m->rigGeometry->GetMesh(0).NumVertices();
        Eigen::VectorXf rawLocalControls = State.m->RawControls(m->rawLocalIndices);
        if (m->threadPool)
        {
            vertices.resize(3, numVertices);
            ParallelNoAliasGEMV<float>(vertices.reshaped(), m->identityVertexEvaluationMatrix, rawLocalControls, m->threadPool.get());
            if (((int)State.m->VertexDeltas.cols() == m->rigGeometry->GetMesh(0).NumVertices()) && (State.m->VertexDeltaScale > 0))
            {
                vertices += m->rigGeometry->GetMesh(0).Vertices() + State.m->VertexDeltaScale * State.m->VertexDeltas;
            }
            else
            {
                vertices += m->rigGeometry->GetMesh(0).Vertices();
            }
        }
        else
        {
            vertices = (m->identityVertexEvaluationMatrix * rawLocalControls + m->rigGeometry->GetMesh(0).Vertices().reshaped()).reshaped(3, numVertices);
            if (((int)State.m->VertexDeltas.cols() == m->rigGeometry->GetMesh(0).NumVertices()) && (State.m->VertexDeltaScale > 0))
            {
                vertices += State.m->VertexDeltaScale * State.m->VertexDeltas;
            }
        }
        // Add face offset for non-pose case
        if (hasFaceOffset && State.m->faceBlend > 0.0f)
        {
            vertices.leftCols(m->numFaceVertices) += State.m->faceBlend * faceOffset;
        }
        if (State.m->BodySeamDelta.cols() == numVertices && State.m->faceBlend > 0.0f && State.m->seamBlend > 0.0f)
        {
            vertices += (State.m->faceBlend * State.m->seamBlend) * State.m->BodySeamDelta;
        }

        // Apply the root joint pose (translation + rotation) to all vertices.
        // Pelvis rotation is zeroed at the top of EvaluateState, so no
        // pelvis-pivot math is needed here. dof layout per joint is
        // [tx ty tz | rx ry rz | sx sy sz] (dofPerJoint = 9 in BodyGeometry).
        const DiffData<float> jointsForRoot = m->poseLogic->EvaluateJoints(0, State.m->RawControls);
        const auto& jv = jointsForRoot.Value();
        const Eigen::Matrix3f rootRotation    = EulerXYZ(jv[3], jv[4], jv[5]);
        const Eigen::Vector3f rootTranslation = jv.segment<3>(0);
        vertices = vertices.colwise() + rootTranslation;
    }
    // Derive the root joint offset from evaluating joints
    const DiffData<float> joints = m->poseLogic->EvaluateJoints(0, State.m->RawControls);
    Eigen::Vector3f rootJointOffset = joints.Value().segment<3>(0);

    Eigen::VectorXf jointDeltas = m->identityJointEvaluationMatrix *  State.m->RawControls(m->rawLocalIndices);

    for(int ji : m->jointEstimator.CoreJoints())
    {
        State.m->JointBindMatrices[ji].translation() = jointDeltas.segment(3 * ji, 3) + m->rigGeometry->GetBindMatrices()[ji].translation();
        if (State.m->JointDeltas.cols() > 0)
        {
            State.m->JointBindMatrices[ji].translation() += State.m->VertexDeltaScale * State.m->JointDeltas.col(ji);
        }
    }

    // Apply the root joint offset to core joints before computing dependent joints
    // This way dependent joints will inherit the offset through the joint-joint matrix
    for(int ji : m->jointEstimator.CoreJoints())
    {
        State.m->JointBindMatrices[ji].translation() += rootJointOffset;
    }

    Eigen::Matrix<float, 3, -1> jointBindPose;
    jointBindPose.resize(3, State.m->JointBindMatrices.size());
    for(int ji = 0; ji < jointBindPose.cols(); ji++)
    {
        jointBindPose.col(ji) = State.m->JointBindMatrices[ji].translation();
    }

    const auto& vjm = m->jointEstimator.VertexJointMatrix();
    const auto& jjm = m->jointEstimator.JointJointMatrix();
    const auto dependentJoints = (jointBindPose * jjm).eval();
    for(int ji : m->jointEstimator.DependentJoints())
    {
        State.m->JointBindMatrices[ji].translation() = dependentJoints.col(ji);
    }
    const auto surfaceJoints = (vertices * vjm).eval();
    for(int ji : m->jointEstimator.SurfaceJoints())
    {
        State.m->JointBindMatrices[ji].translation() = surfaceJoints.col(ji) + State.m->VertexDeltaScale * State.m->JointDeltas.col(ji);
    }
    if (!isEvaluatingPose)
    {
        State.m->WorldMatrices = State.m->JointBindMatrices;
    }
    
    vertices = ScaleVertices(vertices, State.m->ScaleFactor);
    
    ScaleJointMatrices( State.m->JointBindMatrices, State.m->ScaleFactor);
    ScaleJointMatrices( State.m->WorldMatrices, State.m->ScaleFactor);

    if (State.m->FloorOffsetApplied)
    {
        // get floor position (using index or lowest vertex in the mesh) and move vertices and joints
        float floorOffset = 0;
        if (m->floorIndex >= 0)
        {
            floorOffset = vertices.row(1)[m->floorIndex];
        }
        else
        {
            floorOffset = vertices.row(1).minCoeff();
        }
        vertices.row(1).array() -= floorOffset;

        Eigen::Vector3f offsetTranslation(0.0f, floorOffset, 0.0f);
        for (int i = 1; i < (int)State.m->JointBindMatrices.size(); i++)
        {
            State.m->JointBindMatrices[i].translation() -= offsetTranslation;
            State.m->WorldMatrices[i].translation() -= offsetTranslation;
        }
    }
    State.m->JointBindMatrices[0].translation() = Eigen::Vector3f::Zero();
    State.m->WorldMatrices[0].translation() = Eigen::Vector3f::Zero();
    // make sure the rig meshes have the right triangulation
    State.m->RigMeshes.resize(m->meshTriangles.size());
    for (size_t i = 0; i < m->meshTriangles.size(); ++i)
    {
        if (State.m->RigMeshes[i].NumTriangles() != (int)m->meshTriangles[i].cols())
        {
            State.m->RigMeshes[i].SetTriangles(m->meshTriangles[i]);
        }
    }
    
    // update LOD0
    State.m->RigMeshes[0].SetVertices(vertices);
    State.m->RigMeshes[0].CalculateVertexNormals(true, VertexNormalComputationType::AreaWeighted, /*stableNormalize=*/true, m->threadPool.get());

    // update other LODs
    m->CalculateCombinedLods(State);
    BodyMeasurement::UpdateBodyMeasurementPoints(State.m->Constraints, vertices, State.m->RigMeshes[0].VertexNormals(), *m->heTopology, nullptr); // m->threadPool.get());
    State.m->ConstraintMeasurements = BodyMeasurement::GetBodyMeasurements(State.m->Constraints, State.m->RigMeshes[0].Vertices(), State.m->RawControls);
}

void BodyShapeEditor::SetEvaluatePose(State& state, bool evaluatePose) const
{
    if (state.m->EvaluatePose == evaluatePose) return;

    // No deltas to preserve — just flip the flag.
    if (state.m->VertexDeltas.squaredNorm() == 0.0f)
    {
        state.m->EvaluatePose = evaluatePose;
        EvaluateState(state);
        return;
    }

    // Two pose-suppressed, floor-off, vds=1 evaluations on copies of state
    // — one under the OLD shape evaluator, one under the NEW. Both meshes
    // live in the same unfloor'd world frame, so their difference is the
    // pure shape-evaluator gap × ScaleFactor. Divide by ScaleFactor·vds and
    // add to VertexDeltas: the /vds compensates for the multiplication that
    // happens at eval time, so the correction lands invariant to state's
    // slider position.
    State captureCopy(state);
    captureCopy.m->VertexDeltaScale   = 1.0f;
    captureCopy.m->faceBlend          = 1.0f;
    captureCopy.m->seamBlend          = 1.0f;
    captureCopy.m->FloorOffsetApplied = false;

    State baselineCopy(state);
    baselineCopy.m->VertexDeltaScale   = 1.0f;
    baselineCopy.m->faceBlend          = 1.0f;
    baselineCopy.m->seamBlend          = 1.0f;
    baselineCopy.m->FloorOffsetApplied = false;
    baselineCopy.m->EvaluatePose       = evaluatePose;

    const CalcStrategy oldStrategy = state.m->EvaluatePose ? CalcStrategy::Nonlinear : CalcStrategy::Linear;
    const CalcStrategy newStrategy = evaluatePose          ? CalcStrategy::Nonlinear : CalcStrategy::Linear;

    EvaluateState(captureCopy,  oldStrategy, /*isEvaluatingPose=*/false);
    EvaluateState(baselineCopy, newStrategy, /*isEvaluatingPose=*/false);

    const Eigen::Matrix<float, 3, -1>& capturedMesh = captureCopy.m->RigMeshes[0].Vertices();
    const Eigen::Matrix<float, 3, -1>& baselineMesh = baselineCopy.m->RigMeshes[0].Vertices();

    const float scaleFactor = (state.m->ScaleFactor      != 0.0f) ? state.m->ScaleFactor      : 1.0f;
    const float vds         = (state.m->VertexDeltaScale != 0.0f) ? state.m->VertexDeltaScale : 1.0f;
    state.m->VertexDeltas += (capturedMesh - baselineMesh) / (scaleFactor * vds);
    state.m->EvaluatePose = evaluatePose;
    EvaluateState(state);
}

void BodyShapeEditor::UpdateGuiFromRawControls(State& state) const
{
    const Eigen::VectorXf prevRawControls = state.m->RawControls;

    state.m->GuiControls = Eigen::VectorXf::Zero(m->poseLogic->NumGUIControls());
    state.m->GuiControls(m->globalIndices) = m->rawToGlobalGuiControlsSolveMatrix * prevRawControls;
    Eigen::VectorXf newRawControls = m->poseLogic->EvaluateRawControls(state.m->GuiControls).Value();
    for (int vID = 0; vID < (int)m->rawToGuiControls.size(); ++vID)
    {
        const int guiID = m->rawToGuiControls[vID];
        if (guiID >= 0)
        {
            state.m->GuiControls[guiID] += prevRawControls[vID] - newRawControls[vID];
        }
    }
}

int BodyShapeEditor::NumLODs() const
{
    if (!m->combinedLodGenerationData)
    {
        return 1;
    }
    else
    {
        return static_cast<int>(m->combinedLodGenerationData->HigherLodMeshNames().size()) + 1;
    }
}

std::vector<int> FindMissing(int totalInputs, const std::vector<int>& selected)
{
    std::vector<bool> isSelected(totalInputs, false);

    for (int control : selected)
    {
        isSelected[control] = true;
    }

    std::vector<int> missing;
    for (int i = 0; i < totalInputs; ++i)
    {
        if (!isSelected[i])
        {
            missing.push_back(i);
        }
    }

    return missing;
}

std::vector<int> NonZeroMaskVerticesIntersection(const std::vector<int>& mapping, const std::vector<int>& mask)
{
    std::unordered_set<int> maskSet(mask.begin(), mask.end());
    std::vector<int> result;

    for (int idx : mapping)
    {
        if (maskSet.contains(idx))
        {
            result.push_back(idx);
        }
    }

    return result;
}

int ClosestIndex(int queryIndex, std::vector<int>& targetIndices, const Eigen::Matrix<float, 3, -1>& vertexPositions)
{
    float distance = 1e5f;
    int resultIndex = -1;

    const Eigen::Vector3f queryVertex = vertexPositions.col(queryIndex);
    for (int i = 0; i < (int)targetIndices.size(); ++i)
    {
        const Eigen::Vector3f targetVertex = vertexPositions.col(targetIndices[i]);
        float currentDistance = (targetVertex - queryVertex).norm();
        if (currentDistance < distance)
        {
            distance = currentDistance;
            resultIndex = targetIndices[i];
        }
    }

    return resultIndex;
}

const BodyJointEstimator& BodyShapeEditor::JointEstimator() { return m->jointEstimator; }

void BodyShapeEditor::UpdateSkinningAndRBF(const Eigen::VectorXf& rawControls, const Eigen::VectorXf& joints, std::shared_ptr<BodyGeometry<float>> poseGeometry, std::shared_ptr<BodyLogic<float>> poseLogic) const
{
    Eigen::Matrix3Xf restPoses = poseGeometry->GetJointRestPoses();
    // poseGeometry->GetVertexInfluenceWeights(0) = m->skinWeightsPCA.calculateResult(m->rawToGlobalGuiControlsSolveMatrix * rawControls);
    BodyGeometry<float>::State state;
    m->rigGeometry->EvaluateBodyGeometry(0, joints, rawControls, state);
    std::vector<Eigen::Affine3f> world = state.GetWorldMatrices();
    const SparseMatrix<float> jointVertexMatrix = m->jointEstimator.VertexJointMatrix().transpose();

    for (int jID : m->jointEstimator.SurfaceJoints())
    {
        SparseMatrix<float>::InnerIterator it(jointVertexMatrix, jID);
        const int vID = it.col();
        int parentID = m->rigGeometry->GetJointParentIndices()[jID];
        auto affine = world[jID]; 
        affine.translation() = state.Vertices().Matrix().col(vID);
        restPoses.col(jID) = (world[parentID].inverse() * affine).translation();
    }
    poseGeometry->GetJointRestPoses() = restPoses;
    poseGeometry->UpdateBindPoses();
    poseLogic->GetRbfJointMatrix(0) = m->rbfPCA.calculateResult(m->rawToGlobalGuiControlsSolveMatrix * rawControls);
}

Vector<float> BodyShapeEditor::SolveForTemplateMesh(State& State, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> TargetMesh,  const FitToTargetOptions& options, ConstArrayView<float> inJointRotations, Eigen::VectorXf vertexWeightOverride, IterationFunc iterationFunc)
{
    auto poseGeometry = m->rigGeometry->Clone();
    auto poseLogic = m->poseLogic->Clone();
    int combinedVertexCount = m->rigGeometry->GetMesh(0).Vertices().cols();
    Eigen::Matrix<float, 3, -1> TargetMeshCombined(3, combinedVertexCount);
    State.m->JointDeltas.setZero();
    TargetMeshCombined.setZero();
    const auto& ArchBindMatrices = m->rigGeometry->GetBindMatrices();
    for (int i = 0; i < m->rigGeometry->NumJoints(); ++i)
    {
        State.m->JointBindMatrices[i].linear() = ArchBindMatrices[i].linear();
    }

    // Track which combined-mesh indices actually got a target sample. For a
    // full combined-topology target every index is live; for a body-only target
    // only the body-mapped indices are — face indices stay at zero and MUST NOT
    // become keypoints, otherwise they pull the face PCA toward the origin.
    std::vector<uint8_t> hasTarget(combinedVertexCount, 0);
    if (TargetMesh.cols() != combinedVertexCount)
    {
        for (int i = 0; i < m->bodyToCombinedMapping[0].size(); ++i)
        {
            const int ci = m->bodyToCombinedMapping[0][i];
            if (ci >= 0 && ci < combinedVertexCount)
            {
                TargetMeshCombined.col(ci) = TargetMesh.col(i);
                hasTarget[ci] = 1;
            }
        }
    }
    else
    {
        TargetMeshCombined = TargetMesh;
        std::fill(hasTarget.begin(), hasTarget.end(), 1);
    }

    std::vector<std::pair<int, Eigen::Vector3f>> keypointCorrespondences;
    if (!m->fitToTargetIndices.empty())
    {
        keypointCorrespondences.reserve(m->fitToTargetIndices.size());
        for (int idx : m->fitToTargetIndices)
        {
            if (idx >= 0 && idx < combinedVertexCount && hasTarget[idx])
                keypointCorrespondences.emplace_back(idx, TargetMeshCombined.col(idx));
        }
    }
    else
    {
        keypointCorrespondences.reserve(combinedVertexCount);
        for (int i = 0; i < combinedVertexCount; ++i)
        {
            if (hasTarget[i])
                keypointCorrespondences.emplace_back(i, TargetMeshCombined.col(i));
        }
    }

    // Template-mesh fit: keypoint-only (no ICP) with heavy regularization to
    // mimic the legacy single-shot Tikhonov solve. Cap at 5 Gauss-Newton
    // iterations — more doesn't improve the linear over-determined system,
    // just burns cycles.
    BodySolveConfiguration solveConfig;
    solveConfig.body["keypoint"].Set(solveConfig.body["icp"].Value<float>()/TargetMesh.cols());
    solveConfig.body["icp"].Set(0.0f);
    solveConfig.body["iterations"].Set(5);

    State.m->GuiControls.setZero();

    BodyShapeEditorTarget solveTarget;
    solveTarget.SetMesh(BodyShapeEditorTarget::MeshSlot::Combined, std::make_shared<Mesh<float>>());
    for (const auto& [vIdx, tgt] : keypointCorrespondences)
        solveTarget.AddKeypoint(vIdx, tgt);
    solveTarget.SetJointRotations(inJointRotations);

    // Body-only target: head + upper neck have no keypoints to drive them, so
    // the pose regulariser is the only signal acting on the neck/spine_05
    // pose_driver_* controls. That tends to wobble them around chasing noise.
    // Lock neck + spine_05 driver controls so they stay at the template's
    // initial pose (consistent with face-data path which has those joints
    // pinned by the face fit).
    std::vector<int> lockedControlIndices;
    if (TargetMesh.cols() != combinedVertexCount)
    {
        const auto& names = poseLogic->GuiControlNames();
        for (int i = 0; i < (int)names.size(); ++i)
        {
            if (names[i].starts_with("pose_driver_neck") ||
                names[i].starts_with("pose_driver_spine_05"))
                lockedControlIndices.push_back(i);
        }
        LOG_INFO("SolveForTemplateMesh: body-only target — locking {} neck+spine_05 driver controls",
                 (int)lockedControlIndices.size());
    }

    Vector<float> result = SolveForArbitraryMeshWithICP(
        State,
        solveTarget,
        solveConfig,
        lockedControlIndices,
        iterationFunc
    );

    auto geometryState = m->StatePoolJacobian.Aquire();
    const DiffData<float> rawControls = poseLogic->EvaluateRawControls(result);
    const DiffData<float> joints = poseLogic->EvaluateJoints(0, rawControls);
    const DiffData<float> rbfPsd = m->rbfLogic->EvaluatePoseControlsFromJoints(joints, true);
    const DiffData<float> rbfJoints = poseLogic->EvaluateRbfJoints(0, rbfPsd);
    const DiffData<float> twistedJoints = m->twistSwingLogic->EvaluateJointsFromJoints(joints + rbfJoints);
    poseGeometry->EvaluateBodyGeometry(0, twistedJoints, rawControls, *geometryState);

    // Reset deltas so SetNeutralMesh computes them fresh against the post-
    // solve baseline. Pass the original TargetMesh — not the combined-
    // expanded version — so SetNeutralMesh routes through its body-only
    // branch for body-only inputs and leaves face VertexDeltas untouched.
    State.m->VertexDeltas = Eigen::Matrix<float, 3, -1>::Zero(3, combinedVertexCount);
    SetNeutralMesh(State, TargetMesh);

    // Body-only target: smooth the head side of the neck seam onto the
    // body's new seam value so the two halves meet continuously. Combined
    // targets already fit both halves, so skip and wipe any stale
    // BodySeamDelta from a previous body-only solve.
    if (TargetMesh.cols() != TargetMeshCombined.cols())
    {
        AdaptNeckSeam(State, /*laplacianWeight=*/1.5f, /*rings=*/12, /*iterations=*/15, SeamLockSide::Body);
    }
    else
    {
        State.m->BodySeamDelta.setZero();
    }

    EvaluateState(State);

    return result;
}

void BodyShapeEditor::AdaptNeckSeam(State& state, float laplacianWeight, int rings, int iterations,
                                    SeamLockSide lockSide)
{
    Timer totalTimer;
    const int numSeamVerts = m->neckSeamIndices.empty() ? 0 : (int)m->neckSeamIndices[0].size();
    const char* lockName = (lockSide == SeamLockSide::None) ? "none"
                         : (lockSide == SeamLockSide::Body) ? "body"
                         : "face";
    LOG_INFO("AdaptNeckSeam: start — rings={}, iterations={}, laplacianWeight={}, neckSeamVerts={}, lock={}",
             rings, iterations, laplacianWeight, numSeamVerts, lockName);
    if (rings <= 0 || iterations <= 0 || numSeamVerts == 0)
    {
        LOG_INFO("AdaptNeckSeam: skipped (rings<=0 or iters<=0 or no seam)");
        return;
    }
    // AdaptNeckSeam is intentionally independent of the face PCA state.
    // We never deform the face here — the face side acts as fixed
    // boundary conditions for the Laplacian, treated as zero offset.

    const int numCombined = static_cast<int>(m->rigGeometry->GetMesh(0).Vertices().cols());
    const int numFace     = m->numFaceVertices;

    // Build vertex adjacency as a flat CSR-style structure — one pass, no sets
    // per vertex (std::unordered_set<int> per vert on a 200k-vert mesh is the
    // reason the previous version was slow).
    Timer adjTimer;
    std::vector<int> adjOffsets(numCombined + 1, 0);
    const auto& tris = m->meshTriangles[0];
    for (int t = 0; t < tris.cols(); ++t)
        for (int k = 0; k < 3; ++k) adjOffsets[tris(k, t) + 1] += 2;
    for (int i = 0; i < numCombined; ++i) adjOffsets[i + 1] += adjOffsets[i];
    std::vector<int> adj(adjOffsets.back());
    std::vector<int> cursor(numCombined, 0);
    for (int t = 0; t < tris.cols(); ++t)
    {
        const int a = tris(0, t), b = tris(1, t), c = tris(2, t);
        adj[adjOffsets[a] + cursor[a]++] = b;
        adj[adjOffsets[a] + cursor[a]++] = c;
        adj[adjOffsets[b] + cursor[b]++] = a;
        adj[adjOffsets[b] + cursor[b]++] = c;
        adj[adjOffsets[c] + cursor[c]++] = a;
        adj[adjOffsets[c] + cursor[c]++] = b;
    }
    auto adjBegin = [&](int v) { return adj.data() + adjOffsets[v]; };
    auto adjEnd   = [&](int v) { return adj.data() + adjOffsets[v + 1]; };
    LOG_INFO("AdaptNeckSeam: adjacency built ({} verts, {} edges) in {} ms",
             numCombined, (int)adj.size() / 2, adjTimer.Current());

    // BFS `rings` hops outward from the seam loop — no face/body filter, so the
    // band expands into both face and body. Seam verts themselves are NOT part
    // of the free ring band — they're pinned at the face-solve offset below,
    // acting as the inner Dirichlet boundary the Laplacian propagates from.
    Timer bfsTimer;
    std::vector<char> isSeam(numCombined, 0);   // pinned inner boundary
    std::vector<char> visited(numCombined, 0);  // BFS visit flag (seed + band)
    std::vector<char> inRing(numCombined, 0);   // free-region membership
    std::vector<int>  ringBand;                 // ordered list for fast iteration
    ringBand.reserve(1024);
    std::vector<int> frontier;
    for (int idx : m->neckSeamIndices[0])
        if (idx >= 0 && idx < numCombined && !visited[idx])
        {
            isSeam[idx]  = 1;
            visited[idx] = 1;
            frontier.push_back(idx);
        }
    if (frontier.empty()) return;
    for (int r = 0; r < rings; ++r)
    {
        std::vector<int> next;
        next.reserve(frontier.size() * 2);
        for (int v : frontier)
            for (const int* p = adjBegin(v); p != adjEnd(v); ++p)
                if (!visited[*p])
                {
                    visited[*p] = 1;
                    inRing[*p]  = 1;
                    ringBand.push_back(*p);
                    next.push_back(*p);
                }
        frontier = std::move(next);
    }
    int faceInBand = 0;
    for (int v : ringBand) if (v < numFace) ++faceInBand;
    LOG_INFO("AdaptNeckSeam: BFS done in {} ms — seam verts={} pinned, ring band={} (face={}, body={})",
             bfsTimer.Current(),
             (int)std::count(isSeam.begin(), isSeam.end(), (char)1),
             (int)ringBand.size(), faceInBand, (int)ringBand.size() - faceInBand);

    // Face offset from the current face state. Eval composition (pre-skinning):
    //     fullOffset = VDS*VertexDeltas + faceBlend*BodySeamDelta + faceBlend*faceOffset (face only)
    //
    // Dirichlet setup:
    //   Seam verts (inner pin)             → faceOffset[v]  (face-solve value, fixed)
    //   Outside-ring face verts            → faceOffset[v]  (full face, unchanged)
    //   Outside-ring body verts            → 0              (body, unchanged)
    //   Ring-band verts (free)             → 0 initial, Jacobi-relaxed
    //
    // Jacobi averages each free vert with its neighbours. The ring band on the
    // face side sees faceOffset on both inner (seam) and outer (outside-ring)
    // boundaries → relaxes back to faceOffset, no net change. The ring band on
    // the body side sees faceOffset on the inner (seam neighbour) and 0 on the
    // outer → smooth transition across the band. `rings` controls band width.
    Timer faceTimer;
    // Face side acts as a zero-offset Dirichlet boundary for the body
    // Laplacian — no face PCA evaluation needed.
    Eigen::Matrix<float, 3, -1> faceOffset = Eigen::Matrix<float, 3, -1>::Zero(3, numFace);
	if (m->facePatchBlendModel && state.m->faceState)
	{
		const Eigen::Matrix<float, 3, -1> faceVertices = 
			m->facePatchBlendModel->DeformedVertices(*state.m->faceState);
		const Eigen::Matrix<float, 3, -1>& baseVertices = 
			m->facePatchBlendModel->BaseVertices();
		faceOffset = faceVertices - baseVertices;
	}

    // Build pin values + determine which verts participate in Jacobi (free set).
    // Rules per lock mode:
    //   Face lock: face verts are all pinned at faceOffset (ring + outside).
    //              Body ring is free; body outside pinned at 0.
    //              Seam pin = faceOffset (face-side value).
    //   Body lock: body verts are all pinned at 0 (ring + outside).
    //              Face ring is free; face outside pinned at faceOffset.
    //              Seam pin = 0 (body-side value — face seam pulled to match body).
    //   None:      seam verts become free too (no inner Dirichlet boundary).
    //              Both face-ring and body-ring free. Outer face pin = faceOffset,
    //              outer body pin = 0.
    std::vector<char> isFree(numCombined, 0);
    for (int v : ringBand)
    {
        const bool isFaceSide = (v < numFace);
        if (lockSide == SeamLockSide::Face && isFaceSide)   continue;  // face side locked
        if (lockSide == SeamLockSide::Body && !isFaceSide)  continue;  // body side locked
        isFree[v] = 1;
    }
    if (lockSide == SeamLockSide::None)
        for (int v = 0; v < numCombined; ++v)
            if (isSeam[v]) isFree[v] = 1;

    // Pin values are expressed in "vertex offset from rig-neutral" space —
    // the same space BodySeamDelta and VertexDeltas live in. The combined
    // mesh shares one vertex buffer across face + body, so VertexDeltas
    // carry deltas for both halves (template solve writes body verts, face
    // verts stay at zero; refinement can write either).
    //   face pin = faceOffset + VD_face   (PatchBlend + any face delta the
    //                                      user already has in VertexDeltas)
    //   body pin = VD_body                 (body has no PatchBlend — the
    //                                      body's actual offset is purely VD)
    // Seam pin depends on which side we're anchoring to:
    //   Body lock  → use body's value at seam  (= VD at seam)
    //   Face lock  → use face's value at seam  (= faceOffset + VD at seam)
    //   None       → seam is free, no pin.
    const bool haveVD = (state.m->VertexDeltas.cols() == numCombined);
    Eigen::Matrix<float, 3, -1> offsets = Eigen::Matrix<float, 3, -1>::Zero(3, numCombined);
    for (int v = 0; v < numCombined; ++v)
    {
        if (isFree[v]) continue;
        if (v < numFace)
        {
            offsets.col(v) = faceOffset.col(v);
            if (haveVD) offsets.col(v) += state.m->VertexDeltas.col(v);
        }
        else if (haveVD)
        {
            offsets.col(v) = state.m->VertexDeltas.col(v);
        }
        // else body pin = 0 (already zero-initialised)
    }
    if (lockSide == SeamLockSide::Body)
    {
        // Override the face-side contribution at the seam: we want the seam
        // pin to follow body, not face, so drop faceOffset on seam verts.
        for (int v = 0; v < numCombined; ++v)
        {
            if (!isSeam[v] || v >= numFace) continue;
            if (haveVD) offsets.col(v) = state.m->VertexDeltas.col(v);
            else        offsets.col(v).setZero();
        }
    }

    int freeCount = 0;
    for (int v = 0; v < numCombined; ++v) if (isFree[v]) ++freeCount;
    LOG_INFO("AdaptNeckSeam: pinned boundary prepared in {} ms (free verts={})",
             faceTimer.Current(), freeCount);

    // Iterative Jacobi uniform-Laplacian smoothing on the ring band only.
    // Pinned verts outside the ring stay at their initial values — we never
    // touch their columns.
    // Gauss-Seidel with successive over-relaxation (SOR). Info propagates
    // ~2× faster per iter than plain Jacobi because we read already-updated
    // neighbour values in the same pass. `laplacianWeight` maps to the SOR
    // relaxation factor ω: ω=1 is Gauss-Seidel, 1<ω<2 over-relaxes (faster
    // convergence for Laplacian-harmonic problems). Default ω=1.5 if the
    // weight is <=0 or clearly a non-SOR value like 5.
    float omega = laplacianWeight;
    if (omega <= 0.0f || omega >= 2.0f) omega = 1.5f;

    const int totalIters = iterations;

    // Collect free-vert list for iteration. Skipping the isFree[] lookup per
    // iter per vert is worth the extra vector.
    std::vector<int> freeList;
    freeList.reserve(freeCount);
    for (int v = 0; v < numCombined; ++v) if (isFree[v]) freeList.push_back(v);

    Timer smoothTimer;
    for (int iter = 0; iter < totalIters; ++iter)
    {
        Timer iterTimer;
        float maxDelta = 0.0f;
        for (int v : freeList)
        {
            Eigen::Vector3f sum = Eigen::Vector3f::Zero();
            int n = 0;
            for (const int* p = adjBegin(v); p != adjEnd(v); ++p) { sum += offsets.col(*p); ++n; }
            if (n == 0) continue;
            const Eigen::Vector3f avg = sum / float(n);
            const Eigen::Vector3f old = offsets.col(v);
            const Eigen::Vector3f upd = old + omega * (avg - old);
            offsets.col(v) = upd;  // in-place — Gauss-Seidel
            const float d = (upd - old).norm();
            if (d > maxDelta) maxDelta = d;
        }
        if ((iter + 1) % 10 == 0 || iter + 1 == totalIters)
            LOG_INFO("AdaptNeckSeam:   iter {}/{} — {} ms, maxDelta={}", iter + 1, totalIters, iterTimer.Current(), maxDelta);
    }
    LOG_INFO("AdaptNeckSeam: smoothing done in {} ms ({} iters over {} free verts, ω={})",
             smoothTimer.Current(), totalIters, (int)freeList.size(), omega);

    // Reset BodySeamDelta so runs don't accumulate. At eval time Evaluate
    // stacks offsets as:  fullOffset = VDS*VD + BSD + faceOffset(face only)
    // We pin/relax in "final offset" space so the smoothed value equals the
    // full offset we want the vertex to land at. Back out the contributions
    // Evaluate adds for us (VD everywhere, faceOffset on face verts) so BSD
    // carries only the residual.
    Timer writeTimer;
    state.m->BodySeamDelta = Eigen::Matrix<float, 3, -1>::Zero(3, numCombined);
    for (int v : freeList)
    {
        Eigen::Vector3f extra = offsets.col(v);
        if (v < numFace) extra -= faceOffset.col(v);
        if (haveVD) extra -= state.m->VertexDeltas.col(v);
        state.m->BodySeamDelta.col(v) = extra;
    }
    LOG_INFO("AdaptNeckSeam: BodySeamDelta written in {} ms", writeTimer.Current());

    Timer evalTimer;
    EvaluateState(state);
    LOG_INFO("AdaptNeckSeam: EvaluateState in {} ms", evalTimer.Current());
    LOG_INFO("AdaptNeckSeam: total {} ms", totalTimer.Current());
}

bool BodyShapeEditor::SetTargetScaleAndRigidSolve(State& state, const Eigen::Matrix<float, 3, -1>& targetVertices, const BodySolveConfiguration& options)
{
    if (targetVertices.cols() == 0)
    {
        return false;
    }

    Mesh<float> meanMesh = m->rigGeometry->GetMesh(0); // Get mean model

    // Compute bounding box of masked region (or full mesh if no mask)
    Eigen::Vector3f meanMinCorner = Eigen::Vector3f::Constant(std::numeric_limits<float>::max());
    Eigen::Vector3f meanMaxCorner = Eigen::Vector3f::Constant(-std::numeric_limits<float>::max());

    if (options.vertexMask.size() > 0)
    {
        bool hasActiveVertices = false;
        for (int i = 0; i < meanMesh.NumVertices() && i < options.vertexMask.size(); ++i)
        {
            if (options.vertexMask[i] > 0.0f)
            {
                meanMinCorner = meanMinCorner.cwiseMin(meanMesh.Vertices().col(i));
                meanMaxCorner = meanMaxCorner.cwiseMax(meanMesh.Vertices().col(i));
                hasActiveVertices = true;
            }
        }

        // If mask contains no active vertices, fall back to full mesh bounds
        if (!hasActiveVertices)
        {
            meanMinCorner = meanMesh.Vertices().rowwise().minCoeff();
            meanMaxCorner = meanMesh.Vertices().rowwise().maxCoeff();
        }
    }
    else
    {
        meanMinCorner = meanMesh.Vertices().rowwise().minCoeff();
        meanMaxCorner = meanMesh.Vertices().rowwise().maxCoeff();
    }

    // Compute target mesh bounding box
    Eigen::Vector3f targetMinCorner = targetVertices.rowwise().minCoeff();
    Eigen::Vector3f targetMaxCorner = targetVertices.rowwise().maxCoeff();

    float uniformScale = 1.0f;

    if (std::abs(meanMaxCorner.y() - meanMinCorner.y()) < scaleTolerence ||
        std::abs(targetMaxCorner.y() - targetMinCorner.y()) < scaleTolerence)
    {
        return false;
    }
    float meanHeight = meanMaxCorner.y() - meanMinCorner.y();
    float targetHeight = targetMaxCorner.y() - targetMinCorner.y();
    uniformScale = targetHeight / meanHeight;

    state.m->ScaleFactor = uniformScale;


    // Compute bbox centers
    Eigen::Vector3f meanBBoxCenter = (meanMinCorner + meanMaxCorner) * 0.5f * uniformScale;
    Eigen::Vector3f targetBBoxCenter = (targetMinCorner + targetMaxCorner) * 0.5f;

    // Set up optimization to align bbox centers
    auto poseLogic = m->poseLogic->Clone();

    BoundedVectorVariable<float> pose{ state.m->GuiControls };
    Context<float> context{};
    GaussNewtonSolver<float> solver;

    std::vector<int> constantIndices;
    const auto& controlNames = poseLogic->GuiControlNames();
    for (int i = 0; i < controlNames.size(); ++i)
    {
        const std::string& name = controlNames[i];
        if (!name.starts_with("pose_rigid_root.tx") &&
            !name.starts_with("pose_rigid_root.ty") &&
            !name.starts_with("pose_rigid_root.tz"))
        {
            constantIndices.push_back(i);
        }
    }
    pose.MakeIndividualIndicesConstant(constantIndices);

    std::function<Cost<float>(Context<float>*)> costFunction = [&](Context<float>* context)
    {
        Cost<float> cost;
        const DiffData<float> guiControls = pose.Evaluate(context);
        const DiffData<float> rawControls = poseLogic->EvaluateRawControls(guiControls);
        const DiffData<float> joints = poseLogic->EvaluateJoints(0, rawControls);

        // Extract root translation from joints
        Eigen::Vector3f rootTranslation = Eigen::Vector3f::Zero();
        const int rootJointIndex = 0;
        if (joints.Size() >= (rootJointIndex + 1) * 6)
        {
            rootTranslation.x() = joints.Value()[rootJointIndex * 6 + 0];
            rootTranslation.y() = joints.Value()[rootJointIndex * 6 + 1];
            rootTranslation.z() = joints.Value()[rootJointIndex * 6 + 2];
        }

        // Scale the root translation to match the scaled bbox center
        Eigen::Vector3f scaledRootTranslation = rootTranslation * uniformScale;

        // Transform the bbox center by scaled root translation
        Eigen::Vector3f transformedCenter = meanBBoxCenter + scaledRootTranslation;

        // Compute residual: difference between transformed center and target center
        Vector<float> residual = transformedCenter - targetBBoxCenter;

        DiffData<float> residualDiffData(residual);
        if (joints.HasJacobian())
        {
            // Build jacobian: d(transformedCenter)/d(controls) = uniformScale * d(rootTranslation)/d(controls)
            const int txParamIndex = rootJointIndex * 6 + 0;
            const int tyParamIndex = rootJointIndex * 6 + 1;
            const int tzParamIndex = rootJointIndex * 6 + 2;

            SparseMatrixConstPtr<float> jointsJacPtr = joints.Jacobian().AsSparseMatrix();
            const SparseMatrix<float>& jointsJac = *jointsJacPtr;

            SparseMatrix<float> centerJacobian(3, joints.Jacobian().Cols());
            std::vector<Eigen::Triplet<float>> triplets;

            for (SparseMatrix<float>::InnerIterator it(jointsJac, txParamIndex); it; ++it)
            {
                triplets.push_back(Eigen::Triplet<float>(0, it.col(), it.value() * uniformScale));
            }
            for (SparseMatrix<float>::InnerIterator it(jointsJac, tyParamIndex); it; ++it)
            {
                triplets.push_back(Eigen::Triplet<float>(1, it.col(), it.value() * uniformScale));
            }
            for (SparseMatrix<float>::InnerIterator it(jointsJac, tzParamIndex); it; ++it)
            {
                triplets.push_back(Eigen::Triplet<float>(2, it.col(), it.value() * uniformScale));
            }

            centerJacobian.setFromTriplets(triplets.begin(), triplets.end());
            auto jacobianPtr = std::make_shared<SparseJacobian<float>>(
                std::make_shared<SparseMatrix<float>>(centerJacobian), 0);
            residualDiffData.SetJacobianPtr(jacobianPtr);
        }

        cost.Add(std::move(residualDiffData), 1.0f, "rigid_translation", true);
        return cost;
    };

    GaussNewtonSolver<float>::Settings solverSettings;
    solverSettings.iterations = 1;
    solverSettings.reg = 1e-4f;
    solverSettings.maxLineSearchIterations = 10;
    solverSettings.residualErrorStoppingCriterion = 0.01f;
    solverSettings.predictionReductionStoppingCriterion = 0.01f;
    solver.Solve(costFunction, context, solverSettings);

    state.m->GuiControls = pose.Value();
    state.m->GuiControlsPrior = state.m->GuiControls;
    EvaluateState(state);
    return true;
}

void BodyShapeEditor::AlignToTargetMesh(State& state, const BodyShapeEditorTarget& target)
{
    using MeshSlot = BodyShapeEditorTarget::MeshSlot;

    auto targetMeshPtr = target.PrimaryMesh();
    auto landmarks2D   = target.Landmarks2D();

    const bool hasHead     = target.HasMesh(MeshSlot::Head);
    const bool hasBody     = target.HasMesh(MeshSlot::Body);
    const bool hasCombined = target.HasMesh(MeshSlot::Combined);
    const bool headOnly    = hasHead && !hasBody && !hasCombined;

    auto nVerts = [&](MeshSlot s) {
        return target.HasMesh(s) ? (int)target.MeshFor(s)->NumVertices() : 0;
    };
    const char* decidedBranch = headOnly ? "head-only"
                              : hasCombined ? "combined"
                              : (hasHead && hasBody) ? "head+body"
                              : hasBody ? "body-only"
                              : "unknown";
    LOG_INFO("AlignToTargetMesh: BEGIN slots=[head={}v body={}v combined={}v] landmarks2D={} keypoints={} -> branch='{}'",
             nVerts(MeshSlot::Head), nVerts(MeshSlot::Body), nVerts(MeshSlot::Combined),
             target.Landmarks2D() ? "yes" : "no", (int)target.Keypoints().size(), decidedBranch);

    // Reset rigid controls + scale — idempotent entry; clears any residual
    // rotation/translation from prior solves so every Align run starts clean.
    {
        const auto& controlNames = GetGuiControlNames();
        Eigen::VectorXf controls = state.GetGuiControls();
        for (int i = 0; i < (int)controlNames.size(); ++i)
            if (controlNames[i].find("pose_rigid_") != std::string::npos)
                controls[i] = 0.0f;
        state.SetGuiControls(controls);
        state.m->ScaleFactor = 1.0f;
        EvaluateState(state);
    }

    // Shared finalisers — write pose_rigid_root.t* / pose_rigid_pelvis.r* then eval.
    const auto& controlNames = GetGuiControlNames();
    Eigen::VectorXf guiControls = state.GetGuiControls();
    auto setControl = [&](const std::string& name, float value) {
        auto it = std::find(controlNames.begin(), controlNames.end(), name);
        if (it != controlNames.end())
            guiControls[int(it - controlNames.begin())] = value;
        else
            LOG_WARNING("AlignToTargetMesh: control not found: {}", name);
    };
    auto applyRootT = [&](const Eigen::Vector3f& t_root) {
        const Eigen::Vector3f controlT = t_root / 100.0f;  // joint-matrix weight
        setControl("pose_rigid_root.tx", controlT.x());
        setControl("pose_rigid_root.ty", controlT.y());
        setControl("pose_rigid_root.tz", controlT.z());
    };
    auto applyPelvisEuler = [&](const Eigen::Vector3f& euler) {
        setControl("pose_rigid_pelvis.rx", euler.x());
        setControl("pose_rigid_pelvis.ry", euler.y());
        setControl("pose_rigid_pelvis.rz", euler.z());
    };
    auto finish = [&]() {
        state.SetGuiControls(guiControls);
        state.m->GuiControlsPrior = state.m->GuiControls;
        EvaluateState(state);
        LOG_INFO("AlignToTargetMesh: DONE");
    };

    // ── Head-only target: full Procrustes on face landmarks + curves.
    //    Heads legitimately tilt, so rotation is allowed here.
    if (headOnly)
    {
        if (!landmarks2D || !targetMeshPtr)
        {
            LOG_WARNING("AlignToTargetMesh: head-only but no landmarks/mesh, skipping");
            return;
        }
        const Eigen::Matrix<float, 3, -1>& meshVerts = state.GetMesh(0).Vertices();

        std::map<std::string, Eigen::Vector3f> rigLm;
        std::map<std::string, std::vector<Eigen::Vector3f>> rigCurves;
        const auto& meshLm = landmarks2D->GetMeshLandmarks();
        for (const auto& [n, bc] : meshLm.LandmarksBarycentricCoordinates())
            rigLm[n] = bc.template Evaluate<3>(meshVerts);
        for (const auto& [n, bcv] : meshLm.MeshCurvesBarycentricCoordinates())
        {
            auto& arr = rigCurves[n]; arr.reserve(bcv.size());
            for (const auto& bc : bcv) arr.push_back(bc.template Evaluate<3>(meshVerts));
        }

        std::vector<Eigen::Vector3f> srcPts, tgtPts;
        for (const auto& [lmInst, cam] : landmarks2D->GetTargetLandmarks())
        {
            const auto lmTri = TriangulateLandmarksViaAABB(cam, lmInst, *targetMeshPtr);
            for (const auto& [n, srcPt] : rigLm)
            {
                auto it = lmTri.find(n);
                if (it != lmTri.end() && it->second.second)
                { srcPts.push_back(srcPt); tgtPts.push_back(it->second.first); }
            }
            const auto cTri = TriangulateCurvesViaAABB(cam, lmInst, *targetMeshPtr);
            for (const auto& [n, srcCurve] : rigCurves)
            {
                auto it = cTri.find(n);
                if (it == cTri.end()) continue;
                const auto& [tgtCurve, valid] = it->second;
                const int N = std::min((int)srcCurve.size(), (int)tgtCurve.cols());
                for (int i = 0; i < N; ++i)
                    if (i < (int)valid.size() && valid[i])
                    { srcPts.push_back(srcCurve[i]); tgtPts.push_back(tgtCurve.col(i)); }
            }
        }
        LOG_INFO("AlignToTargetMesh[head]: correspondences = {}", srcPts.size());
        if (srcPts.size() < 3)
        {
            LOG_WARNING("AlignToTargetMesh[head]: insufficient correspondences");
            return;
        }

        Eigen::Matrix<float, 3, -1> srcMat(3, (int)srcPts.size());
        Eigen::Matrix<float, 3, -1> tgtMat(3, (int)tgtPts.size());
        for (int i = 0; i < (int)srcPts.size(); ++i)
        { srcMat.col(i) = srcPts[i]; tgtMat.col(i) = tgtPts[i]; }

        auto [scale, affine] = Procrustes<float, 3>::AlignRigidAndScale(srcMat, tgtMat, true);
        if (scale < 1e-6f)
        {
            LOG_WARNING("AlignToTargetMesh[head]: degenerate scale {}", scale);
            return;
        }
        const Eigen::Matrix3f R = affine.Linear();
        const Eigen::Vector3f t = affine.Translation();

        state.m->ScaleFactor = scale;
        const Eigen::Vector3f pelvisPos  = state.GetJointBindMatrices()[1].translation();
        const Eigen::Matrix3f pelvisBind = state.GetJointBindMatrices()[1].linear();
        const Eigen::Vector3f t_root = t / scale + (R - Eigen::Matrix3f::Identity()) * pelvisPos;
        const Eigen::Matrix3f pelvisLocalRot = pelvisBind.transpose() * R * pelvisBind;
        const Eigen::Vector3f pelvisEuler = RotationMatrixToEulerXYZ<float>(pelvisLocalRot);
        applyRootT(t_root);
        applyPelvisEuler(pelvisEuler);
        LOG_INFO("AlignToTargetMesh[head]: scale={} pelvisEuler=({},{},{}) t_root=({},{},{})",
                 scale, pelvisEuler.x(), pelvisEuler.y(), pelvisEuler.z(),
                 t_root.x(), t_root.y(), t_root.z());
        finish();
        return;
    }

    // ── Body / Combined: R = I, scale from Y-bbox, translation from bbox.
    //    Three concrete input shapes end up here (headOnly was handled above):
    //      - bodyOnly  (body scan, no head)        → rig bbox over [numFaceVertices, end)
    //      - combined  (single merged scan)        → rig bbox over full mesh, target is that mesh
    //      - head+body (two separate scans, no combined slot — two-ICP mode) →
    //                                              → rig bbox over full mesh, target is head∪body bbox
    if (!targetMeshPtr)
    {
        LOG_WARNING("AlignToTargetMesh: no target mesh, skipping");
        return;
    }

    const bool bodyOnly    = hasBody && !hasHead && !hasCombined;
    const bool isFullBody  = hasCombined || (hasHead && hasBody);  // rig bbox over whole mesh
    const char* regionTag  = bodyOnly ? "body" : (hasCombined ? "combined" : "head+body");

    const Eigen::Matrix<float, 3, -1>& rigVerts = m->rigGeometry->GetMesh(0).Vertices();
    const int bodyStart = isFullBody ? 0 : m->numFaceVertices;
    float rigMinX =  std::numeric_limits<float>::max(), rigMaxX = -std::numeric_limits<float>::max();
    float rigMinY =  std::numeric_limits<float>::max(), rigMaxY = -std::numeric_limits<float>::max();
    float rigMinZ =  std::numeric_limits<float>::max(), rigMaxZ = -std::numeric_limits<float>::max();
    for (int i = bodyStart; i < (int)rigVerts.cols(); ++i)
    {
        const float x = rigVerts(0, i), y = rigVerts(1, i), z = rigVerts(2, i);
        if (x < rigMinX) rigMinX = x; if (x > rigMaxX) rigMaxX = x;
        if (y < rigMinY) rigMinY = y; if (y > rigMaxY) rigMaxY = y;
        if (z < rigMinZ) rigMinZ = z; if (z > rigMaxZ) rigMaxZ = z;
    }
    const float rigH = rigMaxY - rigMinY;

    // Target bbox. Walk whatever mesh slots the target has and take the union — that
    // way head+body (two separate scans) measures head-top to feet properly, while
    // combined/body-only degenerate to just the single mesh.
    float tgtMinX =  std::numeric_limits<float>::max(), tgtMaxX = -std::numeric_limits<float>::max();
    float tgtMinY =  std::numeric_limits<float>::max(), tgtMaxY = -std::numeric_limits<float>::max();
    float tgtMinZ =  std::numeric_limits<float>::max(), tgtMaxZ = -std::numeric_limits<float>::max();
    auto accumulate = [&](const Mesh<float>& m) {
        const auto& v = m.Vertices();
        tgtMinX = std::min(tgtMinX, v.row(0).minCoeff()); tgtMaxX = std::max(tgtMaxX, v.row(0).maxCoeff());
        tgtMinY = std::min(tgtMinY, v.row(1).minCoeff()); tgtMaxY = std::max(tgtMaxY, v.row(1).maxCoeff());
        tgtMinZ = std::min(tgtMinZ, v.row(2).minCoeff()); tgtMaxZ = std::max(tgtMaxZ, v.row(2).maxCoeff());
    };
    if (hasCombined) accumulate(*target.MeshFor(MeshSlot::Combined));
    else {
        if (hasHead) accumulate(*target.MeshFor(MeshSlot::Head));
        if (hasBody) accumulate(*target.MeshFor(MeshSlot::Body));
    }
    const float tgtH = tgtMaxY - tgtMinY;

    if (rigH < 1e-4f || tgtH < 1e-4f)
    {
        LOG_WARNING("AlignToTargetMesh: degenerate height (rigH={}, tgtH={})", rigH, tgtH);
        return;
    }
    const float scale = tgtH / rigH;
    state.m->ScaleFactor = scale;
    LOG_INFO("AlignToTargetMesh[{}]: Y-bbox scale rigH={} tgtH={} -> s={}",
             regionTag, rigH, tgtH, scale);

    // Translation: bbox-only. Y aligns min-Y (feet on ground), X/Z align bbox
    // centres. Keypoints are deliberately NOT used here — a sparse or upper-
    // body-biased keypoint set (e.g. the 5 body landmarks the pose detector
    // usually gives us) pulls the centroid mid-chest and floats the character
    // off the ground. Keypoints still drive the Body solve that runs right
    // after, so they're not ignored — just excluded from the initial Align.
    Eigen::Vector3f t_world;
    t_world.x() = 0.5f * (tgtMinX + tgtMaxX) - scale * 0.5f * (rigMinX + rigMaxX);
    t_world.y() = tgtMinY                     - scale * rigMinY;
    t_world.z() = 0.5f * (tgtMinZ + tgtMaxZ) - scale * 0.5f * (rigMinZ + rigMaxZ);
    LOG_INFO("AlignToTargetMesh[{}]: translation from bbox (minY + centreXZ)", regionTag);

    // R = I; rig pelvis-skinned vertex: finalV = scale*(v + t_root)
    // Equating to scale*v + t_world  ->  t_root = t_world / scale.
    const Eigen::Vector3f t_root = t_world / scale;
    applyRootT(t_root);
    applyPelvisEuler(Eigen::Vector3f::Zero());
    LOG_INFO("AlignToTargetMesh[{}]: scale={} t_root=({},{},{})",
             regionTag, scale, t_root.x(), t_root.y(), t_root.z());
    finish();
}

Vector<float> BodyShapeEditor::SolveForArbitraryMeshWithICP(
    State& state,
    const BodyShapeEditorTarget& target,
    const BodySolveConfiguration& options,
    const std::vector<int>& lockedControlIndices,
    IterationFunc iterationFunc)
{
    Timer solveTimer;

    auto targetMeshPtr = target.PrimaryMesh();
    if (!targetMeshPtr)
    {
        LOG_ERROR("SolveForArbitraryMeshWithICP: No target mesh provided");
        return state.m->GuiControls;
    }

    float uniformScale =  state.m->ScaleFactor;
    const auto& keypointCorrespondences = target.Keypoints();
    const auto& jointCorrespondences = target.Joints();
    const auto& viewportConstraints2D = target.Viewports();
    auto landmarks2DShared = target.Landmarks2D();
    auto* landmarkConstraints2D = landmarks2DShared.get();
    UpdateLandmarkConstraintsConfig(landmarkConstraints2D, options);

    auto poseGeometry = m->rigGeometry->Clone();
    auto poseLogic = m->poseLogic->Clone();
    state.m->VertexDeltas.setZero();

    WeightSchedule normalCompatSchedule = LoadWeightSchedule(options.body, "normalCompat");
    WeightSchedule joint2DSchedule      = LoadWeightSchedule(options.body, "joint2D");

    // ── Build the list of ICP targets ──
    //
    // Resolution rule:
    //   • Combined slot set               → single ICP against that mesh (legacy path)
    //   • else Head AND Body slots set    → two ICPs, each restricted to its rig region
    //                                        via a correspondence-search mask
    //   • else                            → single ICP against PrimaryMesh()
    //
    // numFaceVertices is the rig's face/body split; the region masks keep head-ICP
    // from pulling body verts and vice-versa.
    using MeshSlot = BodyShapeEditorTarget::MeshSlot;
    struct IcpSlot {
        std::shared_ptr<const Mesh<float>> mesh;
        Eigen::VectorXf regionMask;     // empty = no region restriction (rig side)
        Eigen::VectorXf targetWeights;  // non-degenerate mask, per target vertex
        const char* tag;
    };
    std::vector<IcpSlot> icpSlots;
    auto nVertsSlot = [&](MeshSlot s) {
        return target.HasMesh(s) ? (int)target.MeshFor(s)->NumVertices() : 0;
    };
    const int totalRigVerts = m->rigGeometry->GetMesh(0).NumVertices();
    LOG_INFO("SolveForArbitraryMeshWithICP: target slots=[head={}v body={}v combined={}v] rigVerts={} numFaceVertices={} stepMask.size={} icpWeight={}",
             nVertsSlot(MeshSlot::Head), nVertsSlot(MeshSlot::Body), nVertsSlot(MeshSlot::Combined),
             totalRigVerts, m->numFaceVertices,
             (int)options.vertexMask.size(), options.body["icp"].Value<float>());

    const char* resolution = "none";
    if (target.HasMesh(MeshSlot::Combined))
    {
        icpSlots.push_back({
            target.MeshFor(MeshSlot::Combined), {},
            target.NonDegenerateMaskFor(MeshSlot::Combined),
            "combined" });
        resolution = "single-combined";
    }
    else if (target.HasMesh(MeshSlot::Head) && target.HasMesh(MeshSlot::Body))
    {
        const int faceEnd       = std::min(m->numFaceVertices, totalRigVerts);
        Eigen::VectorXf headMask = Eigen::VectorXf::Zero(totalRigVerts);
        headMask.head(faceEnd).setOnes();
        Eigen::VectorXf bodyMask = Eigen::VectorXf::Zero(totalRigVerts);
        bodyMask.tail(totalRigVerts - faceEnd).setOnes();
        icpSlots.push_back({
            target.MeshFor(MeshSlot::Head), std::move(headMask),
            target.NonDegenerateMaskFor(MeshSlot::Head),
            "head" });
        icpSlots.push_back({
            target.MeshFor(MeshSlot::Body), std::move(bodyMask),
            target.NonDegenerateMaskFor(MeshSlot::Body),
            "body" });
        resolution = "dual-head+body";
    }
    else
    {
        // PrimaryMesh — find which slot it is so we can pull its target mask.
        Eigen::VectorXf primaryMask;
        for (int s = 0; s < (int)BodyShapeEditorTarget::MeshSlot::Combined + 1; ++s)
        {
            const auto slot = static_cast<MeshSlot>(s);
            if (target.MeshFor(slot) == targetMeshPtr) { primaryMask = target.NonDegenerateMaskFor(slot); break; }
        }
        icpSlots.push_back({ targetMeshPtr, {}, std::move(primaryMask), "primary" });
        resolution = "single-primary";
    }
    LOG_INFO("SolveForArbitraryMeshWithICP: resolved target = '{}' ({} ICP slot{})",
             resolution, (int)icpSlots.size(), icpSlots.size() == 1 ? "" : "s");

    std::vector<std::unique_ptr<ICPConstraints<float>>> icps;
    std::optional<Configuration> icpConfig;
    if ((options.body["icp"].Value<float>() > 0.0f))
    {
        for (const auto& slot : icpSlots)
        {
            if (!slot.mesh) continue;
            auto icp = std::make_unique<ICPConstraints<float>>();
            icp->SetTargetMesh(*slot.mesh);
            // Non-degenerate target weights — masks border / zero-normal /
            // high-edge-ratio verts so ICP doesn't latch onto pinch points.
            // Mask is sized to the target mesh's vertex count.
            if (slot.targetWeights.size() == (int)slot.mesh->NumVertices())
                icp->SetTargetWeights(slot.targetWeights);
            if (!icpConfig) icpConfig = icp->GetConfiguration();
            (*icpConfig)["geometryWeight"] = options.body["icp"].Value<float>();
            (*icpConfig)["point2point"] = 0.0f;
            (*icpConfig)["normalIncompatibilityThreshold"] = normalCompatSchedule.start;
            (*icpConfig)["minimumDistanceThreshold"] = options.body["icpTol"].Value<float>();
            icp->SetConfiguration(*icpConfig);

            // Combine the slot's region mask (if any) with the step's user mask by
            // element-wise product. Either may be empty; empty means "no restriction"
            // for that component.
            Eigen::VectorXf searchMask;
            if (slot.regionMask.size() > 0 && options.vertexMask.size() == slot.regionMask.size())
                searchMask = slot.regionMask.array() * options.vertexMask.array();
            else if (slot.regionMask.size() > 0)
                searchMask = slot.regionMask;
            else if (options.vertexMask.size() > 0)
                searchMask = options.vertexMask;
            if (searchMask.size() > 0)
                icp->SetCorrespondenceSearchVertexWeights(VertexWeights<float>(searchMask));

            const int activeRigVerts = (searchMask.size() > 0)
                ? (int)(searchMask.array() > 0.0f).count()
                : totalRigVerts;
            LOG_INFO("SolveForArbitraryMeshWithICP: ICP slot='{}' targetVerts={} rigVertsActive={}/{} regionMask={} stepMask={}",
                     slot.tag, (int)slot.mesh->NumVertices(),
                     activeRigVerts, totalRigVerts,
                     slot.regionMask.size() > 0 ? "yes" : "no",
                     options.vertexMask.size() > 0 ? "yes" : "no");
            icps.push_back(std::move(icp));
        }
    }
    const bool hasIcp = !icps.empty();

    auto startsWith = [](const std::vector<std::string>& prefixes, const std::vector<std::string>& names)
    {
        std::vector<int> result;
        for (int i = 0; i < (int)names.size(); ++i)
        {
            const auto& name = names[i];
            for (const auto& prefix : prefixes)
            {
                if (name.starts_with(prefix))
                {
                    result.push_back(i);
                    break;
                }
            }
        }
        return result;
    };
    BoundedVectorVariable<float> pose{ state.m->GuiControls };

    Vector<float> symControlsInit = m->symControls->GuiToSymmetricControls(state.m->GuiControls);
    BoundedVectorVariable<float> symPose{ symControlsInit };

    Eigen::VectorXf scaleInit(1);
    scaleInit[0] = uniformScale;
    VectorVariable<float> scaleVar(scaleInit);
    if (options.lockScale)
    {
        // Pin the uniform scale — pose + proportions still optimise, but the Align-
        // produced ScaleFactor rides through unchanged. Useful for refinement passes
        // (fingers/feet) where re-optimising scale just re-stretches the whole body.
        scaleVar.MakeConstant();
        LOG_INFO("SolveForArbitraryMeshWithICP: lockScale=true, scaleVar pinned at {}", uniformScale);
    }

    Context<float> context{};
    GaussNewtonSolver<float> solver;
    std::vector<int> constantIndices{};
    const bool useSymmetry = options.body["symmetry"].Value<bool>();

    // Initialize pose from joint rotations stored on the target, if provided
    const auto targetJointRotations = target.JointRotations();
    if (targetJointRotations.size() == static_cast<std::size_t>(m->rigGeometry->NumJoints()) * 3u)
    {
        Vector<float> poseWithLockedValues = pose.Value();

        const auto& jointHierarchy = m->rigGeometry->GetJointParentIndices();
        const auto getJointParent = [&jointHierarchy](std::uint16_t jointIndex)
        {
            return jointHierarchy[jointIndex];
        };

        const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices = state.GetJointBindMatrices();
        std::vector<float> jointRotations;
        jointRotations.resize(m->rigGeometry->NumJoints() * 3);

        for (std::uint16_t jointIndex = 0; jointIndex < m->rigGeometry->NumJoints(); jointIndex++)
        {
            Eigen::Transform<float, 3, Eigen::Affine> localTransform;
            const int parentJointIndex = getJointParent(jointIndex);
            if (parentJointIndex >= 0)
            {
                auto parentTransform = jointMatrices[parentJointIndex];
                localTransform = parentTransform.inverse() * jointMatrices[jointIndex];
            }
            else
            {
                localTransform = jointMatrices[jointIndex];
            }

            Eigen::Matrix<float, 3, 3> InJointRotation = EulerXYZ(targetJointRotations[jointIndex * 3 + 0], targetJointRotations[jointIndex * 3 + 1], targetJointRotations[jointIndex * 3 + 2]);
            Eigen::Vector3f euler = RotationMatrixToEulerXYZ<float>(localTransform.linear().inverse() * InJointRotation);
            jointRotations[jointIndex * 3 + 0] = euler[0];
            jointRotations[jointIndex * 3 + 1] = euler[1];
            jointRotations[jointIndex * 3 + 2] = euler[2];
        }

        for (auto ji : m->jointEstimator.CoreJoints())
        {
            if (ji < 2) continue;

            std::string jointControlNamePrefix = "pose_driver_" + m->rigGeometry->GetJointNames()[ji] + ".";
            auto jointControlIndices = startsWith({ jointControlNamePrefix }, poseLogic->GuiControlNames());
            if (jointControlIndices.size() == 3)
            {
                poseWithLockedValues(jointControlIndices[0]) = jointRotations[ji * 3 + 0];
                poseWithLockedValues(jointControlIndices[1]) = jointRotations[ji * 3 + 1];
                poseWithLockedValues(jointControlIndices[2]) = jointRotations[ji * 3 + 2];
            }
        }
        pose = poseWithLockedValues;

        // Lock pose controls after initializing from external rotations
        const auto poseIndices = startsWith({ "pose_driver" }, poseLogic->GuiControlNames());
        constantIndices.insert(constantIndices.end(), poseIndices.begin(), poseIndices.end());
    }


    // Add locked control indices from state (values are already in GuiControls)
    const std::vector<int>& stateLockedIndices = state.GetLockedControlIndices();
    if (!stateLockedIndices.empty())
    {
        constantIndices.insert(constantIndices.end(), stateLockedIndices.begin(), stateLockedIndices.end());
    }

    // Add user-specified locked control indices (parameter override)
    if (!lockedControlIndices.empty())
    {
        constantIndices.insert(constantIndices.end(), lockedControlIndices.begin(), lockedControlIndices.end());
    }

    if (useSymmetry)
    {
        const auto& names = poseLogic->GuiControlNames();
        const auto nonSymIndices = m->symControls->GetNonSymmetricGuiIndices();
        for (int idx : nonSymIndices)
        {
            if (names[idx].starts_with("pose_driver"))
            {
                if (std::find(constantIndices.begin(), constantIndices.end(), idx) == constantIndices.end())
                    constantIndices.push_back(idx);
            }
        }
    }

    pose.MakeIndividualIndicesConstant(constantIndices);
    if (useSymmetry)
    {
        const auto symIndices = m->symControls->GuiIndicesToSymmetricIndices(constantIndices);
        symPose.MakeIndividualIndicesConstant(symIndices);
    }

    auto geometryState = m->StatePoolJacobian.Aquire();

    const int totalIters = options.body["iterations"].Value<int>();
    const auto poseDriverIndices = startsWith({ "pose_driver" }, poseLogic->GuiControlNames());

    WeightSchedule regGlobalSchedule      = LoadWeightSchedule(options.body, "regGlobal");
    WeightSchedule regLocalSchedule       = LoadWeightSchedule(options.body, "regLocal");
    WeightSchedule regProportionsSchedule = LoadWeightSchedule(options.body, "regProportions");
    WeightSchedule regPoseSchedule        = LoadWeightSchedule(options.body, "regPose");

    WeightSchedule icpSchedule        = LoadWeightSchedule(options.body, "icp");
    WeightSchedule keypointSchedule   = LoadWeightSchedule(options.body, "keypoint");
    WeightSchedule landmark2DSchedule = LoadWeightSchedule(options.body, "landmark2D");
    WeightSchedule toleranceSchedule  = LoadWeightSchedule(options.body, "icpTol");

    const bool logCostBreakdown = options.body["logCostBreakdown"].Value<bool>();

    float previousCost = std::numeric_limits<float>::max();
    Mesh<float> poseGeometryMesh = poseGeometry->GetMesh(0);

    // Log joint2D constraint summary once before loop
    {
        int totalJoint2DConstraints = 0;
        for (const auto& [mat, constraints] : viewportConstraints2D)
            for (const auto& c : constraints)
                if (c.isJoint) ++totalJoint2DConstraints;
        LOG_INFO("Body solve: proportionIndices={} joint2DConstraints={} joint2DWeight={} regProportions={}",
            m->localSkeletonIndices.size(), totalJoint2DConstraints,
            joint2DSchedule.start, regProportionsSchedule.start);
    }


    // Pose-driver chain consistency matrix — built once from GUI control
    // names. Each row encodes one (c_i − c_j) pair: penalising opposite
    // rotations on adjacent drivers in a kinematic chain. Currently chains
    // pose_driver_neck_01 → pose_driver_neck_02 → pose_driver_head across
    // rx / ry / rz. Off when poseChain weight = 0.
    SparseMatrix<float> poseChainMatrix;
    {
        const auto& ctrlNames = poseLogic->GuiControlNames();
        auto findIdx = [&](const std::string& name) -> int {
            auto it = std::find(ctrlNames.begin(), ctrlNames.end(), name);
            return (it != ctrlNames.end()) ? (int)(it - ctrlNames.begin()) : -1;
        };
        std::vector<Eigen::Triplet<float>> trips;
        int row = 0;
        auto addPair = [&](int a, int b) {
            if (a < 0 || b < 0) return;
            trips.emplace_back(row, a,  1.0f);
            trips.emplace_back(row, b, -1.0f);
            ++row;
        };
        for (const char* axis : { ".rx", ".ry", ".rz" })
        {
            const int n01 = findIdx(std::string("pose_driver_neck_01") + axis);
            const int n02 = findIdx(std::string("pose_driver_neck_02") + axis);
            const int hd  = findIdx(std::string("pose_driver_head")    + axis);
            addPair(n01, n02);
            addPair(n02, hd);
        }
        if (row > 0)
        {
            poseChainMatrix.resize(row, (int)ctrlNames.size());
            poseChainMatrix.setFromTriplets(trips.begin(), trips.end());
            LOG_INFO("Body solve: pose-driver chain regulariser active — {} pairs", row);
        }
    }
    const float poseChainWeight = options.body["poseChain"].Value<float>();

    SparseMatrix<float> gwm(poseLogic->NumGUIControls(), poseLogic->NumGUIControls());
    Eigen::VectorXf prevPose = useSymmetry ? symControlsInit : state.m->GuiControls;
    for (int icpIter = 0; icpIter < totalIters; ++icpIter)
    {
        const float wRegGlobal      = regGlobalSchedule.Evaluate(icpIter, totalIters);
        const float wRegLocal       = regLocalSchedule.Evaluate(icpIter, totalIters);
        const float wRegProportions = regProportionsSchedule.Evaluate(icpIter, totalIters);
        const float wRegPose        = regPoseSchedule.Evaluate(icpIter, totalIters);
        Eigen::VectorXf guiWeight = Eigen::VectorXf::Zero(poseLogic->NumGUIControls());
        for (const auto& index : poseDriverIndices) guiWeight[index] = wRegPose;
        for (const auto& index : m->localShapeIndices) guiWeight[index] = wRegLocal;
        for (const auto& index : m->localSkeletonIndices) guiWeight[index] = wRegProportions;
        for (const auto& index : m->globalIndices) guiWeight[index] = wRegGlobal;
        LOG_INFO("Body iter {}/{}: icp={} kp={} lm2d={} joint2D={} regG={} regL={} regP={} regPose={}",
            icpIter + 1, totalIters,
            icpSchedule.Evaluate(icpIter, totalIters), keypointSchedule.Evaluate(icpIter, totalIters),
            landmark2DSchedule.Evaluate(icpIter, totalIters), joint2DSchedule.Evaluate(icpIter, totalIters),
            wRegGlobal, wRegLocal, wRegProportions, wRegPose);
        gwm.setIdentity();
        gwm.diagonal() = guiWeight;

        if ((icpSchedule.start > 0.0f || icpSchedule.end > 0.0f) && hasIcp)
        {
            (*icpConfig)["minimumDistanceThreshold"]      = toleranceSchedule.Evaluate(icpIter, totalIters);
            (*icpConfig)["normalIncompatibilityThreshold"] = normalCompatSchedule.Evaluate(icpIter, totalIters);
            for (auto& icp : icps) icp->SetConfiguration(*icpConfig);

            const DiffData<float> guiControls = useSymmetry
                ? m->symControls->EvaluateSymmetricControls(symPose.Evaluate(&context))
                : pose.Evaluate(&context);
            const DiffData<float> rawControls = poseLogic->EvaluateRawControls(guiControls);
            const DiffData<float> joints = poseLogic->EvaluateJoints(0, rawControls);
            const DiffData<float> rbfPsd = m->rbfLogic->EvaluatePoseControlsFromJoints(joints, true);
            const DiffData<float> rbfJoints = poseLogic->EvaluateRbfJoints(0, rbfPsd);
            const DiffData<float> twistedJoints = m->twistSwingLogic->EvaluateJointsFromJoints(joints + rbfJoints);

            if (m->facePatchBlendModel && state.m->faceState)
            {
                if (!state.m->faceState)
                {
                    state.m->faceState = m->facePatchBlendModel->CreateState();
                }
                Eigen::Matrix<float, 3, -1> faceVertices = m->facePatchBlendModel->DeformedVertices(*state.m->faceState);
                const Eigen::Matrix<float, 3, -1>& baseVertices = m->facePatchBlendModel->BaseVertices();
                Eigen::Matrix<float, 3, -1> faceOffset = faceVertices - baseVertices;

                const int totalVertices = poseGeometry->GetMesh(0).NumVertices();
                Eigen::Matrix<float, 3, -1> fullOffset = Eigen::Matrix<float, 3, -1>::Zero(3, totalVertices);
                fullOffset.leftCols(m->numFaceVertices) = faceOffset;

                poseGeometry->EvaluateBodyGeometryWithOffset(0, fullOffset, twistedJoints, rawControls, *geometryState);
            }
            else
            {
                poseGeometry->EvaluateBodyGeometry(0, twistedJoints, rawControls, *geometryState);
            }

            // Apply scale to the non-differentiable mesh copy
            Eigen::Matrix<float, 3, -1> vertices = ScaleVertices(geometryState->Vertices().Matrix(), uniformScale);
            poseGeometryMesh.SetVertices(vertices);

            if(poseGeometryMesh.Triangles().size()==0)
            {
                poseGeometryMesh.Triangulate();
            }
            poseGeometryMesh.CalculateVertexNormals(false, VertexNormalComputationType::VoronoiAreaWeighted, true, m->threadPool.get());
            // Each ICP re-scans correspondences against its own target mesh using the
            // same evaluated rig vertices + normals; per-slot SetCorrespondenceSearchVertexWeights
            // already restricts which rig verts participate.
            for (auto& icp : icps)
                icp->SetupCorrespondences(poseGeometryMesh.Vertices(), poseGeometryMesh.VertexNormals());
        }

        std::function<Cost<float>(Context<float>*)> costFunction = [&, icpIter](Context<float>* context)
        {
            Cost<float> cost;

            const DiffData<float> guiControls = useSymmetry
                ? m->symControls->EvaluateSymmetricControls(symPose.Evaluate(context))
                : pose.Evaluate(context);
            const DiffData<float> rawControls = poseLogic->EvaluateRawControls(guiControls);
            const DiffData<float> joints = poseLogic->EvaluateJoints(0, rawControls);
            const DiffData<float> rbfPsd = m->rbfLogic->EvaluatePoseControlsFromJoints(joints, true);
            const DiffData<float> rbfJoints = poseLogic->EvaluateRbfJoints(0, rbfPsd);
            const DiffData<float> twistedJoints = m->twistSwingLogic->EvaluateJointsFromJoints(joints + rbfJoints);

            if (m->facePatchBlendModel && state.m->faceState)
            {
                Eigen::Matrix<float, 3, -1> faceVertices = m->facePatchBlendModel->DeformedVertices(*state.m->faceState);
                const Eigen::Matrix<float, 3, -1>& baseVertices = m->facePatchBlendModel->BaseVertices();
                Eigen::Matrix<float, 3, -1> faceOffset = faceVertices - baseVertices;

                const int totalVertices = poseGeometry->GetMesh(0).NumVertices();
                Eigen::Matrix<float, 3, -1> fullOffset = Eigen::Matrix<float, 3, -1>::Zero(3, totalVertices);
                fullOffset.leftCols(m->numFaceVertices) = faceOffset;

                poseGeometry->EvaluateBodyGeometryWithOffset(0, fullOffset, twistedJoints, rawControls, *geometryState);
            }
            else
            {
                poseGeometry->EvaluateBodyGeometry(0, twistedJoints, rawControls, *geometryState);
            }
            
            const DiffData<float> scaleDiff = scaleVar.Evaluate(context);
            DiffDataMatrix<float, 3, -1> scaledVertices = ScaleVerticesDiff(geometryState->Vertices(), scaleDiff);

            if ((icpSchedule.start > 0.0f || icpSchedule.end > 0.0f) && hasIcp)
            {
                const float icpWeight = icpSchedule.Evaluate(icpIter, totalIters);
                for (auto& icp : icps)
                {
                    Cost<float> icpCost = icp->EvaluateICP(scaledVertices);
                    cost.Add(std::move(icpCost), icpWeight, /*average=*/true);
                }
            }

            const float keypointWeight = keypointSchedule.Evaluate(icpIter, totalIters);
            AddKeypointConstraints(cost, scaledVertices, keypointCorrespondences,
                options.vertexMask, keypointWeight);

            std::vector<Eigen::Transform<float, 3, Eigen::Affine>> scaledWorldMatrices = geometryState->GetWorldMatrices();
            Eigen::Matrix<float, -1, -1, Eigen::RowMajor> scaledJacobian = geometryState->GetWorldMatricesJacobian();
            ScaleJointMatricesDiff(scaledWorldMatrices, scaledJacobian, scaleDiff);

            // Joint mask removed from BodySolveConfiguration — pass empty so all
            // joints carry weight 1. The underlying AddJointConstraints /
            // AddViewport2DConstraints treat an empty jointMask as "no mask".
            static const Eigen::VectorXf emptyJointMask;
            AddJointConstraints(cost, scaledWorldMatrices, scaledJacobian,
                jointCorrespondences, emptyJointMask, 0.0f);

            AddViewport2DConstraints(cost, scaledVertices,
                &scaledWorldMatrices, &scaledJacobian,
                viewportConstraints2D, options.vertexMask, emptyJointMask, joint2DSchedule.Evaluate(icpIter, totalIters));

            const float landmark2DWeight = landmark2DSchedule.Evaluate(icpIter, totalIters);
            AddLandmark2DConstraints(cost, scaledVertices,
                poseGeometryMesh.VertexNormals(), landmarkConstraints2D, landmark2DWeight);

            cost.Add(gwm * guiControls, 1.0f, "gui_regularization", false);

            if (poseChainWeight > 0.0f && poseChainMatrix.rows() > 0)
                cost.Add(poseChainMatrix * guiControls, poseChainWeight, "pose_driver_chain", false);

            return cost;
        };

        GaussNewtonSolver<float>::Settings solverSettings;
        solverSettings.iterations = 1;
        solverSettings.reg = 1e-4f;
        solverSettings.maxLineSearchIterations = 10;
        solverSettings.residualErrorStoppingCriterion = 1e-3f;
        solverSettings.predictionReductionStoppingCriterion = 1e-3f;
        solver.Solve(costFunction, context, solverSettings);

        const Cost<float> costResult = costFunction(&context);
        const float currentCost = costResult.Value().squaredNorm();

        if (logCostBreakdown)
        {
            const DiffData<float> guiControlsLog = useSymmetry
                ? m->symControls->EvaluateSymmetricControls(symPose.Evaluate(&context))
                : pose.Evaluate(&context);
            const DiffData<float> rawControlsLog = poseLogic->EvaluateRawControls(guiControlsLog);
            const DiffData<float> jointsLog = poseLogic->EvaluateJoints(0, rawControlsLog);
            const DiffData<float> rbfPsdLog = m->rbfLogic->EvaluatePoseControlsFromJoints(jointsLog, true);
            const DiffData<float> rbfJointsLog = poseLogic->EvaluateRbfJoints(0, rbfPsdLog);
            const DiffData<float> twistedJointsLog = m->twistSwingLogic->EvaluateJointsFromJoints(jointsLog + rbfJointsLog);
            poseGeometry->EvaluateBodyGeometry(0, twistedJointsLog, rawControlsLog, *geometryState);
            const DiffData<float> scaleDiffLog = scaleVar.Evaluate(&context);
            DiffDataMatrix<float, 3, -1> scaledVerticesLog = ScaleVerticesDiff(geometryState->Vertices(), scaleDiffLog);

            float icpCostVal = 0.0f, kpCostVal = 0.0f, lm2dCostVal = 0.0f, regCostVal = 0.0f;
            if ((icpSchedule.start > 0.0f || icpSchedule.end > 0.0f) && hasIcp)
                for (auto& icp : icps)
                    icpCostVal += icp->EvaluateICP(scaledVerticesLog).Value().squaredNorm();
            {
                Cost<float> kpCost;
                AddKeypointConstraints(kpCost, scaledVerticesLog, keypointCorrespondences, options.vertexMask, 1.0f);
                kpCostVal = kpCost.Value().squaredNorm();
            }
            {
                Cost<float> lm2dCost;
                AddLandmark2DConstraints(lm2dCost, scaledVerticesLog, poseGeometryMesh.VertexNormals(), landmarkConstraints2D, 1.0f);
                lm2dCostVal = lm2dCost.Value().squaredNorm();
            }
            regCostVal = (gwm * guiControlsLog).Value().squaredNorm();
            LOG_INFO("[body iter {}] icp={} keypoint={} landmark2D={} reg={}",
                icpIter + 1,
                icpCostVal, kpCostVal, lm2dCostVal, regCostVal);
        }

        poseGeometryMesh.SetVertices(geometryState->Vertices().Matrix());
        if (poseGeometryMesh.Triangles().size() == 0)
        {
            poseGeometryMesh.Triangulate();
        }
        poseGeometryMesh.CalculateVertexNormals(false, VertexNormalComputationType::VoronoiAreaWeighted, true, m->threadPool.get());

        // Extract optimized scale
        uniformScale = scaleVar.Value()[0];
        state.m->ScaleFactor = uniformScale;
        LOG_INFO("Optimized scale: {}", uniformScale);

        // Log proportion control changes
        if (!m->localSkeletonIndices.empty())
        {
            const Eigen::VectorXf& curPose = useSymmetry ? symPose.Value() : pose.Value();
            float maxProportionDelta = 0.0f;
            for (int idx : m->localSkeletonIndices)
                maxProportionDelta = std::max(maxProportionDelta, std::abs(curPose[idx] - prevPose[idx]));
            LOG_INFO("  Proportion controls max delta: {}", maxProportionDelta);
            prevPose = curPose;
        }

        DiffDataMatrix<float, 3, -1> scaledVertices = ScaleVertices(geometryState->Vertices(), uniformScale);
        std::vector<Eigen::Transform<float, 3, Eigen::Affine>> scaledWorldMatrices = geometryState->GetWorldMatrices();
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> scaledJacobian = geometryState->GetWorldMatricesJacobian();
        ScaleJointMatrices(scaledWorldMatrices, scaledJacobian, uniformScale);
        iterationFunc(scaledVertices.Matrix(), poseGeometryMesh.VertexNormals(), icpIter, currentCost, scaledWorldMatrices);

        if (icpIter >= 3)
        {
            const float relativeCostChange = std::abs(currentCost - previousCost) / (previousCost + 1e-10f);
            if (relativeCostChange < 1e-3f)
            {
                LOG_INFO("ICP converged at iteration {} (relative cost change: {})", icpIter + 1, relativeCostChange);
                break;
            }
        }

        previousCost = currentCost;
    }

    state.m->JointDeltas.setZero();
    if (useSymmetry)
    {
        state.m->GuiControls = m->symControls->SymmetricToGuiControls(symPose.Value());
    }
    else
    {
        state.m->GuiControls = pose.Value();
    }
    // Check for solver divergence (NaN or huge values)
    if (!state.m->GuiControls.allFinite() || state.m->GuiControls.cwiseAbs().maxCoeff() > 1e6f)
    {
        LOG_ERROR("SolveForArbitraryMeshWithICP: solver diverged (max control={}) — reverting to prior",
            state.m->GuiControls.cwiseAbs().maxCoeff());
        state.m->GuiControls = state.m->GuiControlsPrior;
    }
    state.m->GuiControlsPrior = state.m->GuiControls;
    EvaluateState(state);

    LOG_INFO("SolveForArbitraryMeshWithICP completed in {} ms", solveTimer.Current());

    // Seam adaptation is now an explicit pipeline step (StepKind::AdaptNeck),
    // no longer auto-invoked here.

    return state.m->GuiControls;
}

void BodyShapeEditor::SolveFace(
    State& state,
    const BodyShapeEditorTarget& target,
    const BodySolveConfiguration& options,
    IterationFunc iterationFunc)
{
    if (!m->facePatchBlendModel)
    {
        LOG_WARNING("SolveFace: No face PatchBlendModel set");
        return;
    }

    if (!state.m->faceState)
    {
        state.m->faceState = m->facePatchBlendModel->CreateState();
    }

    auto faceOptState = m->facePatchBlendModel->CreateOptimizationState();
    faceOptState.CopyFromState(*state.m->faceState);

    auto targetMeshPtr = target.MeshFor(BodyShapeEditorTarget::MeshSlot::Head);
    if (!targetMeshPtr)
    {
        LOG_WARNING("SolveFace: No target mesh provided");
        return;
    }

    float uniformScale = state.m->ScaleFactor;
    const Mesh<float>& targetMesh = *targetMeshPtr;
    const auto& keypointCorrespondences = target.Keypoints();
    const auto& viewportConstraints2D = target.Viewports();
    auto landmarks2DShared = target.Landmarks2D();
    auto* landmarkConstraints2D = landmarks2DShared.get();
    if (landmarkConstraints2D)
    {
        Configuration lmConfig = landmarkConstraints2D->GetConfiguration();
        lmConfig["landmarksWeight"] = options.face["landmark2D"].Value<float>();
        lmConfig["innerLipWeight"] = 0.0f;
        lmConfig["curveResampling"] = options.face["curveResampling"].Value<int>();
        landmarkConstraints2D->SetConfiguration(lmConfig);
    }

    WeightSchedule faceNormalCompatSchedule = LoadWeightSchedule(options.face, "normalCompat");

    std::unique_ptr<ICPConstraints<float>> icpConstraints;       // head-side
    std::unique_ptr<ICPConstraints<float>> bodyIcpConstraints;   // body-side (optional)
    std::optional<Configuration> faceIcpConfig;
    if ((options.face["icp"].Value<float>() > 0.0f))
    {
        icpConstraints = std::make_unique<ICPConstraints<float>>();
        icpConstraints->SetTargetMesh(targetMesh);
        if (target.NonDegenerateMaskFor(BodyShapeEditorTarget::MeshSlot::Head).size() == (int)targetMesh.NumVertices())
            icpConstraints->SetTargetWeights(target.NonDegenerateMaskFor(BodyShapeEditorTarget::MeshSlot::Head));

        faceIcpConfig = icpConstraints->GetConfiguration();
        (*faceIcpConfig)["geometryWeight"] = options.face["icp"].Value<float>();
        (*faceIcpConfig)["point2point"] = 0.0f;
        (*faceIcpConfig)["normalIncompatibilityThreshold"] = faceNormalCompatSchedule.start;
        (*faceIcpConfig)["minimumDistanceThreshold"] = options.face["icpTol"].Value<float>();
        icpConstraints->SetConfiguration(*faceIcpConfig);

        // Use the face-region slice of the single vertexMask as the ICP weight.
        // Callers wanting to combine a shipped face-ICP-validity mask with a
        // UI mask should multiply them into vertexMask before calling.
        if (options.vertexMask.size() >= m->numFaceVertices)
        {
            Eigen::VectorXf faceMask = options.vertexMask.head(m->numFaceVertices);
            icpConstraints->SetCorrespondenceSearchVertexWeights(VertexWeights<float>(faceMask));
            LOG_INFO("SolveFace: Applied head-ICP mask ({} active / {} verts)",
                     (faceMask.array() > 0).count(), m->numFaceVertices);
        }

        // Body-side ICP — engaged when the target has an explicit Body slot
        // (so the face solve also fits body-region rig verts to the body
        // scan). Mirror behaviour of SolveForArbitraryMeshWithICP's dual
        // path: body verts → Body mesh, face verts → Head mesh, regional
        // mask separates the two on the rig side.
        if (target.HasMesh(BodyShapeEditorTarget::MeshSlot::Body))
        {
            const auto bodyTargetPtr = target.MeshFor(BodyShapeEditorTarget::MeshSlot::Body);
            const Mesh<float>& bodyTarget = *bodyTargetPtr;
            bodyIcpConstraints = std::make_unique<ICPConstraints<float>>();
            bodyIcpConstraints->SetTargetMesh(bodyTarget);
            if (target.NonDegenerateMaskFor(BodyShapeEditorTarget::MeshSlot::Body).size() == (int)bodyTarget.NumVertices())
                bodyIcpConstraints->SetTargetWeights(target.NonDegenerateMaskFor(BodyShapeEditorTarget::MeshSlot::Body));

            Configuration bodyIcpConfig = bodyIcpConstraints->GetConfiguration();
            bodyIcpConfig["geometryWeight"] = options.face["icp"].Value<float>();
            bodyIcpConfig["point2point"] = 0.0f;
            bodyIcpConfig["normalIncompatibilityThreshold"] = faceNormalCompatSchedule.start;
            bodyIcpConfig["minimumDistanceThreshold"] = options.face["icpTol"].Value<float>();
            bodyIcpConstraints->SetConfiguration(bodyIcpConfig);

            const int totalRigVerts = m->rigGeometry->GetMesh(0).NumVertices();
            const int faceEnd       = std::min(m->numFaceVertices, totalRigVerts);
            Eigen::VectorXf bodyRegionMask = Eigen::VectorXf::Zero(totalRigVerts);
            if (options.vertexMask.size() == totalRigVerts)
                bodyRegionMask.tail(totalRigVerts - faceEnd) = options.vertexMask.tail(totalRigVerts - faceEnd);
            else
                bodyRegionMask.tail(totalRigVerts - faceEnd).setOnes();
            bodyIcpConstraints->SetCorrespondenceSearchVertexWeights(VertexWeights<float>(bodyRegionMask));
            LOG_INFO("SolveFace: Applied body-ICP ({} body verts active / {})",
                     (int)(bodyRegionMask.array() > 0.0f).count(), totalRigVerts);
        }
    }

    auto poseGeometry = m->rigGeometry->Clone();
    auto poseLogic = m->poseLogic->Clone();
    auto geometryState = m->StatePoolJacobian.Aquire();

    const Eigen::VectorXf bodyGuiControls = state.m->GuiControls;

    // Body pose Variable locked to the current GuiControls. Locked so the
    // face solver can't step on body controls; Variable so that, when
    // re-evaluated inside the cost lambda with `context`, the body chain
    // (raw → joints → rbf → twist) carries a (zero-filled) Jacobian with
    // valid column bounds. EvaluateBodyGeometryWithOffset's Jacobian-aware
    // path inspects diffJoints.Jacobian() column bounds to size joint
    // Jacobian buffers — if joints have no Jacobian while offset does, the
    // sizing collapses. The lock keeps the gradient contribution at zero
    // for body controls while preserving the column geometry.
    BoundedVectorVariable<float> bodyPose{ bodyGuiControls };
    {
        std::vector<int> allIndices(bodyGuiControls.size());
        std::iota(allIndices.begin(), allIndices.end(), 0);
        bodyPose.MakeIndividualIndicesConstant(allIndices);
    }

    // Outer-scope chain (no Jacobian) — used by:
    //   - the initial EvaluateBodyGeometry below to seed `currentMesh`,
    //   - the per-iter ICP-correspondence snapshot,
    //   - the cost-breakdown log path.
    // All three call into BodyGeometry without a solver context (PBM has no
    // Jacobian either there), so requiresJacobians collapses to false and
    // the Jacobian-less BodyGeometry path is taken.
    const DiffData<float> bodyRawControls = poseLogic->EvaluateRawControls(DiffData<float>(bodyGuiControls));
    const DiffData<float> bodyJoints = poseLogic->EvaluateJoints(0, bodyRawControls);
    const DiffData<float> bodyRbfPsd = m->rbfLogic->EvaluatePoseControlsFromJoints(bodyJoints, true);
    const DiffData<float> bodyRbfJoints = poseLogic->EvaluateRbfJoints(0, bodyRbfPsd);
    const DiffData<float> bodyTwistedJoints = m->twistSwingLogic->EvaluateJointsFromJoints(bodyJoints + bodyRbfJoints);

    poseGeometry->EvaluateBodyGeometry(0, bodyTwistedJoints, bodyRawControls, *geometryState);
    const Eigen::Matrix<float, 3, -1> bodySkinnedVertices = geometryState->Vertices().Matrix();
    const int totalVertices = static_cast<int>(bodySkinnedVertices.cols());

    // Apply scale to vertices for ICP correspondence setup
    Eigen::Matrix<float, 3, -1> scaledBodyVertices = ScaleVertices(bodySkinnedVertices, uniformScale);
    
    Mesh<float> currentMesh;
    currentMesh.SetTriangles(m->meshTriangles[0]);
    currentMesh.SetVertices(scaledBodyVertices);
    currentMesh.Triangulate();
    currentMesh.CalculateVertexNormals(false, VertexNormalComputationType::VoronoiAreaWeighted, true, m->threadPool.get());

    // Body ICP correspondences are set once: face solve only moves face-region
    // verts; body verts stay at scaledBodyVertices for the whole solve.
    if (bodyIcpConstraints)
        bodyIcpConstraints->SetupCorrespondences(currentMesh.Vertices(), currentMesh.VertexNormals());


    int iteration = 0;
    const int faceTotalIters = options.face["iterations"].Value<int>();

    WeightSchedule faceModelRegSchedule    = LoadWeightSchedule(options.face, "modelRegularization");
    WeightSchedule facePatchSmoothSchedule = LoadWeightSchedule(options.face, "patchSmoothness");
    WeightSchedule faceIcpSchedule         = LoadWeightSchedule(options.face, "icp");
    WeightSchedule faceKeypointSchedule    = LoadWeightSchedule(options.face, "keypoint");
    WeightSchedule faceLandmark2DSchedule  = LoadWeightSchedule(options.face, "landmark2D");
    WeightSchedule faceToleranceSchedule   = LoadWeightSchedule(options.face, "icpTol");

    float iterModelReg    = faceModelRegSchedule.start;
    float iterPatchSmooth = facePatchSmoothSchedule.start;

    const bool faceLogCostBreakdown = options.face["logCostBreakdown"].Value<bool>();

    int faceOuterIter = 0;

    // Promote the PBM's face-vertex DiffData into a full-body-sized bind-space
    // offset (zero on body rows, (PBM_def − PBM_base) on face rows) so that
    // BodyGeometry::EvaluateBodyGeometryWithOffset can run the body's
    // joint-deform + skin pipeline on it. Result: the face delta is properly
    // skinned into world space AND the Jacobian (PBM controls → bind delta)
    // is composed with the skin Jacobian → cost gradient lives in world space.
    //
    // Old code: `finalVertices = bodySkinned + (PBM_def − PBM_base)` glued
    // a bind-space delta onto already-skinned world-space verts and copied
    // the bind-frame Jacobian into the cost — only correct when every face
    // vertex's skin matrix is identity (head at rest, no scale, no RBF
    // contribution). Non-trivial body pose broke it silently.
    auto buildBodyOffsetWithFaceBlock = [&](const DiffDataMatrix<float, 3, -1>& faceVerts) -> DiffDataMatrix<float, 3, -1>
    {
        const Eigen::Matrix<float, 3, -1>& baseVerts = m->facePatchBlendModel->BaseVertices();
        Eigen::Matrix<float, 3, -1> offsetVals = Eigen::Matrix<float, 3, -1>::Zero(3, totalVertices);
        offsetVals.leftCols(m->numFaceVertices) =
            faceVerts.Matrix().leftCols(m->numFaceVertices) - baseVerts.leftCols(m->numFaceVertices);

        if (!faceVerts.HasJacobian())
            return DiffDataMatrix<float, 3, -1>(offsetVals);

        const int startCol = faceVerts.Jacobian().StartCol();
        const int numCols = faceVerts.Jacobian().Cols() - startCol;
        auto fullJac = std::make_shared<Eigen::Matrix<float, -1, -1, Eigen::RowMajor>>(
            3 * totalVertices, numCols);
        fullJac->setZero();
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> faceJacDense(3 * m->numFaceVertices, numCols);
        faceVerts.Jacobian().CopyToDenseMatrix(faceJacDense);
        fullJac->topRows(3 * m->numFaceVertices) = faceJacDense;
        return DiffDataMatrix<float, 3, -1>(offsetVals, std::make_shared<DenseJacobian<float>>(fullJac, startCol));
    };

    std::function<Cost<float>(Context<float>*)> faceCostFunction = [&](Context<float>* context)
    {
        Cost<float> cost;

        auto [faceVertices, faceCost] = m->facePatchBlendModel->EvaluateVerticesAndConstraints(
            context, faceOptState, iterModelReg, iterPatchSmooth);

        // Re-evaluate the body chain inside the lambda with the active
        // context so joints/raw carry a Jacobian (all-zero rows, since the
        // body Variable is fully locked) with column bounds registered on
        // `context`. EvaluateBodyGeometryWithOffset's Jacobian-aware path
        // needs that to size joint Jacobian buffers correctly when the
        // offset Jacobian is the only real gradient source.
        const DiffData<float> guiCtrl = bodyPose.Evaluate(context);
        const DiffData<float> rawCtrl = poseLogic->EvaluateRawControls(guiCtrl);
        const DiffData<float> jts     = poseLogic->EvaluateJoints(0, rawCtrl);
        const DiffData<float> rbfPsd  = m->rbfLogic->EvaluatePoseControlsFromJoints(jts, true);
        const DiffData<float> rbfJts  = poseLogic->EvaluateRbfJoints(0, rbfPsd);
        const DiffData<float> twJts   = m->twistSwingLogic->EvaluateJointsFromJoints(jts + rbfJts);

        // Build the bind-space offset and run the body pipeline on it. The
        // output lives on `geometryState->Vertices()` in body world space
        // with the full PBM → bind-delta → skin-deform Jacobian composed in.
        // DiffDataMatrix is move-only — consume `geometryState->Vertices()`
        // via const-ref into ScaleVertices directly, no local copy.
        DiffDataMatrix<float, 3, -1> offsetDiff = buildBodyOffsetWithFaceBlock(faceVertices);
        poseGeometry->EvaluateBodyGeometryWithOffset(0, offsetDiff, twJts, rawCtrl, *geometryState);

        if (context)
        {
            // Scale vertices and joint matrices for iteration callback
            Eigen::Matrix<float, 3, -1> scaledIterationVertices = ScaleVertices(geometryState->Vertices().Matrix(), uniformScale);
            std::vector<Eigen::Transform<float, 3, Eigen::Affine>> scaledWorldMatrices = geometryState->GetWorldMatrices();
            ScaleJointMatrices(scaledWorldMatrices, uniformScale);

            Mesh<float> IterationMesh;
            IterationMesh.SetTriangles(m->meshTriangles[0]);
            IterationMesh.SetVertices(scaledIterationVertices);
            IterationMesh.CalculateVertexNormals(true, VertexNormalComputationType::AreaWeighted, true, m->threadPool.get());
            iterationFunc(IterationMesh.Vertices(), IterationMesh.VertexNormals(), iteration++, faceCost.Value().squaredNorm(), scaledWorldMatrices);
        }

        // Scale the vertices for constraints
        DiffDataMatrix<float, 3, -1> scaledFinalVertices = ScaleVertices(geometryState->Vertices(), uniformScale);

        // ICP cost — head target on face-region rig verts, plus body target
        // on body-region verts when Body slot is set.
        if ((faceIcpSchedule.start > 0.0f || faceIcpSchedule.end > 0.0f))
        {
            const float faceIcpWeight = faceIcpSchedule.Evaluate(faceOuterIter, faceTotalIters);
            if (icpConstraints)
            {
                Cost<float> icpCost = icpConstraints->EvaluateICP(scaledFinalVertices);
                cost.Add(std::move(icpCost), faceIcpWeight, /*average=*/true);
            }
            if (bodyIcpConstraints)
            {
                Cost<float> bodyIcpCost = bodyIcpConstraints->EvaluateICP(scaledFinalVertices);
                cost.Add(std::move(bodyIcpCost), faceIcpWeight, /*average=*/true);
            }
        }

        const float faceKeypointWeight = faceKeypointSchedule.Evaluate(faceOuterIter, faceTotalIters);
        AddKeypointConstraints(cost, scaledFinalVertices, keypointCorrespondences,
            Eigen::VectorXf(), faceKeypointWeight, m->numFaceVertices, "face_keypoints");

        AddViewport2DConstraints(cost, scaledFinalVertices, nullptr, nullptr,
            viewportConstraints2D, Eigen::VectorXf(), Eigen::VectorXf(),
            0.0f, m->numFaceVertices);

        const float faceLandmark2DWeight = faceLandmark2DSchedule.Evaluate(faceOuterIter, faceTotalIters);
        AddLandmark2DConstraints(cost, scaledFinalVertices,
            currentMesh.VertexNormals(), landmarkConstraints2D, faceLandmark2DWeight);


        cost.Add(std::move(faceCost), 1.0f, true);
        return cost;
    };

    faceOptState.SetOptimizeScale(true);

    GaussNewtonSolver<float> faceSolver;
    Context<float> faceContext;

    GaussNewtonSolver<float>::Settings faceSettings;
    faceSettings.iterations = 1;
    faceSettings.reg = options.face["lmDamping"].Value<float>();

    // Build face-only mesh for per-iteration ICP update
    std::vector<Eigen::Vector3i> faceOnlyTris;
    for (int t = 0; t < static_cast<int>(m->meshTriangles[0].cols()); ++t)
    {
        const auto& tri = m->meshTriangles[0].col(t);
        if (tri[0] < m->numFaceVertices && tri[1] < m->numFaceVertices && tri[2] < m->numFaceVertices)
            faceOnlyTris.push_back(tri);
    }
    Eigen::Matrix<int, 3, -1> faceTriMatrix(3, faceOnlyTris.size());
    for (size_t i = 0; i < faceOnlyTris.size(); ++i)
        faceTriMatrix.col(i) = faceOnlyTris[i];
    Mesh<float> faceMesh;
    faceMesh.SetTriangles(std::move(faceTriMatrix));

    Timer faceTimer;
    for (faceOuterIter = 0; faceOuterIter < faceTotalIters; ++faceOuterIter)
    {
        iterModelReg    = faceModelRegSchedule.Evaluate(faceOuterIter, faceTotalIters);
        iterPatchSmooth = facePatchSmoothSchedule.Evaluate(faceOuterIter, faceTotalIters);
        LOG_INFO("Face iter {}/{}: icp={} kp={} lm2d={} modelReg={} patchSmooth={} lmDamp={}",
            faceOuterIter + 1, faceTotalIters,
            faceIcpSchedule.Evaluate(faceOuterIter, faceTotalIters),
            faceKeypointSchedule.Evaluate(faceOuterIter, faceTotalIters),
            faceLandmark2DSchedule.Evaluate(faceOuterIter, faceTotalIters),
            iterModelReg, iterPatchSmooth, options.face["lmDamping"].Value<float>());

        // Update ICP correspondences with current face vertices. Snapshot only —
        // no Jacobian needed — so we skip the context. Route through the same
        // offset-pipeline path the cost lambda uses so the body skin composes
        // the face delta into world space (matters for non-identity head pose).
        auto [currentFaceVerts, _] = m->facePatchBlendModel->EvaluateVerticesAndConstraints(
            nullptr, faceOptState, iterModelReg, iterPatchSmooth);
        DiffDataMatrix<float, 3, -1> snapshotOffset = buildBodyOffsetWithFaceBlock(currentFaceVerts);
        poseGeometry->EvaluateBodyGeometryWithOffset(0, snapshotOffset, bodyTwistedJoints, bodyRawControls, *geometryState);
        Eigen::Matrix<float, 3, -1> faceVerts = geometryState->Vertices().Matrix().leftCols(m->numFaceVertices);
        faceVerts *= uniformScale;

        faceMesh.SetVertices(faceVerts);
        faceMesh.CalculateVertexNormals(false, VertexNormalComputationType::AreaWeighted, true, m->threadPool.get());

        if (icpConstraints && faceIcpConfig)
        {
            (*faceIcpConfig)["minimumDistanceThreshold"]      = faceToleranceSchedule.Evaluate(faceOuterIter, faceTotalIters);
            (*faceIcpConfig)["normalIncompatibilityThreshold"] = faceNormalCompatSchedule.Evaluate(faceOuterIter, faceTotalIters);
            icpConstraints->SetConfiguration(*faceIcpConfig);
            icpConstraints->SetupCorrespondences(faceMesh.Vertices(), faceMesh.VertexNormals());
        }

        faceSolver.Solve(faceCostFunction, faceContext, faceSettings);

        if (faceLogCostBreakdown)
        {
            auto [faceVertsLog, _faceRegCost] = m->facePatchBlendModel->EvaluateVerticesAndConstraints(
                nullptr, faceOptState, iterModelReg, iterPatchSmooth);
            DiffDataMatrix<float, 3, -1> logOffset = buildBodyOffsetWithFaceBlock(faceVertsLog);
            poseGeometry->EvaluateBodyGeometryWithOffset(0, logOffset, bodyTwistedJoints, bodyRawControls, *geometryState);
            DiffDataMatrix<float, 3, -1> scaledVertsLog = ScaleVertices(geometryState->Vertices(), uniformScale);

            float faceIcpCostVal = 0.0f, faceKpCostVal = 0.0f, faceLm2dCostVal = 0.0f;
            if ((faceIcpSchedule.start > 0.0f || faceIcpSchedule.end > 0.0f) && icpConstraints)
                faceIcpCostVal = icpConstraints->EvaluateICP(scaledVertsLog).Value().squaredNorm();
            {
                Cost<float> kpCost;
                AddKeypointConstraints(kpCost, scaledVertsLog, keypointCorrespondences, Eigen::VectorXf(), 1.0f, m->numFaceVertices, "face_keypoints");
                faceKpCostVal = kpCost.Value().squaredNorm();
            }
            {
                Cost<float> lm2dCost;
                AddLandmark2DConstraints(lm2dCost, scaledVertsLog, currentMesh.VertexNormals(), landmarkConstraints2D, 1.0f);
                faceLm2dCostVal = lm2dCost.Value().squaredNorm();
            }
            LOG_INFO("[face iter {}] icp={} keypoint={} landmark2D={}",
                faceOuterIter + 1,
                faceIcpCostVal, faceKpCostVal, faceLm2dCostVal);
        }

        faceOptState.BakeRotationLinearization();
    }
    LOG_INFO("SolveFace completed in {} ms ({} iters)", faceTimer.Current(), options.face["iterations"].Value<int>());

    faceOptState.CopyToState(*state.m->faceState);
    EvaluateState(state);

    // Seam adaptation is now an explicit pipeline step (StepKind::AdaptNeck),
    // no longer auto-invoked here.
}
void BodyShapeEditor::RunPipeline(
    State& state,
    const BodyShapeEditorTarget& target,
    const BodySolveConfiguration& defaultConfig,
    const std::vector<SolveStep>& steps,
    IterationFunc iterationFunc,
    std::function<void(int, int, const std::string&)> progressFunc)
{
    const int totalSteps = static_cast<int>(steps.size());

    // Helper: build a per-step target that honours the step's explicit targetSlot.
    //
    //   • targetSlot == Combined (the "whole thing" slot)
    //       - If the outer target has Combined, use just that.
    //       - Else if it has both Head and Body, pass BOTH through so downstream
    //         solvers run their dual-ICP / dual-search path against them.
    //       - Else degenerate to whatever single slot is available.
    //   • targetSlot == Head or Body
    //       - Pass through ONLY that slot. A step that asks for Head must not be
    //         silently fitted to a body scan, and vice-versa.
    //
    // Fallback: if the requested slot(s) aren't populated, return the outer target
    // verbatim so the solver at least has something to work with.
    auto buildStepTarget = [&](const SolveStep& s) -> BodyShapeEditorTarget
    {
        using MeshSlot = BodyShapeEditorTarget::MeshSlot;
        BodyShapeEditorTarget t = target;
        if (!target.HasAnyMesh()) return t;

        BodyShapeEditorTarget newT;
        if (s.targetSlot == MeshSlot::Combined)
        {
            if (target.HasMesh(MeshSlot::Combined))
            {
                newT.SetMesh(MeshSlot::Combined, target.MeshFor(MeshSlot::Combined));
            }
            else if (target.HasMesh(MeshSlot::Head) && target.HasMesh(MeshSlot::Body))
            {
                newT.SetMesh(MeshSlot::Head, target.MeshFor(MeshSlot::Head));
                newT.SetMesh(MeshSlot::Body, target.MeshFor(MeshSlot::Body));
            }
            else if (target.HasMesh(MeshSlot::Head))
                newT.SetMesh(MeshSlot::Head, target.MeshFor(MeshSlot::Head));
            else if (target.HasMesh(MeshSlot::Body))
                newT.SetMesh(MeshSlot::Body, target.MeshFor(MeshSlot::Body));
        }
        else  // explicit Head or Body: only that slot
        {
            if (target.HasMesh(s.targetSlot))
                newT.SetMesh(s.targetSlot, target.MeshFor(s.targetSlot));
        }

        if (!newT.HasAnyMesh())
            return t;  // fallback keeps the step runnable even with an orphan slot

        for (const auto& [v, p] : target.Keypoints()) newT.AddKeypoint(v, p);
        for (const auto& [v, p] : target.Joints())    newT.AddJointCorrespondence(v, p);
        newT.SetLandmarks2D(target.Landmarks2D());
        for (const auto& vp : target.Viewports()) newT.AddViewportConstraints(vp.first, vp.second);
        return newT;
    };

    for (int stepIdx = 0; stepIdx < totalSteps; ++stepIdx)
    {
        const auto& step = steps[stepIdx];
        if (!step.enabled) continue;

        // Build the BodySolveConfiguration the solver API still expects, from
        // the step's single Configuration + vertexMask. Fill only the sub-slot
        // matching the step kind; the other slots stay default (unused).
        BodySolveConfiguration stepConfig = defaultConfig;  // keeps defaults for unused slots
        switch (step.kind)
        {
            case StepKind::BodySolve: stepConfig.body       = step.config; break;
            case StepKind::FaceSolve: stepConfig.face       = step.config; break;
            case StepKind::Refine:    stepConfig.refinement = step.config; break;
            default: break;  // Align / AdaptNeck don't use config
        }
        stepConfig.vertexMask = step.vertexMask.size() > 0 ? step.vertexMask : defaultConfig.vertexMask;
        stepConfig.lockScale  = step.lockScale;

        const auto& lockedControls = step.lockedControls;
        if (!lockedControls.empty())
            state.SetLockedControlIndices(lockedControls);
        else
            state.ClearLockedControls();

        if (!step.initialControls.empty())
        {
            Eigen::VectorXf controls = state.GetGuiControls();
            const auto& names = m->poseLogic->GuiControlNames();
            for (const auto& [name, value] : step.initialControls)
            {
                auto it = std::find(names.begin(), names.end(), name);
                if (it != names.end())
                    controls[static_cast<int>(std::distance(names.begin(), it))] = value;
                else
                    LOG_WARNING("Pipeline step {}: unknown control '{}'", stepIdx + 1, name);
            }
            state.SetGuiControls(controls);
        }

        BodyShapeEditorTarget stepTarget = buildStepTarget(step);

        // Diagnostic: what did buildStepTarget produce for this step? Lets us see
        // which mesh slots the step's chosen targetSlot actually resolved to after
        // priority + fallback rules.
        {
            auto nv = [&](BodyShapeEditorTarget::MeshSlot s) {
                return stepTarget.HasMesh(s) ? (int)stepTarget.MeshFor(s)->NumVertices() : 0;
            };
            LOG_INFO("Pipeline step {}/{} '{}' kind={} requestedSlot={} -> stepTarget slots=[head={}v body={}v combined={}v] lockScale={} maskedVerts={}",
                     stepIdx + 1, totalSteps, step.name, (int)step.kind, (int)step.targetSlot,
                     nv(BodyShapeEditorTarget::MeshSlot::Head),
                     nv(BodyShapeEditorTarget::MeshSlot::Body),
                     nv(BodyShapeEditorTarget::MeshSlot::Combined),
                     step.lockScale,
                     stepConfig.vertexMask.size() > 0
                        ? (int)(stepConfig.vertexMask.array() > 0.0f).count()
                        : 0);
        }

        switch (step.kind)
        {
            case StepKind::Align:
                progressFunc(stepIdx + 1, totalSteps, "Align");
                AlignToTargetMesh(state, stepTarget);
                break;
            case StepKind::BodySolve:
                progressFunc(stepIdx + 1, totalSteps, "Body Solve");
                SolveForArbitraryMeshWithICP(state, stepTarget, stepConfig, lockedControls, iterationFunc);
                break;
            case StepKind::FaceSolve:
                progressFunc(stepIdx + 1, totalSteps, "Face Solve");
                SolveFace(state, stepTarget, stepConfig, iterationFunc);
                break;
            case StepKind::AdaptNeck:
                progressFunc(stepIdx + 1, totalSteps, "Adapt Neck Seam");
                AdaptNeckSeam(state, step.seamLaplacian, step.seamRings, step.seamIterations, step.seamLockSide);
                break;
            case StepKind::Refine:
                progressFunc(stepIdx + 1, totalSteps, "Refine Vertices");
                RefineVertices(state, stepTarget, stepConfig);
                break;
        }

        LOG_INFO("Pipeline step {} completed", stepIdx + 1);

        // Diagnostic: spot-check the rigid-root/pelvis controls after each step so we can tell
        // whether a later step (or the outer EvaluateState) wipes an Align's work.
        {
            const auto& names = m->poseLogic->GuiControlNames();
            auto readC = [&](const char* n) -> float {
                auto it = std::find(names.begin(), names.end(), std::string(n));
                return it == names.end() ? std::numeric_limits<float>::quiet_NaN()
                                         : state.GetGuiControls()[int(it - names.begin())];
            };
            LOG_INFO("PostStep{}: root=({},{},{}) pelvis=({},{},{})", stepIdx + 1,
                     readC("pose_rigid_root.tx"), readC("pose_rigid_root.ty"), readC("pose_rigid_root.tz"),
                     readC("pose_rigid_pelvis.rx"), readC("pose_rigid_pelvis.ry"), readC("pose_rigid_pelvis.rz"));
        }
    }

    EvaluateState(state);

    // Diagnostic: controls after the outer EvaluateState — these are what the app will render.
    {
        const auto& names = m->poseLogic->GuiControlNames();
        auto readC = [&](const char* n) -> float {
            auto it = std::find(names.begin(), names.end(), std::string(n));
            return it == names.end() ? std::numeric_limits<float>::quiet_NaN()
                                     : state.GetGuiControls()[int(it - names.begin())];
        };
        LOG_INFO("RunPipeline: FINAL root=({},{},{}) pelvis=({},{},{})",
                 readC("pose_rigid_root.tx"), readC("pose_rigid_root.ty"), readC("pose_rigid_root.tz"),
                 readC("pose_rigid_pelvis.rx"), readC("pose_rigid_pelvis.ry"), readC("pose_rigid_pelvis.rz"));
    }
}

void BodyShapeEditor::RefineVertices(
    State& state,
    const BodyShapeEditorTarget& target,
    const BodySolveConfiguration& solveConfig)
{
    const auto& refinementSettings = solveConfig.refinement;
    auto targetMeshPtr = target.PrimaryMesh();
    if (!targetMeshPtr)
    {
        LOG_WARNING("RefineVertices: No target mesh provided");
        return;
    }
    float uniformScale = state.m->ScaleFactor;
    const Mesh<float>& targetMesh = *targetMeshPtr;

    auto geometryState = m->StatePoolJacobian.Aquire();
    const DiffData<float> rawControls = m->poseLogic->EvaluateRawControls(state.m->GuiControls);
    const DiffData<float> joints = m->poseLogic->EvaluateJoints(0, rawControls);
    const DiffData<float> rbfPsd = m->rbfLogic->EvaluatePoseControlsFromJoints(joints, true);
    const DiffData<float> rbfJoints = m->poseLogic->EvaluateRbfJoints(0, rbfPsd);
    const DiffData<float> twistedJoints = m->twistSwingLogic->EvaluateJointsFromJoints(joints + rbfJoints);

    const int totalVertices = m->rigGeometry->GetMesh(0).NumVertices();
    Eigen::Matrix<float, 3, -1> fullOffset = Eigen::Matrix<float, 3, -1>::Zero(3, totalVertices);

    // Bake the existing VertexDeltas into the rest at full strength (i.e.
    // as if VertexDeltaScale = 1) so refinement is deterministic w.r.t. the
    // runtime slider. The slider's job is post-evaluation attenuation; the
    // refiner always operates on the "full" VD and writes the full result
    // back. Without this, refining at slider=0.5 stored 2× the residual,
    // and bumping the slider back to 1 over-shot the fit.
    //
    // We still need the laplacian / strain / bending terms to see the prior
    // VD, otherwise they pull the optimisation back to the un-deltaed mesh
    // and erase prior refinements wherever the fit term is weak (mask=0 or
    // far-from-scan).
    const bool haveVD = (state.m->VertexDeltas.cols() == totalVertices);
    if (haveVD)
        fullOffset += state.m->VertexDeltas;

    if (m->facePatchBlendModel && state.m->faceState)
    {
        const Eigen::Matrix<float, 3, -1> faceOffset = m->facePatchBlendModel->DeformedVertices(*state.m->faceState) - m->facePatchBlendModel->BaseVertices();
        fullOffset.leftCols(m->numFaceVertices) += faceOffset;
    }

    if (state.m->BodySeamDelta.cols() == totalVertices)
        fullOffset += state.m->BodySeamDelta;

    m->rigGeometry->EvaluateBodyGeometryWithOffset(0, fullOffset, twistedJoints, rawControls, *geometryState);

    Eigen::Matrix<float, 3, -1> scaledVertices = ScaleVertices(geometryState->Vertices().Matrix(), uniformScale);

    Mesh<float> poseGeometryMesh = m->rigGeometry->GetMesh(0);
    poseGeometryMesh.Triangulate();
    poseGeometryMesh.SetVertices(scaledVertices);
    poseGeometryMesh.CalculateVertexNormals(false, VertexNormalComputationType::VoronoiAreaWeighted, true, m->threadPool.get());

    const int numVertices = int(geometryState->Vertices().Cols());

    // Find closest points on target mesh for each vertex.
    // Resolution mirrors SolveForArbitraryMeshWithICP:
    //   • Combined slot set           → single search against Combined
    //   • else Head AND Body          → two searches, face-region rig verts route to
    //                                    the Head search, body-region to the Body search
    //   • else                        → single search against PrimaryMesh
    using MeshSlot = BodyShapeEditorTarget::MeshSlot;
    Eigen::Matrix<float, 3, -1> initialTargetPositions = scaledVertices;
    {
        const float icpTol = refinementSettings["icpTol"].Value<float>();

        const bool dualHeadBody = !target.HasMesh(MeshSlot::Combined)
                                   && target.HasMesh(MeshSlot::Head)
                                   && target.HasMesh(MeshSlot::Body);

        MeshCorrespondenceSearch<float> headSearch, bodySearch, singleSearch;
        const Mesh<float>* headMeshPtr = nullptr;
        const Mesh<float>* bodyMeshPtr = nullptr;
        const Mesh<float>* singleMeshPtr = nullptr;

        if (dualHeadBody)
        {
            headMeshPtr = target.MeshFor(MeshSlot::Head).get();
            bodyMeshPtr = target.MeshFor(MeshSlot::Body).get();
            headSearch.Init(*headMeshPtr);
            bodySearch.Init(*bodyMeshPtr);
            LOG_INFO("RefineVertices: dual-head+body search (headVerts={} bodyVerts={} faceCut={})",
                     (int)headMeshPtr->NumVertices(), (int)bodyMeshPtr->NumVertices(), m->numFaceVertices);
        }
        else
        {
            singleMeshPtr = &targetMesh;  // Combined slot if set, else PrimaryMesh
            singleSearch.Init(*singleMeshPtr);
            LOG_INFO("RefineVertices: single search (targetVerts={})", (int)singleMeshPtr->NumVertices());
        }

        // Pull the matching non-degenerate target masks. Empty when the slot
        // wasn't set; the per-hit check skips when empty.
        const Eigen::VectorXf& headTargetMask = dualHeadBody ? target.NonDegenerateMaskFor(MeshSlot::Head) : Eigen::VectorXf();
        const Eigen::VectorXf& bodyTargetMask = dualHeadBody ? target.NonDegenerateMaskFor(MeshSlot::Body) : Eigen::VectorXf();
        Eigen::VectorXf singleTargetMask;
        if (!dualHeadBody)
        {
            // Find which slot the primary mesh belongs to so we can pull its mask.
            for (int s = 0; s < (int)MeshSlot::Combined + 1; ++s)
            {
                const auto slot = static_cast<MeshSlot>(s);
                if (target.MeshFor(slot).get() == singleMeshPtr) { singleTargetMask = target.NonDegenerateMaskFor(slot); break; }
            }
        }

        int rejected = 0;
        int rejectedDegen = 0;
        for (int i = 0; i < numVertices; ++i)
        {
            const bool useHead = dualHeadBody && i < m->numFaceVertices;
            auto& search = dualHeadBody ? (useHead ? headSearch : bodySearch) : singleSearch;
            const Mesh<float>& meshForLookup = dualHeadBody
                ? (useHead ? *headMeshPtr : *bodyMeshPtr)
                : *singleMeshPtr;
            const Eigen::VectorXf& tgtMask = dualHeadBody
                ? (useHead ? headTargetMask : bodyTargetMask)
                : singleTargetMask;
            const BarycentricCoordinates<float, 3> bc = search.Search(scaledVertices.col(i));

            // Reject hits that landed on a fully-masked target triangle —
            // bc carries the 3 vertex indices we landed on; all-zero mask
            // means every contributor is degenerate / borderline.
            if (tgtMask.size() == (int)meshForLookup.NumVertices())
            {
                const auto& idx = bc.Indices();
                const float w = tgtMask(idx[0]) + tgtMask(idx[1]) + tgtMask(idx[2]);
                if (w <= 0.0f)
                {
                    initialTargetPositions.col(i) = scaledVertices.col(i);
                    ++rejectedDegen;
                    continue;
                }
            }

            initialTargetPositions.col(i) = bc.Evaluate<3>(meshForLookup.Vertices());
            if (icpTol > 0.0f)
            {
                const float dist = (initialTargetPositions.col(i) - scaledVertices.col(i)).norm();
                if (dist > icpTol)
                {
                    initialTargetPositions.col(i) = scaledVertices.col(i);
                    ++rejected;
                }
            }
        }
        if (rejectedDegen > 0)
            LOG_INFO("RefineVertices: rejected {} correspondences on degenerate target tris", rejectedDegen);
        if (icpTol > 0.0f)
            LOG_INFO("RefineVertices: icpTol={} rejected {} / {} correspondences", icpTol, rejected, numVertices);
    }

    DeformationModelVertex<float> vertexDefModel;
    vertexDefModel.SetMeshTopology(poseGeometryMesh);
    vertexDefModel.SetRestVertices(geometryState->Vertices().Matrix());

    Configuration vertexConfig = vertexDefModel.GetConfiguration();
    vertexConfig["vertexOffsetRegularization"].Set(0.0f);
    vertexConfig["vertexLaplacian"].Set(refinementSettings["laplacian"].Value<float>());
    vertexConfig["dihedralBending"].Set(refinementSettings["bending"].Value<float>());
    vertexConfig["projectiveStrain"].Set(refinementSettings["strain"].Value<float>());
    vertexDefModel.SetConfiguration(vertexConfig);

    vertexDefModel.SetVertexOffsets(Eigen::Matrix<float, 3, -1>::Zero(3, numVertices));
    Context<float> vertexContext;
    GaussNewtonSolver<float> vertexSolver;

    Vector<float> fitWeights = Vector<float>::Ones(numVertices);
    if (solveConfig.vertexMask.size() == numVertices)
    {
        fitWeights = solveConfig.vertexMask;
    }
    else if (solveConfig.vertexMask.size() > 0)
    {
        LOG_WARNING("RefineVertices: vertexMask size ({}) doesn't match vertex count ({}), ignoring mask",
                    solveConfig.vertexMask.size(), numVertices);
    }

    const auto& keypointCorrespondences = target.Keypoints();
    const auto& viewportConstraints2D = target.Viewports();
    auto landmarks2DShared = target.Landmarks2D();
    auto* landmarkConstraints2D = landmarks2DShared.get();
	UpdateLandmarkConstraintsConfig(landmarkConstraints2D, solveConfig);

    // anchorUnmasked removed — previously applied an extra point-to-point
    // anchor on unmasked verts during refine; unused in practice.

    std::function<Cost<float>(Context<float>*)> vertexCostFunction = [&](Context<float>* ctx)
    {
        Cost<float> cost;

        DiffDataMatrix<float, 3, -1> deformedVertices = vertexDefModel.EvaluateVertices(ctx);
        DiffDataMatrix<float, 3, -1> scaledDeformedVertices = ScaleVertices(deformedVertices, uniformScale);

        Eigen::VectorXi allIndices(numVertices);
        for (int i = 0; i < numVertices; ++i)
            allIndices[i] = i;

        cost.Add(PointPointConstraintFunction<float, 3>::Evaluate(
                     scaledDeformedVertices, allIndices, initialTargetPositions,
                     fitWeights, refinementSettings["vertexWeight"].Value<float>()),
                1.0f, "vertex_correspondence");

        AddKeypointConstraints(cost, scaledDeformedVertices, keypointCorrespondences,
            solveConfig.vertexMask, refinementSettings["keypoint"].Value<float>());

        AddViewport2DConstraints(cost, scaledDeformedVertices, nullptr, nullptr,
            viewportConstraints2D, solveConfig.vertexMask, Eigen::VectorXf(),
            0.0f);

        AddLandmark2DConstraints(cost, scaledDeformedVertices,
            poseGeometryMesh.VertexNormals(), landmarkConstraints2D, refinementSettings["landmark2D"].Value<float>());

        cost.Add(vertexDefModel.EvaluateModelConstraints(ctx), 1.0f);

        return cost;
    };

    GaussNewtonSolver<float>::Settings vertexSolverSettings;
    vertexSolverSettings.reg = refinementSettings["vertexReg"].Value<float>();
    vertexSolverSettings.residualErrorStoppingCriterion = 1e-4f;
    vertexSolverSettings.useDenseJtJAccumulation = false;

    const int totalIters = refinementSettings["iterations"].Value<int>();
    Timer refineTimer;

    vertexSolverSettings.iterations = totalIters;
    vertexSolver.Solve(vertexCostFunction, vertexContext, vertexSolverSettings);
    LOG_INFO("RefineVertices completed in {} ms", refineTimer.Current());

    const Eigen::Matrix<float, 3, -1> optimizedVertices = vertexDefModel.DeformedVertices();
    const Eigen::Matrix<float, 3, -1> newDeltas = m->rigGeometry->EvaluateInverseSkinning(0, *geometryState, optimizedVertices) -
                                                   m->rigGeometry->EvaluateInverseSkinning(0, *geometryState, geometryState->Vertices().Matrix());

    if (state.m->VertexDeltas.cols() == newDeltas.cols() && fitWeights.size() == newDeltas.cols() && haveVD)
    {
        // Refinement runs at scale=1 internally (see the bake above), so
        // newDeltas is already in stored (raw) space — just add it on
        // weighted by the fit mask. Accumulate so mask=0 preserves the
        // prior VD instead of being overwritten — fix for laplacian eating
        // VD on far-from-scan / unmasked regions.
        for (int i = 0; i < static_cast<int>(newDeltas.cols()); ++i)
            state.m->VertexDeltas.col(i) += fitWeights(i) * newDeltas.col(i);
    }
    else
    {
        state.m->VertexDeltas = newDeltas;
    }
}

void BodyShapeEditor::SetNeutralJointsTranslations(State& state, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> InJoints)
{
    // Eval produces scaled_trans = ScaleFactor * (known + VertexDeltaScale * JointDeltas).
    // To land InJoints exactly, solve:
    //   JointDeltas_new = (InJoints - currentTrans) / (ScaleFactor * VertexDeltaScale) + JointDeltas_old
    const float scaleFactor = (state.m->ScaleFactor != 0.0f) ? state.m->ScaleFactor : 1.0f;
    const float vds = (state.m->VertexDeltaScale != 0.0f) ? state.m->VertexDeltaScale : 1.0f;
    const float invScale = 1.0f / (scaleFactor * vds);

    const size_t numJoints = state.GetJointBindMatrices().size();
    if (state.m->JointDeltas.cols() != static_cast<int>(numJoints))
    {
        state.m->JointDeltas = Eigen::Matrix<float, 3, -1>::Zero(3, (int)numJoints);
    }
    for (size_t i = 0; i < numJoints && (int)i < InJoints.cols(); ++i)
    {
        state.m->JointDeltas.col(i) += invScale * (InJoints.col(i) - state.GetJointBindMatrices()[i].translation());
    }

    EvaluateState(state);
}

void BodyShapeEditor::SetNeutralJointRotations(State& state, av::ConstArrayView<float> inJointRotations)
{
    std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices = state.m->JointBindMatrices;
    std::vector<float> jointRotations;
    jointRotations.resize(m->rigGeometry->NumJoints() * 3);
    const auto getJointParent = [this](std::uint16_t jointIndex)
    {
        return m->rigGeometry->GetJointParentIndices()[jointIndex];
    };
    for (std::uint16_t jointIndex = 0; jointIndex < m->rigGeometry->NumJoints(); jointIndex++)
    {
        Eigen::Transform<float, 3, Eigen::Affine>& localTransform = jointMatrices[jointIndex];
        const int parentJointIndex = getJointParent(jointIndex);
        if (parentJointIndex > -1)
        {
            auto parentTransform = jointMatrices[parentJointIndex];
            localTransform = parentTransform.inverse() * jointMatrices[jointIndex];
        }
        else
        {
            localTransform = jointMatrices[jointIndex];
        }
        Eigen::Matrix<float, 3, 3> InJointRotation = EulerXYZ(inJointRotations[jointIndex * 3 + 0], inJointRotations[jointIndex * 3 + 1], inJointRotations[jointIndex * 3 + 2]);
        localTransform.linear() = InJointRotation;
        if(parentJointIndex > -1)
        {
            localTransform = jointMatrices[parentJointIndex] * localTransform;
        }
    } 
}

void BodyShapeEditor::VolumetricallyFitHandAndFeetJoints(State& State)
{
    auto translationEstimates = m->jointEstimator.EstimateJointWorldTranslations(State.m->RigMeshes[0].Vertices());
    const std::vector<int>& jointHierarchy = m->rigGeometry->GetJointParentIndices();
    const std::vector<std::string>& jointNames = m->rigGeometry->GetJointNames();
    const int n = static_cast<int>(jointHierarchy.size());
    
    std::unordered_map<std::string, int> nameToIdx;
    nameToIdx.reserve(n);
    for (int i = 0; i < n; ++i) 
    {
        nameToIdx.emplace(jointNames[i], i);
    }
    std::vector<int> jointIndicesToUpdate; 
    std::vector<char> mark(n, 0);
    auto markIfFound = [&](const char* nm){
        auto it = nameToIdx.find(nm);
        if (it != nameToIdx.end())
        {
            mark[it->second] = 1;
            jointIndicesToUpdate.push_back(it->second);
        }
    };
    markIfFound("hand_l");
    markIfFound("hand_r");
    markIfFound("foot_l");
    markIfFound("foot_r");
    
    for (int i = 0; i < n; ++i) {
        int p = jointHierarchy[i];
        if (mark[i] != 1 && p > 0 && mark[p] == 1) 
        {
            mark[i] = 1;
            jointIndicesToUpdate.push_back(i);
        }
    }

    std::vector<Eigen::Transform<float, 3, Eigen::Affine>> jointMatrices = State.GetJointBindMatrices();
    Eigen::Matrix<float, 3, -1> jointPositions(3, (int)jointMatrices.size());
    for (size_t i = 0; i < m->rigGeometry->GetBindMatrices().size(); ++i)
    {
        jointPositions.col(i) = jointMatrices[i].translation();
    }
    for (int  i : jointIndicesToUpdate) {
        jointPositions.col(i) = translationEstimates.col(i);
        jointMatrices[i].translation() = translationEstimates.col(i);
    }
    SetNeutralJointsTranslations(State, jointPositions);
    
    jointIndicesToUpdate.push_back(jointHierarchy[nameToIdx["hand_l"]]);
    jointIndicesToUpdate.push_back(jointHierarchy[nameToIdx["hand_r"]]);
    jointIndicesToUpdate.push_back(jointHierarchy[nameToIdx["foot_l"]]);
    jointIndicesToUpdate.push_back(jointHierarchy[nameToIdx["foot_r"]]);
    m->jointEstimator.FixJointOrients(*m->rigGeometry, State.m->JointBindMatrices, State.GetMesh(0).Vertices());
    
}

bool BodyShapeEditor::SetNeutralMesh(State& state, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> inMesh) const
{
    // Contract: AFTER this call, evaluating `state` under canonical mode
    // (EvaluatePose=true, FloorOffsetApplied=false) yields `inMesh`.
    // `inMesh` is assumed to live in that same canonical frame.
    //
    // The baseline we subtract from `inMesh` is the canonical-mode mesh of
    // `state`, derived from a COPY forced into canonical flags. Using
    // state.GetMesh() directly would break invariance to the caller's flag
    // state — Floor=true gives a floor-shifted cache, EvaluatePose=false
    // gives a pose-suppressed one.

    // Check if input mesh is empty
    if (inMesh.cols() == 0)
    {
        LOG_WARNING("SetNeutralMesh: Input mesh is empty (no vertices)");
        return false;
    }

    // Check if bodyToCombinedMapping is set up
    if (m->bodyToCombinedMapping.empty())
    {
        LOG_WARNING("SetNeutralMesh: bodyToCombinedMapping is not set up");
        return false;
    }

    const float scaleFactor = (state.m->ScaleFactor      != 0.0f) ? state.m->ScaleFactor      : 1.0f;
    const float vds         = (state.m->VertexDeltaScale != 0.0f) ? state.m->VertexDeltaScale : 1.0f;
    const auto& mapping     = m->bodyToCombinedMapping[0];

    State canonicalCopy(state);
    canonicalCopy.m->FloorOffsetApplied = false;
    canonicalCopy.m->EvaluatePose       = true;
    EvaluateState(canonicalCopy, CalcStrategy::Auto, /*isEvaluatingPose=*/true);

    // Check if RigMeshes is properly initialized
    if (canonicalCopy.m->RigMeshes.empty())
    {
        LOG_WARNING("SetNeutralMesh: RigMeshes is empty after EvaluateState (model not loaded?)");
        return false;
    }

    const Eigen::Matrix<float, 3, -1>& currentMesh = canonicalCopy.m->RigMeshes[0].Vertices();

    if (state.m->VertexDeltas.cols() != currentMesh.cols())
    {
        state.m->VertexDeltas = Eigen::Matrix<float, 3, -1>::Zero(3, currentMesh.cols());
    }

    // VertexDeltas live in the bind frame (added pre-skinning). Both
    // currentMesh and inMesh are in posed/skinned world frame, so we
    // inverse-skin both before computing the residual. geometryState must
    // be built from the canonical copy's controls so its skinning matrices
    // match the frame `currentMesh` was evaluated in.
    BodyGeometry<float>::State geometryState;
    const DiffData<float> rawControls   = m->poseLogic->EvaluateRawControls(canonicalCopy.m->GuiControls);
    const DiffData<float> joints        = m->poseLogic->EvaluateJoints(0, rawControls);
    const DiffData<float> rbfPsd        = m->rbfLogic->EvaluatePoseControlsFromJoints(joints, true);
    const DiffData<float> rbfJoints     = m->poseLogic->EvaluateRbfJoints(0, rbfPsd);
    const DiffData<float> twistedJoints = m->twistSwingLogic->EvaluateJointsFromJoints(joints + rbfJoints);
    m->rigGeometry->EvaluateBodyGeometry(0, twistedJoints, rawControls, geometryState);

    const Eigen::Matrix<float, 3, -1> currentBind =
        m->rigGeometry->EvaluateInverseSkinning(0, geometryState, currentMesh / scaleFactor);

    if (inMesh.cols() == static_cast<int>(mapping.size()))
    {
        // Body-only target. Seed the combined buffer with currentMesh so
        // non-mapped (face) entries contribute zero delta after subtraction.
        Eigen::Matrix<float, 3, -1> inMeshCombined = currentMesh;
        for (size_t i = 0; i < mapping.size(); ++i)
        {
            const int ci = mapping[i];
            if (ci >= 0 && ci < (int)inMeshCombined.cols())
                inMeshCombined.col(ci) = inMesh.col(i);
        }
        const Eigen::Matrix<float, 3, -1> targetBind =
            m->rigGeometry->EvaluateInverseSkinning(0, geometryState, inMeshCombined / scaleFactor);
        for (size_t i = 0; i < mapping.size(); ++i)
        {
            const int ci = mapping[i];
            if (ci >= 0 && ci < (int)state.m->VertexDeltas.cols())
                state.m->VertexDeltas.col(ci) += (targetBind.col(ci) - currentBind.col(ci)) / vds;
        }
    }
    else if (inMesh.cols() == currentMesh.cols())
    {
        const Eigen::Matrix<float, 3, -1> targetBind =
            m->rigGeometry->EvaluateInverseSkinning(0, geometryState, Eigen::Matrix<float, 3, -1>(inMesh) / scaleFactor);
        state.m->VertexDeltas += (targetBind - currentBind) / vds;
    }
    else
    {
        LOG_WARNING("SetNeutralMesh: Input mesh has {} vertices but expected {} (mapping size) or {} (base mesh size)",
                    inMesh.cols(), mapping.size(), currentMesh.cols());
        return false;
    }
    EvaluateState(state);
    return true;
}

void BodyShapeEditor::ResetPoseGuiControls(State& State)
{
    auto guiControls = State.GetGuiControls();
    for (int i = 0; i < GetGuiControlNames().size(); ++i)
    {
        const auto& guiControlName = GetGuiControlNames()[i];
        if (guiControlName.starts_with("pose_"))
        {
            guiControls[i] = 0.0;
        }
    }
    State.SetGuiControls(guiControls);
}

void BodyShapeEditor::ResetFaceState(State& State)
{
    if (State.m->faceState)
    {
        State.m->faceState.reset();
        if (m->facePatchBlendModel)
        {
            State.m->faceState = m->facePatchBlendModel->CreateState();
        }
    }
    EvaluateState(State);
}

void BodyShapeEditor::ClearJointDeltas(State& State)
{
    State.m->JointDeltas.setZero();
    EvaluateState(State);
}

void BodyShapeEditor::Init(std::shared_ptr<BodyLogic<float>> bodyLogic,
    std::shared_ptr<BodyGeometry<float>> combinedBodyArchetypeGeometry,
    std::shared_ptr<RigLogic<float>> CombinedBodyRigLogic,
    std::shared_ptr<BodyGeometry<float>> bodyGeometry,
    av::ConstArrayView<BodyMeasurement> contours,
    const std::vector<std::map<std::string, std::map<std::string, float>>>& jointSkinningWeightLodPropagationMap,
    const std::vector<int>& maxSkinWeightsPerVertexForEachLod,
    std::shared_ptr<const LodGeneration<float>> combinedLodGenerationData,
    const std::map<std::string, VertexWeights<float>>& partWeights)
{
    m->minMeasurementInput.clear();
    m->maxMeasurementInput.clear();
    m->variableMinMeasurementInput.clear();
    m->variableMaxMeasurementInput.clear();
    m->heightConstraintIndex = -1;
    m->numBaseRawControls = bodyLogic->NumRawControls();
    m->numBaseGuiControls = bodyLogic->NumGUIControls();

    if (!m->poseLogic || m->poseLogic->NumRawControls() == bodyLogic->NumRawControls())
    {
        m->poseLogic = bodyLogic;
    }

    m->rigGeometry = bodyGeometry;
    m->combinedBodyArchetypeRigGeometry = combinedBodyArchetypeGeometry;
    m->combinedLodGenerationData = combinedLodGenerationData;
    m->partWeights = partWeights;
    m->rbfControlOffset = CombinedBodyRigLogic->NumRawControls() + CombinedBodyRigLogic->NumMLControls() + CombinedBodyRigLogic->NumPsdControls();
    if (!m->threadPool)
    {
        m->threadPool = TaskThreadPool::GlobalInstance(true);
    }

    m->rigGeometry->SetThreadPool(m->threadPool);

    m->Constraints.assign(contours.begin(), contours.end());

    const std::vector<std::string>& guiControlNames = m->poseLogic->GuiControlNames();
    const std::vector<std::string>& rawControlNames = m->poseLogic->RawControlNames();
    if (CombinedBodyRigLogic)
    {
        m->jointGroupInputIndices = CombinedBodyRigLogic->GetJointGroupInputIndices();
        m->jointGroupOutputIndices = CombinedBodyRigLogic->GetJointGroupOutputIndices();
        m->jointGroupLODs = CombinedBodyRigLogic->GetJointGroupLODs();
    }

    m->localIndices.clear();
    m->localShapeIndices.clear();
    m->localSkeletonIndices.clear();
    m->globalIndices.clear();
    m->poseIndices.clear();
    {
        for (int i = 0; i < int(guiControlNames.size()); ++i)
        {
            const std::string& name = guiControlNames[i];
            if (name.find("global_") != name.npos)
            {
                m->globalIndices.emplace_back(i);
            }
            else if (name.find("local_") != name.npos)
            {
                m->localIndices.emplace_back(i);
                // Joint controls ending with _0 are proportions (first PCA mode = bone lengths)
                if (name.find("local_joint") != name.npos && name.ends_with("_0"))
                {
                    m->localSkeletonIndices.emplace_back(i);
                }
                else
                {
                    m->localShapeIndices.emplace_back(i);
                }
            }
            else if (name.find("pose_") != name.npos)
            {
                m->poseIndices.emplace_back(i);
            }
            else
            {
                CARBON_CRITICAL("unknown control \"{}\"", name);
            }
        }
    }

    m->rawLocalIndices.clear();
    m->rawPoseIndices.clear();
    {
        for (int i = 0; i < int(rawControlNames.size()); ++i)
        {
            const std::string& name = rawControlNames[i];
            if (name.find("local_") != name.npos)
            {
                m->rawLocalIndices.emplace_back(i);
            }
            else if (name.find("pose_driver_") != name.npos)
            {
                m->rawPoseIndices.emplace_back(i);
            }
            else if (name.find("pose_rigid_") != name.npos) 
            {
                // ignore rigid controls
                continue;
            }
            else
            {
                CARBON_CRITICAL("unknown raw control \"{}\"", name);
            }
        }
    }


    Eigen::SparseMatrix<float, Eigen::ColMajor> invertedJointMatrix = m->poseLogic->GetJointMatrix(0);

    std::map<std::string, std::vector<int>> skeletonPcaControls;
    std::map<std::string, std::vector<int>> shapePcaControls;
    std::map<std::string, std::string> symmetricPartMapping;
    std::set<std::string> regionNamesSet;
    std::vector<int> jointControls;
    std::vector<int> shapeControls;
    for (int i = 0; i < int(rawControlNames.size()); ++i)
    {
        std::string name = rawControlNames[i];
        size_t suffixPos = name.find_last_of('_');
        if (suffixPos != std::string::npos)
        {
            name = name.substr(0, suffixPos);
        }
        const bool isLeft = StringEndsWith(name, "_l");
        const bool isRight = StringEndsWith(name, "_r");
        // const bool isCenter = !isLeft && !isRight;
        const bool isPose = StringStartsWith(name, "pose");
        std::string partname = name;
        if (StringStartsWith(name, "local_joint_"))
        {
            // skeleton pca
            partname = ReplaceSubstring(partname, "local_", "");
            skeletonPcaControls[partname].push_back(i);
            jointControls.push_back(i);

            auto regionName = ReplaceSubstring(partname, "joint_", "");
            for (Eigen::SparseMatrix<float, Eigen::ColMajor>::InnerIterator it(invertedJointMatrix, i); it; ++it)
            {
                m->regionToJoints[regionName].emplace(static_cast<int>(it.row() / 9));
            }
        }
        else if (StringStartsWith(name, "local_"))
        {
            // shape pca
            partname = ReplaceSubstring(partname, "local_", "");
            shapePcaControls[partname].push_back(i);
            shapeControls.push_back(i);
        }
        else if (!isPose)
        {
            LOG_ERROR("unknown control {}", rawControlNames[i]);
        }

        if (!isPose)
        {
            if (isLeft)
            {
                symmetricPartMapping[partname] = partname.substr(0, partname.size() - 2) + "_r";
            }
            else if (isRight)
            {
                symmetricPartMapping[partname] = partname.substr(0, partname.size() - 2) + "_l";
            }
            else
            {
                symmetricPartMapping[partname] = partname;
            }
            regionNamesSet.insert(partname);
        }
    }
    m->skeletonPcaControls = skeletonPcaControls;
    m->shapePcaControls = shapePcaControls;
    m->symmetricPartMapping = symmetricPartMapping;
    m->regionNames = std::vector<std::string>(regionNamesSet.begin(), regionNamesSet.end());

    m->rawToGuiControls = std::vector<int>(rawControlNames.size(), -1);
    m->guiToRawControls = std::vector<int>(guiControlNames.size(), -1);
    for (int i = 0; i < (int)rawControlNames.size(); ++i)
    {
        m->rawToGuiControls[i] = GetItemIndex<std::string>(m->poseLogic->GuiControlNames(), rawControlNames[i]);
    }
    for (int i = 0; i < (int)guiControlNames.size(); ++i)
    {
        m->guiToRawControls[i] = GetItemIndex<std::string>(m->poseLogic->RawControlNames(), guiControlNames[i]);
    }

    // get mapping matrix from gui to raw controls
    m->guiToRawMappingMatrix = Eigen::SparseMatrix<float, Eigen::RowMajor>(m->poseLogic->NumRawControls(), m->poseLogic->NumGUIControls());
    for (const auto& mapping : m->poseLogic->GuiToRawMapping())
    {
        m->guiToRawMappingMatrix.coeffRef(mapping.outputIndex, mapping.inputIndex) += mapping.slope;
        if (mapping.cut != 0)
        {
            CARBON_CRITICAL("invalid cut value {}", mapping.cut);
        }
    }
    m->guiToRawMappingMatrix.makeCompressed();

    {
        Eigen::MatrixXf A = Eigen::MatrixXf(m->guiToRawMappingMatrix)(Eigen::all, m->globalIndices);
        m->rawToGlobalGuiControlsSolveMatrix = (A.transpose() * A).inverse() * A.transpose();
    }

    m->meshTriangles.resize(NumLODs());
    // get lod 0 from the pca model rig geometry, as we will always have this (combinedBodyArchetypeRigGeometry is optional)
    Mesh<float> triMesh = m->rigGeometry->GetMesh(0);
    triMesh.Triangulate();
    m->meshTriangles[0] = triMesh.Triangles();
    m->triTopology = std::make_shared<Mesh<float>>(triMesh);
    m->heTopology = std::make_shared<HalfEdgeMesh<float>>(triMesh);

    if (m->combinedBodyArchetypeRigGeometry)
    {
        for (size_t i = 1; i < static_cast<size_t>(NumLODs()); ++i)
        {
            triMesh = m->combinedBodyArchetypeRigGeometry->GetMesh(static_cast<int>(i));
            triMesh.Triangulate();
            m->meshTriangles[i] = triMesh.Triangles();
        }
    }

    m->symControls = std::make_unique<SymmetricControls<float>>(*m->poseLogic);
    BoundedVectorVariable<float> pose { m->poseLogic->NumGUIControls() };
    Eigen::VectorXf guiWeight = Eigen::VectorXf::Zero(m->poseLogic->NumGUIControls());
    for (const auto& index : m->localIndices)
    {
        guiWeight[index] = 1.0f;
    }
    for (const auto& index : m->globalIndices)
    {
        guiWeight[index] = 0.33f;
    }
    m->gwm = SparseMatrix<float>(guiWeight.size(), guiWeight.size());
    m->gwm.setIdentity();
    m->gwm.diagonal() = guiWeight;

    m->maxSkinWeights = maxSkinWeightsPerVertexForEachLod;

    m->jointSkinningWeightLodPropagationMap = jointSkinningWeightLodPropagationMap;

    {
        // create a linear evaluation matrix
        Eigen::MatrixXf identityEvaluationMatrix = Eigen::MatrixXf::Zero(m->rigGeometry->GetMesh(0).NumVertices() * 3, (int)m->rawLocalIndices.size());
        Eigen::MatrixXf jointEvaluationMatrix = Eigen::MatrixXf::Zero(m->rigGeometry->GetBindMatrices().size() * 3, (int)m->rawLocalIndices.size());
        Eigen::VectorXf zeroRawControls = Eigen::VectorXf::Zero(m->poseLogic->NumRawControls());
        DiffData<float> zeroJoints = m->poseLogic->EvaluateJoints(0, zeroRawControls);
        BodyGeometry<float>::State zeroState;
        m->rigGeometry->EvaluateBodyGeometry(0, zeroJoints, zeroRawControls, zeroState);
        Eigen::Matrix3Xf zeroVertices = zeroState.Vertices().Matrix();

        auto calcVertexEvalMatrices = [&](int start, int end)
        {
            const auto& blendShapeMap = m->rigGeometry->GetBlendshapeMap(0);
            const auto& blendShapes = m->rigGeometry->GetBlendshapeMatrix(0);
            for (int i = start; i < end; ++i)
            {
                const int rawControlIndex = shapeControls[i];
                identityEvaluationMatrix.col(rawControlIndex) = blendShapes.col(blendShapeMap[rawControlIndex]);
            }
        };

        auto calcJointMatrices = [&](int start, int end)
        {
            BodyGeometry<float>::State geometryState;
            for (int i = start; i < end; ++i)
            {
                const int rawControlIndex = jointControls[i];
                Eigen::VectorXf rawControls = Eigen::VectorXf::Zero(m->poseLogic->NumRawControls());
                rawControls[rawControlIndex] = 1.0f;
                DiffData<float> joints = m->poseLogic->EvaluateJoints(0, rawControls);
                m->rigGeometry->EvaluateBodyGeometry(0, joints, rawControls, geometryState);
                identityEvaluationMatrix.col(rawControlIndex) = (geometryState.Vertices().Matrix() - zeroVertices).reshaped();
                for (int ji = 0; ji < (int)geometryState.GetWorldMatrices().size(); ++ji)
                {
                    jointEvaluationMatrix.col(rawControlIndex).segment(3 * ji, 3) = geometryState.GetWorldMatrices()[ji].translation() - m->rigGeometry->GetBindMatrices()[ji].translation();
                }
            }
        };

        m->threadPool->AddTaskRangeAndWait((int)jointControls.size(), calcJointMatrices);
        m->threadPool->AddTaskRangeAndWait((int)shapeControls.size(), calcVertexEvalMatrices);
        m->identityVertexEvaluationMatrix = identityEvaluationMatrix.sparseView(0, 0);
        m->identityVertexEvaluationMatrix.makeCompressed();

        m->identityJointEvaluationMatrix = jointEvaluationMatrix.sparseView(0, 0);
        m->identityJointEvaluationMatrix.makeCompressed();

        // create the identity evaluation matrix based on the symmetric constrols
        const auto& symToGuiMat = m->symControls->SymmetricToGuiControlsMatrix();
        const auto& guiToRawMat = m->guiToRawMappingMatrix;
        Eigen::SparseMatrix<float, Eigen::RowMajor> rawLocalIndicesMat(m->rawLocalIndices.size(), m->rawLocalIndices.size());
        for (int i = 0; i < m->rawLocalIndices.size(); ++i)
        {
            rawLocalIndicesMat.coeffRef(i, m->rawLocalIndices[i]) = 1.0f;
        }
        rawLocalIndicesMat.makeCompressed();
        m->symmetricIdentityVertexEvaluationMatrix = m->identityVertexEvaluationMatrix * (rawLocalIndicesMat * guiToRawMat * symToGuiMat);
    }

    m->Constraints = CreateState()->m->Constraints;

    // retrieve floor index
    for (size_t i = 0; i < m->Constraints.size(); ++i)
    {
        if (m->Constraints[i].GetName() == "Height")
        {
            m->floorIndex = m->Constraints[i].GetVertexIDs().front();
            for (int vID : m->Constraints[i].GetVertexIDs())
            {
                if (m->rigGeometry->GetMesh(0).Vertices().col(vID)[1] < m->rigGeometry->GetMesh(0).Vertices().col(m->floorIndex)[1])
                {
                    m->floorIndex = vID;
                }
            }
        }
    }
}

std::shared_ptr<BodyLogic<float>> BodyShapeEditor::GetBodyLogic() const { return m->poseLogic; }

const std::vector<std::string>& BodyShapeEditor::GetGuiControlNames() const { return m->poseLogic->GuiControlNames(); }
const std::vector<std::string>& BodyShapeEditor::GetRawControlNames() const { return m->poseLogic->RawControlNames(); }

std::vector<std::vector<int>> ReadBodyToCombinedMapping(const JsonElement& json)
{
    std::vector<std::vector<int>> mappings {};

    if (!json.Contains("body_to_combined"))
    {
        CARBON_CRITICAL("Invalid json file. Missing \"body_to_combined\" mapping.");
    }

    const std::uint16_t LODCount = 4u;

    if (json["body_to_combined"].Size() == LODCount)
    {
        mappings = json["body_to_combined"].Get<std::vector<std::vector<int>>>();
    }
    else
    {
        mappings.push_back(json["body_to_combined"].Get<std::vector<int>>());
        mappings.resize(LODCount);
    }
    return mappings;
}

std::vector<BodyShapeEditor::Keypoint> ReadKeypoints(const JsonElement& json)
{
    std::vector<BodyShapeEditor::Keypoint> keypoints {};

    if (!json.Contains("keypoints"))
    {
        CARBON_CRITICAL("Invalid json file. Missing \"keypoints\" mapping.");
    }

    const auto& keyPointsJson = json["keypoints"].Array();
    for (const auto& keyPoint : keyPointsJson)
    {
        BodyShapeEditor::Keypoint kp;
        if (keyPoint.Contains("index"))
        {
            kp.index = keyPoint["index"].Get<int>();
        }
        if (keyPoint.Contains("name"))
        {
            kp.name = keyPoint["name"].String();
        }
        keypoints.push_back(kp);
    }
    return keypoints;
}

std::vector<int> ReadFitToTargetIndices(const JsonElement& json)
{
    if (!json.Contains("fit_to_target_indices"))
    {
        return {};
    }
    return json["fit_to_target_indices"].Get<std::vector<int>>();
}

std::vector<std::vector<int>> ReadBodyToCombinedMapping(const std::string& jsonString)
{
    const JsonElement json = ReadJson(jsonString);
    return ReadBodyToCombinedMapping(json);
}

void BodyShapeEditor::Init(const dna::Reader* reader,
    trio::BoundedIOStream* rbfModelStream,
    trio::BoundedIOStream* skinModelStream,
    dna::Reader* InCombinedArchetypeBodyDnaReader,
    const std::vector<std::map<std::string,
        std::map<std::string,
            float>>>& JointSkinningWeightLodPropagationMap,
    const std::vector<int>& maxSkinWeightsPerVertexForEachLod,
    std::shared_ptr<const LodGeneration<float>> combinedLodGenerationData)
{
    if (!m->threadPool)
    {
        m->threadPool = TaskThreadPool::GlobalInstance(true);
    }
    auto rigLogic = std::make_shared<BodyLogic<float>>();
    auto rigGeometry = std::make_shared<BodyGeometry<float>>(m->threadPool);
    std::shared_ptr<BodyGeometry<float>> combinedArchetypeRigGeometry = nullptr;
    std::shared_ptr<RigLogic<float>> combinedArchetypeRigLogic;
    if (!rigLogic->Init(reader))
    {
        CARBON_CRITICAL("failed to decode rig");
    }
    if (!rigGeometry->Init(reader))
    {
        CARBON_CRITICAL("failed to decode rig");
    }
    if (InCombinedArchetypeBodyDnaReader)
    {
        combinedArchetypeRigGeometry = std::make_shared<BodyGeometry<float>>(m->threadPool);
        if (!combinedArchetypeRigGeometry->Init(InCombinedArchetypeBodyDnaReader))
        {
            CARBON_CRITICAL("failed to decode body archetype");
        }
        combinedArchetypeRigLogic = std::make_shared<RigLogic<float>>();
        if (!combinedArchetypeRigLogic->Init(InCombinedArchetypeBodyDnaReader))
        {
            CARBON_CRITICAL("failed to decode body archetype");
        }
    }

    const auto pcaJsonStringView = reader->getMetaDataValue("pca_model");
    const JsonElement pcaModelJson = ReadJson(std::string { pcaJsonStringView.data(), pcaJsonStringView.size() });
    const std::vector<BodyMeasurement> contours = BodyMeasurement::FromJSON(pcaModelJson, rigGeometry->GetMesh(0).Vertices());

    if (pcaModelJson.Contains("joint_estimator"))
    {
        m->jointEstimator.Init(pcaModelJson["joint_estimator"]);
    }

    if (pcaModelJson.Contains("pose_logic"))
    {
        std::vector<std::string> poseLogicLines;
        for (const auto& line : pcaModelJson["pose_logic"].Array())
        {
            poseLogicLines.push_back(line.String());
        }
        m->poseLogic = PoseLogicFromString(poseLogicLines, rigLogic, rigGeometry, m->jointEstimator.CoreJoints());
        m->poseLogic->InitRBFJointMatrix(InCombinedArchetypeBodyDnaReader);
        m->rbfLogic = std::make_shared<RBFLogic<float>>();
        m->rbfLogic->Init(InCombinedArchetypeBodyDnaReader);
        m->twistSwingLogic = std::make_shared<TwistSwingLogic<float>>();
        m->twistSwingLogic->Init(InCombinedArchetypeBodyDnaReader, true);
    }

    if (pcaModelJson.Contains("solve_hierarchy"))
    {
        m->solveSteps = pcaModelJson["solve_hierarchy"].Get<std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>();
        for (auto& pair : m->solveSteps)
        {
            for (auto& name : pair.first)
            {
                name = BodyMeasurement::GetAlias(name);
            }
        }
    }
    if (pcaModelJson.Contains("model_version"))
    {
        m->ModelVersion = pcaModelJson["model_version"].Get<std::string>();
    }
    else
    {
        m->ModelVersion = "0.4.4";
    }

    std::map<std::string, VertexWeights<float>> partWeights;
    if (pcaModelJson.Contains("part_weights"))
    {
        partWeights = VertexWeights<float>::LoadAllVertexWeights(pcaModelJson["part_weights"], rigGeometry->GetMesh(0).NumVertices());
    }
    if (skinModelStream == nullptr)
    {
        m->skinWeightsPCA.ReadFromDNA(reader, "skin_model");
    }
    else
    {
        m->skinWeightsPCA.ReadFromStream(skinModelStream);
    }

    for (std::uint32_t i = 0; i < reader->getMetaDataCount(); ++i)
    {
        auto keyView = reader->getMetaDataKey(i); 
        const char* prefix = "skin_model-";
        size_t prefixLen = std::strlen(prefix);
        if (keyView.size() >= prefixLen && std::strncmp(keyView.data(), prefix, prefixLen) == 0)
        {
            auto skinModelVersionView = keyView.subview(prefixLen, keyView.size() - prefixLen);
            std::string skinModelVersion {skinModelVersionView.begin(), skinModelVersionView.end()};
            SparseMatrixPCA smPCA;
            smPCA.ReadFromDNA(reader, {keyView.begin(), keyView.end()}); 
            m->skinningModels.try_emplace(skinModelVersion, std::move(smPCA)); 
        }
    }

    if (rbfModelStream == nullptr)
    {
        m->rbfPCA.ReadFromDNA(reader, "rbf_model");
    }
    else
    {
        m->rbfPCA.ReadFromStream(rbfModelStream);
    }
    Init(rigLogic,
        combinedArchetypeRigGeometry,
        combinedArchetypeRigLogic,
        rigGeometry,
        contours,
        JointSkinningWeightLodPropagationMap,
        maxSkinWeightsPerVertexForEachLod,
        combinedLodGenerationData,
        partWeights);

    m->bodyToCombinedMapping = ReadBodyToCombinedMapping(pcaModelJson);
    m->keypoints = ReadKeypoints(pcaModelJson);
    m->fitToTargetIndices = ReadFitToTargetIndices(pcaModelJson);

    // create the reverse mapping
    m->combinedToBodyMapping = std::vector<std::map<int, int>>(m->bodyToCombinedMapping.size());
    for (size_t lod = 0; lod < m->bodyToCombinedMapping.size(); ++lod)
    {
        for (size_t i = 0; i < m->bodyToCombinedMapping[lod].size(); ++i)
        {
            const int combinedIndex = m->bodyToCombinedMapping[lod][i];
            m->combinedToBodyMapping[lod][combinedIndex] = int(i);
        }
    }

    // numFaceVertices is filled in either by SetFacePatchBlendModel (when a
    // face PCA model is wired in) or by SetNeckSeamVertexIDs (topology-based
    // fallback: combined − body + seam). Nothing more to do here.
}

void BodyShapeEditor::SetFittingVertexIDs(const std::vector<int>& vertexIds) { m->combinedFittingIndices = vertexIds; }

void BodyShapeEditor::SetNeckSeamVertexIDs(const std::vector<std::vector<int>>& vertexIds)
{
    m->neckSeamIndices = vertexIds;

    // When no face PCA model is supplying numFaceVertices, derive it from
    // the topology: combined = face + body − seam (face and body share the
    // seam loop), so face = combined − body + seam. This matches the value
    // facePatchBlendModel->NumVertices() would return when the model is wired.
    if (!m->facePatchBlendModel && !m->bodyToCombinedMapping.empty() && m->rigGeometry
        && !m->neckSeamIndices.empty())
    {
        const int combinedCount = static_cast<int>(m->rigGeometry->GetMesh(0).Vertices().cols());
        const int bodyCount     = static_cast<int>(m->bodyToCombinedMapping[0].size());
        const int seamCount     = static_cast<int>(m->neckSeamIndices[0].size());
        if (combinedCount > bodyCount)
            m->numFaceVertices = combinedCount - bodyCount + seamCount;
    }

    // set up skinning weight snap configs from the neck seam indices
    m->skinningWeightSnapConfigs.resize(m->combinedBodyArchetypeRigGeometry->GetNumLODs() - 1);
    for (int lod = 1; lod < m->combinedBodyArchetypeRigGeometry->GetNumLODs(); ++lod)
    {
        m->skinningWeightSnapConfigs[size_t(lod - 1)] = m->CalcNeckSeamSkinningWeightsSnapConfig(lod);
    }
}

void BodyShapeEditor::SetBodyToCombinedMapping(int lod, const std::vector<int>& bodyToCombinedMapping)
{
    if (lod >= m->bodyToCombinedMapping.size())
    {
        m->bodyToCombinedMapping.resize(lod + 1u);
    }
    m->bodyToCombinedMapping[lod].assign(bodyToCombinedMapping.begin(),
        bodyToCombinedMapping.end());
}

const std::vector<int>& BodyShapeEditor::GetBodyToCombinedMapping(int lod) const
{
    return m->bodyToCombinedMapping[lod];
}

void BodyShapeEditor::Private::SetMinMaxMeasurements(const State& State)
{
	minMeasurementInput.resize(Constraints.size());
	maxMeasurementInput.resize(Constraints.size());
	variableMinMeasurementInput.resize(Constraints.size());
	variableMaxMeasurementInput.resize(Constraints.size());

	std::vector<int> missingIndices {};
	for (int i = 0; i < Constraints.size(); i++)
	{
		minMeasurementInput[i] = Constraints[i].GetFixedMinInput();
		maxMeasurementInput[i] = Constraints[i].GetFixedMaxInput();
		variableMinMeasurementInput[i] = Constraints[i].GetVariableMinInput();
		variableMaxMeasurementInput[i] = Constraints[i].GetVariableMaxInput();

		bool fixedMeasurementsSet = minMeasurementInput[i] != BodyMeasurement::InvalidValue &&
			maxMeasurementInput[i] != BodyMeasurement::InvalidValue;
		
		bool variableMeasurementsSet = variableMinMeasurementInput[i].first != BodyMeasurement::InvalidValue &&
			variableMinMeasurementInput[i].second != BodyMeasurement::InvalidValue &&
			variableMaxMeasurementInput[i].first != BodyMeasurement::InvalidValue &&
			variableMaxMeasurementInput[i].second != BodyMeasurement::InvalidValue;

		if (!fixedMeasurementsSet && !variableMeasurementsSet)
		{
			missingIndices.push_back(i);
		}
		else if (fixedMeasurementsSet && !variableMeasurementsSet)
		{
			variableMinMeasurementInput[i].first = minMeasurementInput[i];
			variableMinMeasurementInput[i].second = minMeasurementInput[i];
			variableMaxMeasurementInput[i].first = maxMeasurementInput[i];
			variableMaxMeasurementInput[i].second = maxMeasurementInput[i];
		}
		else if (!fixedMeasurementsSet && variableMeasurementsSet)
		{
			minMeasurementInput[i] = variableMinMeasurementInput[i].first;
			maxMeasurementInput[i] = variableMaxMeasurementInput[i].second;
		}
	}

	if (missingIndices.empty())
	{
		return;
	}

	const auto GetMeasurements = [&](const Eigen::VectorXf& pose)
	{
		const DiffData<float> rawControls = poseLogic->EvaluateRawControls(pose);
		const DiffData<float> joints = poseLogic->EvaluateJoints(0, rawControls);
		BodyGeometry<float>::State state;
		rigGeometry->EvaluateBodyGeometry(0, joints, rawControls, state);
		return BodyMeasurement::GetBodyMeasurements(State.m->Constraints, state.Vertices().Matrix(), rawControls.Value());
	};

	Eigen::VectorXf minVals = Eigen::VectorXf::Constant(State.m->Constraints.size(), 1000000.0f);
	Eigen::VectorXf maxVals = Eigen::VectorXf::Constant(State.m->Constraints.size(), -1000000.0f);
	Eigen::VectorXf pose = Eigen::VectorXf::Zero(poseLogic->NumGUIControls());
	const float rawControlRange = 5.f;

	for (size_t i = 0; i < globalIndices.size(); i++)
	{
	    pose.setZero();
		pose[globalIndices[i]] = rawControlRange;
		Eigen::VectorXf measurements = GetMeasurements(pose);
		minVals = minVals.cwiseMin(measurements);
		maxVals = maxVals.cwiseMax(measurements);
		pose[globalIndices[i]] = -rawControlRange;
		measurements = GetMeasurements(pose);
		minVals = minVals.cwiseMin(measurements);
		maxVals = maxVals.cwiseMax(measurements);
	}

	for (int i : missingIndices)
	{
		if (State.m->Constraints[i].GetType() == BodyMeasurement::type_t::Semantic)
		{
			minVals[i] *= 1.5f;
			maxVals[i] *= 1.5f;
		}
        minMeasurementInput[i] = minVals[i];
        maxMeasurementInput[i] = maxVals[i];
		variableMinMeasurementInput[i] = {minVals[i], minVals[i]};
		variableMaxMeasurementInput[i] = {maxVals[i], maxVals[i]};
	}
}

void BodyShapeEditor::Private::ScaleMinMaxMeasurements(const State& State)
{
    const float scaleFactor = State.m->ScaleFactor;
    
    std::vector<bool> applyScaleFactorToMeasurementInput;
    applyScaleFactorToMeasurementInput.reserve(State.m->Constraints.size());
    for (int i = 0; i < Constraints.size(); i++)
    {
        bool shouldApplyScale = Constraints[i].GetType() != BodyMeasurement::Semantic;
        applyScaleFactorToMeasurementInput.push_back(shouldApplyScale);
    }

    // Scale all min/max measurement inputs by the scale factor
    for (size_t i = 0; i < minMeasurementInput.size(); ++i)
    {
        if (applyScaleFactorToMeasurementInput[i])
        {
            minMeasurementInput[i] *= scaleFactor;
            maxMeasurementInput[i] *= scaleFactor;    
        }
    }

    // Scale variable min/max measurement inputs (both elements of the pair)
    for (size_t i = 0; i < variableMinMeasurementInput.size(); ++i)
    {
        if (applyScaleFactorToMeasurementInput[i])
        {
            variableMinMeasurementInput[i].first *= scaleFactor;
            variableMinMeasurementInput[i].second *= scaleFactor;
            variableMaxMeasurementInput[i].first *= scaleFactor;
            variableMaxMeasurementInput[i].second *= scaleFactor;
        }
    }
}

void BodyShapeEditor::EvaluateConstraintRange(const State& State, av::ArrayView<float> MinValues, av::ArrayView<float> MaxValues, bool bScaleWithHeight) const
{
	const auto& constraints = State.m->Constraints;
	if ((MinValues.size() != MaxValues.size()) || (MinValues.size() != constraints.size()))
	{
		CARBON_CRITICAL("Output values buffer is not of right size");
	}
	
	m->SetMinMaxMeasurements(State);
	m->ScaleMinMaxMeasurements(State);

	if (bScaleWithHeight)
	{
		if (m->heightConstraintIndex < 0)
		{
			for (int i = 0; i < constraints.size(); i++)
			{
				const BodyMeasurement& constraint = constraints[i];
				if (constraint.GetName() == "Height" || constraint.GetName() == "height")
				{
					m->heightConstraintIndex = i;
					break;
				}
			}
		}

		if (m->heightConstraintIndex < 0 || m->heightConstraintIndex >= constraints.size())
		{
			CARBON_CRITICAL("Constraints must include a valid Height constraint");
		}

		// Get height constraint range and height measurement. Calculate height ratio to lerp ranges with.
		const auto& measurements = State.GetNamedConstraintMeasurements();

        float height = 177.5f;
        // Get height from constraint target if available, otherwise from measurement 
        if (!State.GetConstraintTarget(m->heightConstraintIndex, height)) 
        {
            height = measurements[m->heightConstraintIndex];
        }
		
		float min = m->variableMinMeasurementInput[m->heightConstraintIndex].first;
		float max = m->variableMaxMeasurementInput[m->heightConstraintIndex].second;
		if (max <= min)
		{
			CARBON_CRITICAL("Height constraint invalid. Max is less than or equal to Min ");
		}
		float heightRatio = (height - min) / (max - min);
		heightRatio = std::clamp(heightRatio, 0.f, 1.f);

		for (int i = 0; i < m->variableMinMeasurementInput.size(); ++i)
		{
			MinValues[i] = std::lerp(m->variableMinMeasurementInput[i].first, m->variableMinMeasurementInput[i].second, heightRatio);
			MaxValues[i] = std::lerp(m->variableMaxMeasurementInput[i].first, m->variableMaxMeasurementInput[i].second, heightRatio);
		}
	}
	else
	{
		std::copy_n(m->minMeasurementInput.data(), m->minMeasurementInput.size(), MinValues.data());
		std::copy_n(m->maxMeasurementInput.data(), m->maxMeasurementInput.size(), MaxValues.data());
	}
}

std::shared_ptr<BodyShapeEditor::State> BodyShapeEditor::RestoreState(trio::BoundedIOStream* InputStream)
{
    auto state = std::shared_ptr<State>(new State());
    state->m->GuiControls = Eigen::VectorX<float>::Zero(m->poseLogic->NumGUIControls());
    state->m->GuiControlsPrior = Eigen::VectorX<float>::Zero(m->poseLogic->NumGUIControls());
    state->m->RawControls = Eigen::VectorX<float>::Zero(m->poseLogic->NumRawControls());
    state->m->Constraints = m->Constraints;
    state->m->JointBindMatrices = m->rigGeometry->GetBindMatrices();
    state->m->JointDeltas = Eigen::Matrix<float, 3, -1>::Zero(3, m->rigGeometry->NumJoints());
    bool success = true;
    MHCBinaryInputArchive archive(InputStream);

    int32_t MagicNumber = { -1 };
    int32_t Version = { -1 };
    archive(MagicNumber);
    archive(Version);
    if (MagicNumber != m->MagicNumber)
    {
        LOG_ERROR("stream does not contain a MHC body state");
        success = false;
    }
    if ((Version < 1) || (Version > 15))
    {
        LOG_ERROR("version {} is not supported", Version);
        success = false;
    }

    SparseMatrix<float> PreviousSkinning;
    if (success)
    {
        if (Version > 3)
        {
            archive(state->m->ModelVersion);
        }
        else
        {
            state->m->ModelVersion = "0.4.4";
        }
        DeserializeEigenMatrix(archive, InputStream, state->m->GuiControls);
        // Captured before any resize so the refit branch below can detect a control-layout
        // change across versions (e.g. a new joint DOF added between releases).
        const int savedGuiControlCount = static_cast<int>(state->m->GuiControls.size());
        if (state->m->GuiControls.size() != m->poseLogic->NumGUIControls())
        {
            state->m->GuiControls.setZero(m->poseLogic->NumGUIControls());
        }
        
        if (Version < 10)
        {
            ResetPoseGuiControls(*state);
        }

        state->m->GuiControlsPrior = state->m->GuiControls;

        // Target Measurements
        if (Version > 3)
        {
            std::size_t targetMeasurementsSize;
            archive(targetMeasurementsSize);
            state->m->TargetMeasurements.reserve(targetMeasurementsSize);
            for (std::size_t i = 0; i < targetMeasurementsSize; i++)
            {
                std::string targetName;
                float targetValue;

                archive(targetName);
                targetName = BodyMeasurement::GetAlias(targetName);
                archive(targetValue);
                auto constraintIter = std::find_if(m->Constraints.begin(), m->Constraints.end(), [&targetName](const auto& Constraint)
                    { return Constraint.GetName() == targetName; });
                if (constraintIter != m->Constraints.end())
                {
                    state->m->TargetMeasurements.push_back({ static_cast<int>(std::distance(m->Constraints.begin(), constraintIter)), targetValue });
                }
            }
        }
        else
        {
            archive(state->m->TargetMeasurements);
            // We cant be sure if indices will be respected in newer version of model, hopefully by then everyone will migrate to 0.4.5
            if ((m->ModelVersion != "0.4.5") && (m->ModelVersion != "0.4.6"))
            {
                state->m->TargetMeasurements.clear();
            }
        }

        DeserializeEigenMatrix(archive, InputStream, state->m->VertexDeltas);
        if (Version > 4)
        {
            DeserializeEigenMatrix(archive, InputStream, state->m->JointDeltas);
            if (state->m->JointDeltas.cols()==0) 
            {
                state->m->JointDeltas = Eigen::Matrix<float, 3, -1>::Zero(3, m->rigGeometry->NumJoints());
            }
            if (Version > 5 && Version < 10){
                Eigen::VectorXf CustomPose;
                DeserializeEigenMatrix(archive, InputStream, CustomPose);
            }
        }
        if (Version > 13)
        {
            DeserializeEigenMatrix(archive, InputStream, state->m->BodySeamDelta);
        }

        Eigen::Matrix<float, 3, -1> vertices;
        DeserializeEigenMatrix(archive, InputStream, vertices);
        Eigen::Matrix<float, 3, -1> jointPositions;
        if (Version > 1)
        {
            DeserializeEigenMatrix(archive, InputStream, jointPositions);
            for (size_t i = 0; i < std::min<int>((int)state->m->JointBindMatrices.size(), (int)jointPositions.cols()); ++i)
            {
                state->m->JointBindMatrices[i].translation() = jointPositions.col(i);
            }
        }

        if (Version > 7 )
        {
            Eigen::Matrix<float, 3 ,-1> allLinear;
            DeserializeEigenMatrix(archive, InputStream, allLinear);
            for (size_t i = 0; i < std::min<int>((int)state->m->JointBindMatrices.size(), (int)allLinear.cols()); ++i)
            {
                state->m->JointBindMatrices[i].linear() = allLinear.block<3,3>(0, i * 3);
            }
        }

        if (Version > 2 && Version < 7)
        {
            Eigen::VectorXf translation;
            DeserializeEigenMatrix(archive, InputStream, translation);
            if (translation.size() >= 3)
            {
                const auto& guiControlNames = m->poseLogic->GuiControlNames();
                const std::array<std::string, 3> translationControlNames = { "pose_rigid_root.tx", "pose_rigid_root.ty", "pose_rigid_root.tz" };
                for (int i = 0; i < 3; ++i)
                {
                    auto it = std::find(guiControlNames.begin(), guiControlNames.end(), translationControlNames[i]);
                    if (it != guiControlNames.end())
                    {
                        state->m->GuiControls[std::distance(guiControlNames.begin(), it)] = translation[i];
                    }
                }
            }
        }

        if (Version > 4)
        {
            archive(state->m->VertexDeltaScale);
            archive(state->m->FloorOffsetApplied);
        }

        // We need to update the state to new model version, OR to a new rig control
        // layout (NumGUIControls changed — e.g. a joint gained a DOF). Both break any
        // positional mapping between the saved GuiControls and the current pose logic,
        // so the safest thing is to re-solve the identity against the saved mesh in
        // the new control space. pose_driver_* stays locked so the solve only touches
        // shape + rigid.
        bool bRefitted = false;
        const bool controlLayoutChanged = (savedGuiControlCount != m->poseLogic->NumGUIControls());
        if (state->m->ModelVersion != m->ModelVersion || controlLayoutChanged)
        {
            FitToTargetOptions options;
            auto previousPCA = state->m->GuiControls;
            
            // lock to a pose before solving
            const auto& guiControlNames = m->poseLogic->GuiControlNames();
            std::vector<int> poseIndices;
            for (int i = 0; i < static_cast<int>(guiControlNames.size()); ++i)
            {
                if (guiControlNames[i].find("pose_driver") == 0 || guiControlNames[i].starts_with("pose_rigid_pelvis"))
                    poseIndices.push_back(i);
            }
            state->SetLockedControlIndices(poseIndices);
            SolveForTemplateMesh(*state, vertices, options);
            bRefitted = true;
            state->ClearLockedControls();

            // Order matters: SetNeutralJointsTranslations writes JointDeltas which rebakes
            // the joint bind positions and therefore shifts the skinning output. If we ran
            // SetNeutralMesh first, the vertex residual it wrote would go stale the moment
            // joint deltas land. Do joints first, mesh residual last — then VertexDeltas
            // closes the final gap to the saved mesh exactly, giving back-compat round-trips
            // within FP precision instead of the ~0.001 drift we'd otherwise leak.
            SetNeutralJointsTranslations(*state, jointPositions);
            SetNeutralMesh(*state, vertices);
            if(m->skinningModels.contains(state->m->ModelVersion))
            {
                PreviousSkinning = m->skinningModels[state->m->ModelVersion].calculateResult(previousPCA(m->globalIndices));
            }
            const auto& newMesurements = state->GetNamedConstraintMeasurements();
            for (auto& [k, v] : state->m->TargetMeasurements)
            {
                v = newMesurements[k];
            }
        }
        if (Version > 8)
        {
            archive(state->m->LockedControlIndices);
        }

        if (Version > 10)
        {
            archive(state->m->faceBlend);
            bool hasFaceState = false;
            archive(hasFaceState);
            if (hasFaceState && m->facePatchBlendModel)
            {
                state->m->faceState = m->facePatchBlendModel->CreateState();
                Eigen::VectorXf faceStateData;
                DeserializeEigenMatrix(archive, InputStream, faceStateData);
                state->m->faceState->DeserializeFromVector(faceStateData);
            }
            else if (hasFaceState && !m->facePatchBlendModel)
            {
                // File contains faceStateData bytes but we don't have a patch
                // blend model to deserialise them. Skipping would misalign
                // every subsequent field (ScaleFactor, EvaluatePose). Drain
                // the bytes into a throwaway vector so the stream cursor
                // lands on the right offset.
                Eigen::VectorXf throwaway;
                DeserializeEigenMatrix(archive, InputStream, throwaway);
            }
            else if (m->facePatchBlendModel)
            {
                state->m->faceState = m->facePatchBlendModel->CreateState();
            }
        }
        else if (m->facePatchBlendModel)
        {
            state->m->faceState = m->facePatchBlendModel->CreateState();
        }

        float scaleFactor = 1.0f;
        if (Version > 11)
        {
            archive(scaleFactor);
        }
        if (!bRefitted)
        {
            state->m->ScaleFactor = scaleFactor;
        }

        if (Version > 14)
        {
            archive(state->m->EvaluatePose);
        }
    }

    EvaluateState(*state);
    state->m->CustomSkinning = PreviousSkinning;

    return state;
}

void BodyShapeEditor::DumpState(const State& state, trio::BoundedIOStream* OutputStream)
{
    MHCBinaryOutputArchive archive { OutputStream };

    int32_t Version = 15;
    archive(m->MagicNumber);
    archive(Version);
    archive(m->ModelVersion);
    // archive gui controls
    SerializeEigenMatrix(archive, OutputStream, state.m->GuiControls);

    // archive the target measurements
    archive(state.m->TargetMeasurements.size());
    for (const auto& [k, v] : state.m->TargetMeasurements)
    {
        archive(m->Constraints[k].GetName());
        archive(v);
    }
    // archive the vertex deltas
    SerializeEigenMatrix(archive, OutputStream, state.m->VertexDeltas);
    SerializeEigenMatrix(archive, OutputStream, state.m->JointDeltas);
    SerializeEigenMatrix(archive, OutputStream, state.m->BodySeamDelta);
    // archive the actual body vertices (in order to be able to reconstruct the body in a future release)
    SerializeEigenMatrix(archive, OutputStream, state.m->RigMeshes[0].Vertices());
    // archive the joint positions
    Eigen::Matrix<float, 3, -1> jointPositions(3, (int)state.m->JointBindMatrices.size());
    Eigen::Matrix<float, 3, -1> allLinear(3, 3 * (int)state.m->JointBindMatrices.size());
    for (size_t i = 0; i < state.m->JointBindMatrices.size(); ++i)
    {
        jointPositions.col(i) = state.m->JointBindMatrices[i].translation();
        allLinear.block<3,3>(0, i * 3) = state.m->JointBindMatrices[i].linear();
    }
    SerializeEigenMatrix(archive, OutputStream, jointPositions);
    SerializeEigenMatrix(archive, OutputStream, allLinear);
    archive(state.m->VertexDeltaScale);
    archive(state.m->FloorOffsetApplied);
    archive(state.m->LockedControlIndices);

    archive(state.m->faceBlend);
    bool hasFaceState = state.m->faceState.has_value();
    archive(hasFaceState);
    if (hasFaceState)
    {
        Eigen::VectorXf faceStateData = state.m->faceState->SerializeToVector();
        SerializeEigenMatrix(archive, OutputStream, faceStateData);
    }

    archive(state.m->ScaleFactor);
    archive(state.m->EvaluatePose);
}

template <typename T>
std::pair<T, Eigen::RowVectorX<T>> EvaluateLength(const Eigen::Matrix<T, 3, -1>& vertices,
    const Eigen::SparseMatrix<T, Eigen::RowMajor>& evaluationMatrix,
    const std::vector<BarycentricCoordinates<T>>& lines)
{
    T length = 0;
    Eigen::RowVectorX<T> jacobian = Eigen::RowVectorX<T>::Zero(evaluationMatrix.cols());

    for (int j = 0; j < (int)lines.size() - 1; j++)
    {
        const BarycentricCoordinates<float>& b0 = lines[j];
        const BarycentricCoordinates<float>& b1 = lines[j + 1];
        const Eigen::Vector3<T> segment = b1.template Evaluate<3>(vertices) - b0.template Evaluate<3>(vertices);
        const T segmentLength = segment.norm();
        const T segmentWeight = segmentLength > T(1e-9f) ? T(1) / segmentLength : T(0);
        length += segmentLength;

        for (int d = 0; d < 3; d++)
        {
            T b0w = (T)b0.Weight(d);
            T b1w = (T)b1.Weight(d);
            jacobian += (-segmentWeight * segment[0] * b0w) * evaluationMatrix.row(3 * b0.Index(d) + 0);
            jacobian += (-segmentWeight * segment[1] * b0w) * evaluationMatrix.row(3 * b0.Index(d) + 1);
            jacobian += (-segmentWeight * segment[2] * b0w) * evaluationMatrix.row(3 * b0.Index(d) + 2);
            jacobian += (segmentWeight * segment[0] * b1w) * evaluationMatrix.row(3 * b1.Index(d) + 0);
            jacobian += (segmentWeight * segment[1] * b1w) * evaluationMatrix.row(3 * b1.Index(d) + 1);
            jacobian += (segmentWeight * segment[2] * b1w) * evaluationMatrix.row(3 * b1.Index(d) + 2);
        }
    }

    return { length, jacobian };
}

template <typename T>
std::pair<T, Eigen::RowVectorX<T>> EvaluateDistance(const Eigen::Matrix<T, 3, -1>& vertices,
    const Eigen::SparseMatrix<T, Eigen::RowMajor>& evaluationMatrix,
    int vID1,
    int vID2)
{
    Eigen::RowVectorX<T> jacobian = evaluationMatrix.row(3 * vID2 + 1) - evaluationMatrix.row(3 * vID1 + 1);
    return { vertices(1, vID2) - vertices(1, vID1), jacobian };
}

template <typename T>
std::pair<T, Eigen::RowVectorX<T>> EvaluateSemantic(const Eigen::VectorX<T>& rawControls, const Eigen::VectorX<T>& weights)
{
    Eigen::RowVectorX<T> jacobian = weights.transpose();
    return { rawControls.head(weights.size()).dot(weights), jacobian };
}

std::vector<int> GetUsedVertexIndices(int numVertices, const std::vector<BodyMeasurement>& measurements)
{
    std::vector<int> indices;
    indices.reserve(numVertices);

    std::vector<bool> used(numVertices, false);
    for (size_t i = 0; i < measurements.size(); i++)
    {
        for (const auto& b : measurements[i].GetBarycentricCoordinates())
        {
            used[b.Index(0)] = true;
            used[b.Index(1)] = true;
            used[b.Index(2)] = true;
        }
        for (size_t j = 0; j < measurements[i].GetVertexIDs().size(); ++j)
        {
            used[measurements[i].GetVertexIDs()[j]] = true;
        }
    }
    for (int vID = 0; vID < (int)used.size(); ++vID)
    {
        if (used[vID])
        {
            indices.push_back(vID);
        }
    }

    return indices;
}

void BodyShapeEditor::Solve(State& State, float priorWeight, const int /*iterations*/) const
{
    const int numVertices = m->rigGeometry->GetMesh(0).NumVertices();
    // list of vertices that's being controlled
    const std::vector<int> indices = GetUsedVertexIndices(numVertices, State.m->Constraints);

    Eigen::VectorXf symControls = m->symControls->GuiToSymmetricControls(State.m->GuiControls);
    const auto& symToGuiMat = m->symControls->SymmetricToGuiControlsMatrix();
    const auto& guiToRawMat = m->guiToRawMappingMatrix;
    Eigen::SparseMatrix<float, Eigen::RowMajor> symToRawMat = guiToRawMat * symToGuiMat;
    const Eigen::SparseMatrix<float, Eigen::RowMajor>& symEvalMat = m->symmetricIdentityVertexEvaluationMatrix;
    Eigen::Matrix<float, 3, -1> currVertices = m->rigGeometry->GetMesh(0).Vertices();

    Eigen::MatrixXf AtA(symControls.size(), symControls.size());
    Eigen::VectorXf Atb(symControls.size());

    const int numIterationsSteps = 2;
    std::set<std::string> involvedConstraintNames = {};
    for (const auto& [constraintNames, affectedRegions] : m->solveSteps)
    {
        involvedConstraintNames.insert(constraintNames.begin(), constraintNames.end());
        std::vector<bool> usedSymmetricControls(symControls.size(), false);
        int affected = 0;
        for (const std::string& regionName : affectedRegions)
        {
            bool found = false;
            auto it = m->shapePcaControls.find(regionName);
            if (it != m->shapePcaControls.end())
            {
                found = true;
                for (int rawControl : it->second)
                {
                    int guiControl = m->rawToGuiControls[rawControl];
                    for (Eigen::SparseMatrix<float, Eigen::RowMajor>::InnerIterator itMat(symToGuiMat, guiControl); itMat; ++itMat)
                    {
                        usedSymmetricControls[itMat.col()] = true;
                        affected++;
                    }
                }
            }
            it = m->skeletonPcaControls.find(regionName);
            if (it != m->skeletonPcaControls.end())
            {
                found = true;
                for (int rawControl : it->second)
                {
                    int guiControl = m->rawToGuiControls[rawControl];
                    for (Eigen::SparseMatrix<float, Eigen::RowMajor>::InnerIterator itMat(symToGuiMat, guiControl); itMat; ++itMat)
                    {
                        usedSymmetricControls[itMat.col()] = true;
                        affected++;
                    }
                }
            }
            if (!found)
            {
                for (int guiControl = 0; guiControl < symToGuiMat.outerSize(); ++guiControl)
                {
                    for (Eigen::SparseMatrix<float, Eigen::RowMajor>::InnerIterator itMat(symToGuiMat, guiControl); itMat; ++itMat)
                    {
                        usedSymmetricControls[itMat.col()] = true;
                        affected++;
                    }
                }
            }
        }

        for (int iter = 0; iter < numIterationsSteps; ++iter)
        {
            Eigen::VectorXf guiControls = symToGuiMat * symControls;
            Eigen::VectorXf rawControls = guiToRawMat * guiControls;

            AtA.setZero();
            Atb.setZero();

            // prior cost
            if (priorWeight > 0)
            {
                if (State.m->GuiControlsPrior.size() != guiControls.size())
                {
                    State.m->GuiControlsPrior = Eigen::VectorXf::Zero(guiControls.size());
                }
                Eigen::SparseMatrix<float, Eigen::RowMajor> gwmFullMat = m->gwm * symToGuiMat;
                AtA += priorWeight * (gwmFullMat.transpose() * gwmFullMat);
                Atb += priorWeight * (gwmFullMat.transpose() * m->gwm * (State.m->GuiControlsPrior - symToGuiMat * symControls));
            }

            for (int vID : indices)
            {
                currVertices(0, vID) = m->rigGeometry->GetMesh(0).Vertices()(0, vID) + symEvalMat.row(3 * vID + 0) * symControls;
                currVertices(1, vID) = m->rigGeometry->GetMesh(0).Vertices()(1, vID) + symEvalMat.row(3 * vID + 1) * symControls;
                currVertices(2, vID) = m->rigGeometry->GetMesh(0).Vertices()(2, vID) + symEvalMat.row(3 * vID + 2) * symControls;
            }

            for (const auto& keyValuePair : State.m->TargetMeasurements)
            {
                int ConstraintIndex = keyValuePair.first;
                float ConstraintWeight = keyValuePair.second;
                if (involvedConstraintNames.count(State.m->Constraints[ConstraintIndex].GetName()) > 0)
                {
                    if (State.m->Constraints[ConstraintIndex].GetType() == BodyMeasurement::Axis)
                    {
                        // axis costs
                        auto [dist, jacobian] = EvaluateDistance<float>(currVertices,
                            symEvalMat,
                            State.m->Constraints[ConstraintIndex].GetVertexIDs()[0],
                            State.m->Constraints[ConstraintIndex].GetVertexIDs()[1]);
                        AtA.template triangularView<Eigen::Lower>() += jacobian.transpose() * jacobian;
                        Atb += jacobian.transpose() * (ConstraintWeight - dist);
                    }
                    else if (State.m->Constraints[ConstraintIndex].GetType() == BodyMeasurement::Semantic)
                    {
                        // semantic costs
                        const int numLocalControls = static_cast<int>(m->rawLocalIndices.size());
                        auto [value, jacobian] = EvaluateSemantic<float>(rawControls, State.m->Constraints[ConstraintIndex].GetWeights());
                        Eigen::RowVectorXf symJacobian = jacobian * symToRawMat.topRows(numLocalControls);
                        float diff = ConstraintWeight - value;
                        AtA.template triangularView<Eigen::Lower>() += State.m->SemanticWeight * (symJacobian.transpose() * symJacobian);
                        Atb += State.m->SemanticWeight * (symJacobian.transpose() * diff);
                    }
                    else
                    {
                        // length costs
                        auto [value, jacobian] = EvaluateLength(currVertices, symEvalMat, State.m->Constraints[ConstraintIndex].GetBarycentricCoordinates());
                        float diff = ConstraintWeight - value;
                        AtA.template triangularView<Eigen::Lower>() += (jacobian.transpose() * jacobian);
                        Atb += (jacobian.transpose() * diff);
                    }
                }
            }

            for (int i = 0; i < (int)usedSymmetricControls.size(); ++i)
            {
                if (!usedSymmetricControls[i])
                {
                    AtA.col(i).setZero();
                    AtA.row(i).setZero();
                    Atb(i) = 0;
                }
            }

            // regularization
            for (int i = 0; i < AtA.cols(); ++i)
            {
                AtA(i, i) += 1e-2f;
            }

            // solve
            const Eigen::VectorXf dx = AtA.template selfadjointView<Eigen::Lower>().llt().solve(Atb);
            symControls += dx;
        }
    }

    State.m->GuiControls = symToGuiMat * symControls;
    State.m->RawControls = m->poseLogic->EvaluateRawControls(DiffData<float>(State.m->GuiControls)).Value();
    const auto& rawControls = State.m->RawControls;
    const float rawMean = rawControls.mean();
    const float rawStdDev = std::sqrt((rawControls.array() - rawMean).square().sum() / rawControls.size());
    for (int i = 0; i < State.m->RawControls.size(); ++i)
    {
        const auto& rawControlName = m->poseLogic->RawControlNames()[i];
        if (rawControlName.starts_with("local_groin") || rawControlName.starts_with("local_pelvis_0"))
        {
            auto& groinControl = State.m->RawControls[i];
            if (groinControl < rawMean - 2 * rawStdDev)
            {
                groinControl = rawMean - 2 * rawStdDev;
            }
            else if (groinControl > rawMean + 2 * rawStdDev)
            {
                groinControl = rawMean + 2 * rawStdDev;
            }
        }
    }
    UpdateGuiFromRawControls(State);
    EvaluateState(State);
}

void BodyShapeEditor::ComputePosedRBFJointGroups(const State& State, dna::Writer* InOutDnaWriter) const
{
    constexpr float rad2deg = float(180.0 / CARBON_PI);
    const uint16_t numJoints = m->rigGeometry->NumJoints();
    constexpr int dofPerJoint = 9;
    const int numJointDOFs = numJoints * dofPerJoint;
    const auto& jointHierarchy = m->rigGeometry->GetJointParentIndices();

    // evaluate the rig at the current custom pose to get base joint values and RBF controls
    const DiffData<float> baseJoints = m->poseLogic->EvaluateJoints(0, State.m->RawControls);
    const DiffData<float> rbfControlsAtPose = m->rbfLogic->EvaluatePoseControlsFromJoints(baseJoints, true);
    const Eigen::VectorXf c0 = rbfControlsAtPose.Value();
    const int numRBFControls = static_cast<int>(c0.size());

    // get the custom pose WorldMatrices as our reference point
    const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& W0 = State.GetWorldMatrices();

    // extract local-space 9-DOF joint deltas from world matrices relative to reference W0
    // returns delta translations and delta Euler rotations (in radians) for each joint
    auto extractLocalDeltaDOFs = [&](const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& W) -> Eigen::VectorXf
    {
        Eigen::VectorXf dofs = Eigen::VectorXf::Zero(numJointDOFs);
        for (uint16_t ji = 0; ji < numJoints; ++ji)
        {
            // compute local transforms for both reference and perturbed
            Eigen::Transform<float, 3, Eigen::Affine> localRef, localPerturbed;
            const int parentIndex = jointHierarchy[ji];
            if (parentIndex >= 0)
            {
                localRef = W0[parentIndex].inverse() * W0[ji];
                localPerturbed = W[parentIndex].inverse() * W[ji];
            }
            else
            {
                localRef = W0[ji];
                localPerturbed = W[ji];
            }

            // translation delta
            Eigen::Vector3f dt = localPerturbed.translation() - localRef.translation();
            dofs[ji * dofPerJoint + 0] = dt.x();
            dofs[ji * dofPerJoint + 1] = dt.y();
            dofs[ji * dofPerJoint + 2] = dt.z();

            // rotation delta: extract relative rotation localRef^-1 * localPerturbed
            Eigen::Matrix3f deltaR = localRef.linear().transpose() * localPerturbed.linear();
            Eigen::Vector3f deltaEuler = RotationMatrixToEulerXYZ<float>(deltaR);
            dofs[ji * dofPerJoint + 3] = deltaEuler.x();
            dofs[ji * dofPerJoint + 4] = deltaEuler.y();
            dofs[ji * dofPerJoint + 5] = deltaEuler.z();

            // scale delta (typically zero for body, but compute for completeness)
            // extract scale from linear part: scale = column norms of rotation-free part
            // for small perturbations, approximate as identity
            dofs[ji * dofPerJoint + 6] = 0.0f;
            dofs[ji * dofPerJoint + 7] = 0.0f;
            dofs[ji * dofPerJoint + 8] = 0.0f;
        }
        return dofs;
    };

    // compute finite-difference Jacobian
    Eigen::MatrixXf jacobian = Eigen::MatrixXf::Zero(numJointDOFs, numRBFControls);

    constexpr float epsilon = 0.01f;

    for (int k = 0; k < numRBFControls; ++k)
    {
        // positive perturbation
        Eigen::VectorXf cPlus = c0;
        cPlus[k] += epsilon;
        const DiffData<float> rbfJointsPlus = m->poseLogic->EvaluateRbfJoints(0, DiffData<float>(cPlus));
        const DiffData<float> twistedJointsPlus = m->twistSwingLogic->EvaluateJointsFromJoints(baseJoints + rbfJointsPlus);
        BodyGeometry<float>::State geoStatePlus;
        m->rigGeometry->EvaluateBodyGeometry(0, twistedJointsPlus, State.m->RawControls, geoStatePlus);
        const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& WPlus = geoStatePlus.GetWorldMatrices();

        // negative perturbation
        Eigen::VectorXf cMinus = c0;
        cMinus[k] -= epsilon;
        const DiffData<float> rbfJointsMinus = m->poseLogic->EvaluateRbfJoints(0, DiffData<float>(cMinus));
        const DiffData<float> twistedJointsMinus = m->twistSwingLogic->EvaluateJointsFromJoints(baseJoints + rbfJointsMinus);
        BodyGeometry<float>::State geoStateMinus;
        m->rigGeometry->EvaluateBodyGeometry(0, twistedJointsMinus, State.m->RawControls, geoStateMinus);
        const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& WMinus = geoStateMinus.GetWorldMatrices();

        // central difference
        Eigen::VectorXf dofsPlus = extractLocalDeltaDOFs(WPlus);
        Eigen::VectorXf dofsMinus = extractLocalDeltaDOFs(WMinus);
        jacobian.col(k) = (dofsPlus - dofsMinus) / (2.0f * epsilon);
    }

    // write the Jacobian as joint group values to DNA
    for (std::uint16_t jg = 0; jg < m->jointGroupInputIndices.size(); ++jg)
    {
        CARBON_ASSERT(jg < m->jointGroupOutputIndices.size());
        CARBON_ASSERT(jg < m->jointGroupLODs.size());
        const auto& inputIndices = m->jointGroupInputIndices[jg];
        const auto& outputIndices = m->jointGroupOutputIndices[jg];
        const auto& lods = m->jointGroupLODs[jg];
        std::vector<float> values;
        values.reserve(inputIndices.size() * outputIndices.size());
        for (const auto oi : outputIndices)
        {
            float multiplier = 1.0f;
            if (oi % 9 > 2 && oi % 9 < 6) // rotations
            {
                multiplier = rad2deg;
            }
            for (const auto ii : inputIndices)
            {
                // map DNA input index to Jacobian column (RBF control index)
                const int rbfControlIndex = ii - m->rbfControlOffset;
                const float jacobianValue = (rbfControlIndex >= 0 && rbfControlIndex < numRBFControls) ? jacobian(oi, rbfControlIndex) : 0.0f;
                values.push_back(jacobianValue * multiplier);
            }
        }
        InOutDnaWriter->setJointGroupValues(jg, values.data(), static_cast<std::uint32_t>(values.size()));
        InOutDnaWriter->setJointGroupInputIndices(jg, inputIndices.data(), static_cast<std::uint16_t>(inputIndices.size()));
        InOutDnaWriter->setJointGroupOutputIndices(jg, outputIndices.data(), static_cast<std::uint16_t>(outputIndices.size()));
        InOutDnaWriter->setJointGroupLODs(jg, lods.data(), static_cast<std::uint16_t>(lods.size()));
    }
}

void BodyShapeEditor::StateToDna(const State& State, dna::Writer* InOutDnaWriter, bool combinedBodyAndFace, bool usePosedJoints) const
{
    std::vector<SparseMatrix<float>> vertexInfluenceWeights;
    GetVertexInfluenceWeights(State, vertexInfluenceWeights);
    if (vertexInfluenceWeights.size() > 0)
    {
        // update the skinning weights in the DNA
        for (int lod = 0; lod < NumLODs(); ++lod)
        {
            InOutDnaWriter->clearSkinWeights((uint16_t)lod);
            int numVertices = static_cast<int>(vertexInfluenceWeights[size_t(lod)].rows());
            if (combinedBodyAndFace)
            {
                // write skin weights for the combined face and body
                for (int vID = numVertices - 1; vID >= 0; --vID)
                {
                    std::vector<float> weights;
                    std::vector<uint16_t> indices;
                    for (typename SparseMatrix<float>::InnerIterator it(vertexInfluenceWeights[size_t(lod)], vID); it; ++it)
                    {
                        if (it.value() != float(0))
                        {
                            weights.push_back(it.value());
                            indices.emplace_back((uint16_t)it.col());
                        }
                    }

                    InOutDnaWriter->setSkinWeightsValues(lod, vID, weights.data(), (uint16_t)weights.size());
                    InOutDnaWriter->setSkinWeightsJointIndices(lod, vID, indices.data(), (uint16_t)indices.size());
                }
            }
            else
            {
                // map weights from combined to headless body
                for (int vID = numVertices - 1; vID >= 0; --vID)
                {
                    if (m->combinedToBodyMapping[lod].find(vID) != m->combinedToBodyMapping[lod].end())
                    {
                        int bodyVID = m->combinedToBodyMapping[lod][vID];
                        std::vector<float> weights;
                        std::vector<uint16_t> indices;

                        for (typename SparseMatrix<float>::InnerIterator it(vertexInfluenceWeights[size_t(lod)], vID); it; ++it)
                        {
                            if (it.value() != float(0))
                            {
                                weights.push_back(it.value());
                                indices.emplace_back((uint16_t)it.col());
                            }
                        }

                        InOutDnaWriter->setSkinWeightsValues(lod, bodyVID, weights.data(), (uint16_t)weights.size());
                        InOutDnaWriter->setSkinWeightsJointIndices(lod, bodyVID, indices.data(), (uint16_t)indices.size());
                    }
                }
            }
        }
    }


    for (int lod = 0; lod < NumLODs(); ++lod)
    {
        const uint16_t meshIndex = static_cast<uint16_t>(lod);
        if (combinedBodyAndFace)
        {
            const Eigen::Matrix3Xf& bodyVertices = State.GetMesh(lod).Vertices();
            const Eigen::Matrix3Xf& bodyNormals = State.GetMesh(lod).VertexNormals();
            InOutDnaWriter->setVertexPositions(meshIndex, (const dna::Position*)bodyVertices.data(), uint32_t(bodyVertices.cols()));
            InOutDnaWriter->setVertexNormals(meshIndex, (const dna::Position*)bodyNormals.data(), uint32_t(bodyNormals.cols()));
        }
        else
        {
            const std::vector<int>& bodyToCombinedMapping = GetBodyToCombinedMapping(static_cast<int>(lod));
            const Eigen::Matrix3Xf bodyVertices = State.GetMesh(lod).Vertices()(Eigen::all, bodyToCombinedMapping);
            const Eigen::Matrix3Xf bodyNormals = State.GetMesh(lod).VertexNormals()(Eigen::all, bodyToCombinedMapping);
            InOutDnaWriter->setVertexPositions(meshIndex, (const dna::Position*)bodyVertices.data(), uint32_t(bodyVertices.cols()));
            InOutDnaWriter->setVertexNormals(meshIndex, (const dna::Position*)bodyNormals.data(), uint32_t(bodyNormals.cols()));
        }
    }

    // Set neutral joints
    constexpr float rad2deg = float(180.0 / CARBON_PI);

    uint16_t numJoints = m->rigGeometry->NumJoints();

    Eigen::Matrix<float, 3, -1> jointTranslations(3, numJoints);
    Eigen::Matrix<float, 3, -1> jointRotations(3, numJoints);

    const auto& jointHierarchy = m->rigGeometry->GetJointParentIndices();

    const auto getJointParent = [&jointHierarchy](std::uint16_t jointIndex)
    {
        return jointHierarchy[jointIndex];
    };

	const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices = usePosedJoints ? State.GetWorldMatrices() : State.GetJointBindMatrices();

    for (std::uint16_t jointIndex = 0; jointIndex < numJoints; jointIndex++)
    {
        Eigen::Transform<float, 3, Eigen::Affine> localTransform;
        const int parentJointIndex = getJointParent(jointIndex);
        if (parentJointIndex >= 0)
        {
            auto parentTransform = jointMatrices[parentJointIndex];
            localTransform = parentTransform.inverse() * jointMatrices[jointIndex];
        }
        else
        {
            localTransform = jointMatrices[jointIndex];
        }

        jointTranslations.col(jointIndex) = localTransform.translation();
        jointRotations.col(jointIndex) = rad2deg * RotationMatrixToEulerXYZ<float>(localTransform.linear());
    }

    InOutDnaWriter->setNeutralJointTranslations((dna::Vector3*)jointTranslations.data(), numJoints);
    InOutDnaWriter->setNeutralJointRotations((dna::Vector3*)jointRotations.data(), numJoints);

    if (m->rbfPCA.mods.size() > 0)
    {
    	if (usePosedJoints)
    	{
    		ComputePosedRBFJointGroups(State, InOutDnaWriter);
    	}
    	else
    	{
    		auto rbfMatrix = m->rbfPCA.calculateResult(State.m->GuiControls(m->globalIndices));
    		const float uniformScale = (State.m->ScaleFactor != 0.0f) ? State.m->ScaleFactor : 1.0f;
    		for (std::uint16_t jg = 0; jg < m->jointGroupInputIndices.size(); ++jg)
    		{
    			assert(jg < m->jointGroupOutputIndices.size());
    			assert(jg < m->jointGroupLODs.size());
    			const auto& inputIndices = m->jointGroupInputIndices[jg];
    			const auto& outputIndices = m->jointGroupOutputIndices[jg];
    			const auto& lods = m->jointGroupLODs[jg];
    			std::vector<float> values;
    			values.reserve(inputIndices.size() * outputIndices.size());
    			for (const auto oi : outputIndices)
    			{
    				const int dof = oi % 9;
    				float multiplier = 1.0f;
    				if (dof < 3)
    				{
    					// translation rows — pre-scale by character UniformScale.
    					// Internally these deltas live unscaled and get folded
    					// through ScaleVertices/ScaleJointMatrices at eval. The
    					// DNA consumer doesn't apply UniformScale, so write the
    					// already-scaled magnitude so the on-disk values match
    					// the rendered mesh's joint deltas.
    					multiplier = uniformScale;
    				}
    				else if (dof < 6)
    				{
    					multiplier = rad2deg;
    				}
    				for (const auto ii : inputIndices)
    				{
    					values.push_back(rbfMatrix.coeff(oi, ii - m->rbfControlOffset) * multiplier);
    				}
    			}
    			InOutDnaWriter->setJointGroupValues(jg, values.data(), static_cast<std::uint32_t>(values.size()));
    			InOutDnaWriter->setJointGroupInputIndices(jg, inputIndices.data(), static_cast<std::uint16_t>(inputIndices.size()));
    			InOutDnaWriter->setJointGroupOutputIndices(jg, outputIndices.data(), static_cast<std::uint16_t>(outputIndices.size()));
    			InOutDnaWriter->setJointGroupLODs(jg, lods.data(), static_cast<std::uint16_t>(lods.size()));
    		}
    	}
    }
}

int BodyShapeEditor::NumJoints() const
{
    return m->rigGeometry->NumJoints();
}

const std::vector<std::string>& BodyShapeEditor::GetJointNames() const
{
    return m->rigGeometry->GetJointNames();
}

const std::vector<int>& BodyShapeEditor::GetJointParentIndices() const
{
    return m->rigGeometry->GetJointParentIndices();
}

void BodyShapeEditor::GetNeutralJointTransform(const State& State,
    std::uint16_t JointIndex,
    Eigen::Vector3f& OutJointTranslation,
    Eigen::Vector3f& OutJointRotation) const
{
	GetNeutralJointTransform(State.GetJointBindMatrices(), JointIndex, OutJointTranslation, OutJointRotation);
}

void BodyShapeEditor::GetNeutralJointTransform(const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& BindPoseMatrices, std::uint16_t JointIndex, Eigen::Vector3f& OutJointTranslation, Eigen::Vector3f& OutJointRotation) const
{
	if (JointIndex >= m->rigGeometry->NumJoints())
	{
		CARBON_CRITICAL("JointIndex out of range");
	}

	if (BindPoseMatrices.size() < m->rigGeometry->NumJoints())
	{
		CARBON_CRITICAL("Invalid number of BindPoseMatrices");
	}
	
	const auto& jointHierarchy = m->rigGeometry->GetJointParentIndices();

	Eigen::Transform<float, 3, Eigen::Affine> localTransform;
	const int parentJointIndex = jointHierarchy[JointIndex];
	if (parentJointIndex >= 0)
	{
		auto parentTransform = BindPoseMatrices[parentJointIndex];
		localTransform = parentTransform.inverse() * BindPoseMatrices[JointIndex];
	}
	else
	{
		localTransform = BindPoseMatrices[JointIndex];
	}

	OutJointTranslation = localTransform.translation();
	constexpr float rad2deg = float(180.0 / CARBON_PI);
	OutJointRotation =  rad2deg * RotationMatrixToEulerXYZ<float>(localTransform.linear());
}

Eigen::Matrix<float, 3, -1> ScatterCol(const Eigen::Matrix<float, 3, -1>& input, const std::vector<int>& ids, int numCols)
{
    Eigen::Matrix<float, 3, -1> target = Eigen::Matrix<float, 3, -1>::Zero(3, numCols);
    for (int i = 0; i < (int)ids.size(); ++i)
    {
        target.col(ids[i]) = input.col(i);
    }
    return target;
}


void BodyShapeEditor::SetCustomGeometryToState(State& State, std::shared_ptr<const BodyGeometry<float>> Geometry, bool Fit)
{
    if (!Geometry)
    {
        return;
    }

    if (Fit)
    {
        FitToTargetOptions fitToTargetOptions;

        const int numVertices = Geometry->GetMesh(0).NumVertices();
        std::vector<int> mapping(numVertices);
        std::iota(mapping.begin(), mapping.end(), 0);
        Eigen::Matrix3Xf InJoints = Eigen::Matrix3Xf::Zero(3, Geometry->NumJoints());
        for (int i = 0; i < Geometry->NumJoints(); ++i)
        {
            InJoints.col(i) = Geometry->GetBindMatrices()[i].translation();
        }
        SolveForTemplateMesh(State, Geometry->GetMesh(0).Vertices(),fitToTargetOptions);
        SetNeutralJointsTranslations(State, InJoints);
    }
    else
    {
        State.m->RigMeshes.resize(Geometry->GetNumLODs());
        for (int lod = 0; lod < Geometry->GetNumLODs(); ++lod)
        {
            State.m->RigMeshes[lod].SetTriangles(m->meshTriangles[lod]);
            State.m->RigMeshes[lod].SetVertices(Geometry->GetMesh(lod).Vertices());
            State.m->RigMeshes[lod].CalculateVertexNormals(false, VertexNormalComputationType::AreaWeighted, /*stableNormalize=*/true, m->threadPool.get());
        }
        State.m->JointBindMatrices = Geometry->GetBindMatrices();
    	State.m->WorldMatrices = Geometry->GetBindMatrices();
    }
}

const std::vector<std::string>& BodyShapeEditor::GetRegionNames() const
{
    return m->regionNames;
}

bool BodyShapeEditor::Blend(State& state, int RegionIndex, const std::vector<std::pair<float, const State*>>& States, BodyAttribute Type)
{
    const int numRegions = (int)m->regionNames.size();
    Eigen::VectorXf rawControls = state.m->RawControls;
    Eigen::Matrix3Xf VertexDeltas = state.m->VertexDeltas;
    Eigen::Matrix3Xf BodySeamDelta = state.m->BodySeamDelta;
    Eigen::Matrix3Xf JointDeltas = state.m->JointDeltas;
    float ScaleFactor = state.m->ScaleFactor;

    if (VertexDeltas.size() == 0)
    {
        VertexDeltas = Eigen::Matrix3Xf::Zero(3, m->rigGeometry->GetMesh(0).NumVertices());
    }
    if (BodySeamDelta.size() == 0)
    {
        BodySeamDelta = Eigen::Matrix3Xf::Zero(3, m->rigGeometry->GetMesh(0).NumVertices());
    }
    if (JointDeltas.size() == 0)
    {
        JointDeltas = Eigen::Matrix3Xf::Zero(3, m->rigGeometry->NumJoints());
    }

    auto BlendRegion = [&](int idx)
    {
        if ((idx < 0) || (idx >= (int)m->regionNames.size()))
        {
            return;
        }
        // Anatomical joint-into-shape merges. The DNA exposes joint-only PCA
        // regions (`local_joint_upperarm_l`, `local_joint_lowerarm_l`, ...)
        // that aren't reachable from the shape-keyed iteration on their own
        // — `regionNames` keeps them as separate `joint_*` entries so engine
        // consumers don't need to change. For blending, we route each joint
        // region into the anatomically-adjacent shape region so a shape-side
        // iteration picks up both PCAs in one pass. Keep this table tight;
        // anything not listed here keeps the existing 1:1 behavior.
        static const std::map<std::string, std::string> jointMergedIntoShape = {
            { "bicep_l",   "upperarm_l" },
            { "bicep_r",   "upperarm_r" },
            { "forearm_l", "lowerarm_l" },
            { "forearm_r", "lowerarm_r" },
        };
        // Joint-region names to drive `skeletonPcaControls["joint_*"]` and
        // `regionToJoints[*]` for this iteration: the literal region name
        // plus any anatomical alias from the table above.
        std::vector<std::string> jointRegions;
        jointRegions.push_back(m->regionNames[idx]);
        if (auto aliasIt = jointMergedIntoShape.find(m->regionNames[idx]); aliasIt != jointMergedIntoShape.end())
        {
            jointRegions.push_back(aliasIt->second);
        }

        for (const auto& [alpha, otherState] : States)
        {
            if ((Type == BodyAttribute::Skeleton) || (Type == BodyAttribute::Both))
            {
                std::set<int> visitedJoints;
                for (const std::string& jointRegion : jointRegions)
                {
                    auto it = m->skeletonPcaControls.find("joint_" + jointRegion);
                    if (it != m->skeletonPcaControls.end())
                    {
                        rawControls(it->second) += alpha * (otherState->m->RawControls(it->second) - rawControls(it->second));
                    }
                    if ((otherState->m->JointDeltas.size() > 0) || (state.m->JointDeltas.size() > 0))
                    {
                        for (int ji : m->regionToJoints[jointRegion])
                        {
                            if (!visitedJoints.insert(ji).second) continue;  // dedupe across aliased regions
                            if ((ji < otherState->m->JointDeltas.cols()) && (ji < state.m->JointDeltas.cols()))
                            {
                                // alpha * (other_scaled - state) — was `alpha*X - Y`
                                // due to operator precedence which made t=0 collapse
                                // JointDeltas to zero instead of leaving it at A.
                                JointDeltas.col(ji) += alpha * ((otherState->m->VertexDeltaScale * otherState->m->JointDeltas.col(ji)) - state.m->JointDeltas.col(ji));
                            }
                            else if (ji < otherState->m->JointDeltas.cols())
                            {
                                JointDeltas.col(ji) += alpha * (otherState->m->VertexDeltaScale * otherState->m->JointDeltas.col(ji));
                            }
                            else
                            {
                                JointDeltas.col(ji) -= alpha * state.m->JointDeltas.col(ji);
                            }
                        }
                    }
                }
            }
            if ((Type == BodyAttribute::Shape) || (Type == BodyAttribute::Both))
            {
                auto it = m->shapePcaControls.find(m->regionNames[idx]);
                if (it != m->shapePcaControls.end())
                {
                    rawControls(it->second) += alpha * (otherState->m->RawControls(it->second) - rawControls(it->second));
                }

                auto it2 = m->partWeights.find(m->regionNames[idx]);
                if (it2 != m->partWeights.end())
                {
                    for (const auto& [vID, weight] : it2->second.NonzeroVerticesAndWeights())
                    {
                        if ((otherState->m->VertexDeltas.size() > 0) || (state.m->VertexDeltas.size() > 0))
                        {
                            if ((vID < otherState->m->VertexDeltas.cols()) && (vID < state.m->VertexDeltas.cols()))
                                VertexDeltas.col(vID) += weight * alpha * ((otherState->m->VertexDeltaScale * otherState->m->VertexDeltas.col(vID)) - state.m->VertexDeltas.col(vID));
                            else if (vID < otherState->m->VertexDeltas.cols())
                                VertexDeltas.col(vID) += (weight * alpha * otherState->m->VertexDeltaScale) * (otherState->m->VertexDeltas.col(vID));
                            else if (vID < state.m->VertexDeltas.cols())
                                VertexDeltas.col(vID) -= weight * alpha * (state.m->VertexDeltas.col(vID));
                        }
                        if ((otherState->m->BodySeamDelta.size() > 0) || (state.m->BodySeamDelta.size() > 0))
                        {
                            if ((vID < otherState->m->BodySeamDelta.cols()) && (vID < state.m->BodySeamDelta.cols()))
                                BodySeamDelta.col(vID) += weight * alpha * (otherState->m->BodySeamDelta.col(vID) - state.m->BodySeamDelta.col(vID));
                            else if (vID < otherState->m->BodySeamDelta.cols())
                                BodySeamDelta.col(vID) += weight * alpha * otherState->m->BodySeamDelta.col(vID);
                            else if (vID < state.m->BodySeamDelta.cols())
                                BodySeamDelta.col(vID) -= weight * alpha * state.m->BodySeamDelta.col(vID);
                        }
                    }
                }
            }
        }
    };

    if ((RegionIndex < 0) || (RegionIndex >= numRegions))
    {
        for (int idx = 0; idx < numRegions; ++idx)
        {
            BlendRegion(idx);
        }
        // Global blend only — uniform scale is a character-wide property
        // (applied via ScaleVertices in EvaluateState), so it belongs with
        // Skeleton/Both blending and only when we're morphing the whole
        // character. Per-region blends leave it untouched.
        if ((Type == BodyAttribute::Skeleton) || (Type == BodyAttribute::Both))
        {
            for (const auto& [alpha, otherState] : States)
            {
                ScaleFactor += alpha * (otherState->m->ScaleFactor - ScaleFactor);
            }
        }
    }
    else
    {
        BlendRegion(RegionIndex);
        if (state.m->UseSymmetry)
        {
            auto it = m->symmetricPartMapping.find(m->regionNames[RegionIndex]);
            if (it != m->symmetricPartMapping.end())
            {
                const int SymmetricRegionIndex = GetItemIndex(m->regionNames, it->second);
                if ((SymmetricRegionIndex != RegionIndex) && (SymmetricRegionIndex >= 0))
                {
                    BlendRegion(SymmetricRegionIndex);
                }
            }
        }
    }

    state.m->RawControls = rawControls;
    state.m->ScaleFactor = ScaleFactor;

    // JointDeltas had per-region accumulation above but historically was
    // never assigned back — joint contributions to the blend went to /dev/null.
    if (JointDeltas.squaredNorm() > 0)
    {
        state.m->JointDeltas = JointDeltas;
    }
    else
    {
        state.m->JointDeltas.setZero();
    }

    // Flat-lerp faceState across all participating states. SerializeToVector
    // packs (regionScales, regionRotations, regionTranslations, regionPcaWeights)
    // into one vector; lerping the vector covers every patch's PCA / scale /
    // translation in one shot. Quaternion components lerp (not slerp) — fine
    // for small inter-state rotations, exact at the endpoints. Only runs in
    // the global-region path so per-region blends don't disturb the face PCA.
    const bool isGlobalBlend = (RegionIndex < 0) || (RegionIndex >= numRegions);
    if (isGlobalBlend && state.m->faceState)
    {
        Eigen::VectorXf faceVec = state.m->faceState->SerializeToVector();
        for (const auto& [alpha, otherState] : States)
        {
            if (!otherState->m->faceState) continue;
            const Eigen::VectorXf otherVec = otherState->m->faceState->SerializeToVector();
            if (otherVec.size() != faceVec.size()) continue;  // shape mismatch — skip
            faceVec += alpha * (otherVec - faceVec);
        }
        state.m->faceState->DeserializeFromVector(faceVec);
    }

    if (VertexDeltas.squaredNorm() > 0)
    {
        state.m->VertexDeltas = VertexDeltas;
    }
    else
    {
        state.m->VertexDeltas.resize(3, 0);
    }
    if (BodySeamDelta.squaredNorm() > 0)
    {
        state.m->BodySeamDelta = BodySeamDelta;
    }
    else
    {
        state.m->BodySeamDelta.resize(3, 0);
    }

    UpdateGuiFromRawControls(state);
    EvaluateState(state);
    state.m->GuiControlsPrior = state.m->GuiControls;
    state.m->TargetMeasurements.clear();

    return true;
}

bool BodyShapeEditor::GetMeasurements(Eigen::Ref<const Eigen::Matrix<float, 3, -1>> combinedBodyAndFaceVertices, Eigen::VectorXf& measurements,
    std::vector<std::string>& measurementNames) const
{
    if ((int)combinedBodyAndFaceVertices.cols() != m->rigGeometry->GetMesh(0).NumVertices())
    {
        CARBON_CRITICAL("vertices have incorrect size for combined body and face: {}, but expected {}",
            combinedBodyAndFaceVertices.cols(),
            m->rigGeometry->GetMesh(0).NumVertices());
    }

    std::vector<BodyMeasurement> Constraints;
    measurementNames.clear();
    for (int i = 0; i < (int)m->Constraints.size(); ++i)
    {
        if (m->Constraints[i].GetType() != BodyMeasurement::type_t::Semantic)
        {
            Constraints.push_back(m->Constraints[i]);
            measurementNames.push_back(m->Constraints[i].GetName());
        }
    }
    Eigen::Matrix<float, 3, -1> vertexNormals;
    m->triTopology->CalculateVertexNormals(combinedBodyAndFaceVertices,
        vertexNormals,
        VertexNormalComputationType::AreaWeighted,
        /*stableNormalize*/ true,
        m->threadPool.get());
    BodyMeasurement::UpdateBodyMeasurementPoints(Constraints, combinedBodyAndFaceVertices, vertexNormals, *m->heTopology, m->threadPool.get());
    measurements = BodyMeasurement::GetBodyMeasurements(Constraints, combinedBodyAndFaceVertices, Eigen::VectorXf());

    return true;
}

bool BodyShapeEditor::GetMeasurements(Eigen::Ref<const Eigen::Matrix<float, 3, -1>> faceVertices,
    Eigen::Ref<const Eigen::Matrix<float, 3, -1>> bodyVertices,
    Eigen::VectorXf& measurements,
    std::vector<std::string>& measurementNames) const
{
    if (m->bodyToCombinedMapping.empty())
    {
        CARBON_CRITICAL("body to combined mapping is missing");
    }
    if ((int)bodyVertices.cols() != (int)m->bodyToCombinedMapping.front().size())
    {
        CARBON_CRITICAL("incorrect body vertices size: {}, but expected {}", bodyVertices.cols(), m->bodyToCombinedMapping.front().size());
    }
    Eigen::Matrix<float, 3, -1> combinedBodyAndFaceVertices = m->rigGeometry->GetMesh(0).Vertices();
    if (faceVertices.cols() > combinedBodyAndFaceVertices.cols())
    {
        CARBON_CRITICAL("invalid number of face vertices: {}", faceVertices.cols());
    }
    combinedBodyAndFaceVertices.leftCols(faceVertices.cols()) = faceVertices;
    combinedBodyAndFaceVertices(Eigen::all, m->bodyToCombinedMapping.front()) = bodyVertices;
    return GetMeasurements(combinedBodyAndFaceVertices, measurements, measurementNames);
}

SnapConfig<float> BodyShapeEditor::Private::CalcNeckSeamSkinningWeightsSnapConfig(int lod) const
{
    SnapConfig<float> curSnapConfig;
    curSnapConfig.sourceVertexIndices = neckSeamIndices[0];

    const Eigen::Matrix<float, 3, -1>& curLodMeshVertices = combinedBodyArchetypeRigGeometry->GetMesh(lod).Vertices();
    const Eigen::Matrix<float, 3, -1>& lod0MeshVertices = combinedBodyArchetypeRigGeometry->GetMesh(0).Vertices();

    // find closest vertex in lod N for each sourceVertexIndex, and ensure the distance is close to zero
    for (size_t sInd = 0; sInd < curSnapConfig.sourceVertexIndices.size(); ++sInd)
    {
        Eigen::Matrix<float, 3, 1> curSourceVert = lod0MeshVertices.col(curSnapConfig.sourceVertexIndices[sInd]);
        float closestDist2 = std::numeric_limits<float>::max();
        int closestVInd = 0;
        for (int tInd = 0; tInd < curLodMeshVertices.cols(); ++tInd)
        {
            float curDist2 = (curSourceVert - curLodMeshVertices.col(tInd)).squaredNorm();
            if (curDist2 < closestDist2)
            {
                closestDist2 = curDist2;
                closestVInd = tInd;
            }
        }

        curSnapConfig.targetVertexIndices.emplace_back(closestVInd);
    }

    return curSnapConfig;
}

void BodyShapeEditor::GetVertexInfluenceWeights(const State& state, std::vector<SparseMatrix<float>>& vertexInfluenceWeights) const
{
    if (m->skinWeightsPCA.mean.size() > 0u)
    {
        vertexInfluenceWeights.resize(size_t(NumLODs()));
        vertexInfluenceWeights.resize(NumLODs());
        if (state.m->CustomSkinning.size() > 0)
        {
            vertexInfluenceWeights[0] = state.m->CustomSkinning; 
        }
        else 
        {
            vertexInfluenceWeights[0] =  m->skinWeightsPCA.calculateResult(state.m->GuiControls(m->globalIndices));
        }
        skinningweightutils::SortPruneAndRenormalizeSkinningWeights(vertexInfluenceWeights[0], GetMaxSkinWeights()[0]);

        // now propagate the skinning weights to higher lods
        std::map<std::string, std::vector<BarycentricCoordinates<float>>> lod0MeshClosestPointBarycentricCoordinates;
        if (m->combinedLodGenerationData)
        {
            m->combinedLodGenerationData->GetDriverMeshClosestPointBarycentricCoordinates(lod0MeshClosestPointBarycentricCoordinates);
        }

        for (int lod = 1; lod < NumLODs(); ++lod)
        {
            std::vector<BarycentricCoordinates<float>> curLodBarycentricCoordinates = lod0MeshClosestPointBarycentricCoordinates.at(
                m->combinedLodGenerationData->HigherLodMeshNames()[lod - 1]);
            skinningweightutils::PropagateSkinningWeightsToHigherLOD<float>(curLodBarycentricCoordinates,
                m->combinedBodyArchetypeRigGeometry->GetMesh(0).Vertices(),
                vertexInfluenceWeights[0],
                m->jointSkinningWeightLodPropagationMap[lod - 1],
                m->skinningWeightSnapConfigs[size_t(lod - 1)],
                *m->combinedBodyArchetypeRigGeometry,
                GetMaxSkinWeights()[size_t(lod)],
                vertexInfluenceWeights[size_t(lod)]);
        }
    }
}

int BodyShapeEditor::GetJointIndex(const std::string& JointName) const
{
    return m->combinedBodyArchetypeRigGeometry->GetJointIndex(JointName);
}

namespace
{
    // Apply an Affine3f to every column of a 3xN matrix.
    Eigen::Matrix3Xf TransformColumns(const Eigen::Transform<float, 3, Eigen::Affine>& M,
                                      const Eigen::Ref<const Eigen::Matrix3Xf>& v)
    {
        Eigen::Matrix3Xf out(3, v.cols());
        const Eigen::Matrix3f L = M.linear();
        const Eigen::Vector3f t = M.translation();
        for (Eigen::Index i = 0; i < v.cols(); ++i)
            out.col(i) = L * v.col(i) + t;
        return out;
    }
}

BodyShapeEditor::PoseFrame
BodyShapeEditor::ComposePoseFrame(const State& state) const
{
    // EvaluatePose=true: matrices are already cached on the state.
    // EvaluatePose=false: state.WorldMatrices == JointBindMatrices, so any
    // bind*world^-1 transform collapses to identity. Re-run the nonlinear
    // path on a throwaway clone to recover both bind and world from the
    // same evaluation
    if (state.m->EvaluatePose)
    {
        return { state.m->JointBindMatrices, state.m->WorldMatrices };
    }
    State copy(state);
    copy.m->EvaluatePose = true;
    EvaluateState(copy, CalcStrategy::Nonlinear, /*isEvaluatingPose=*/true);
    return { copy.m->JointBindMatrices, copy.m->WorldMatrices };
}

Eigen::Matrix3Xf BodyShapeEditor::RigidAttachmentToBind(
    const State& state,
    const std::string& jointName,
    const Eigen::Ref<const Eigen::Matrix3Xf>& posedVertices) const
{
    const int j = GetJointIndex(jointName);
    if (j < 0) return {};
    const auto frame = ComposePoseFrame(state);
    if (j >= (int)frame.bind.size() || j >= (int)frame.world.size()) return {};

    // pose -> bind = M_bind * M_world^-1
    const Eigen::Transform<float, 3, Eigen::Affine> M =
        frame.bind[(size_t)j] * frame.world[(size_t)j].inverse();
    Eigen::Matrix3Xf result = TransformColumns(M, posedVertices);

    // ComposePoseFrame strips floor offset from the matrices it returns
    // (its internal clone forces FloorOffsetApplied=false). When the
    // caller's state had floor offset applied, the result lands in
    // unfloor'd bind space — we need to subtract Δ_state to put it in
    // state's actual floored bind frame.
    //
    // No stored Δ: compute it on the fly from a scratch eval. Cloning
    // state with FloorOffsetApplied=false and re-evaluating gives the
    // unfloor'd mesh; the Y of the floor-index vertex is exactly the Δ
    // that state.GetMesh(0) had subtracted in its last EvaluateState.
    // The scratch eval uses Auto strategy so it follows state's current
    // EvaluatePose mode — matching the Δ state itself produces. No cache,
    // no staleness, always reflects state's current configuration.
    if (state.m->FloorOffsetApplied)
    {
        State scratch(state);
        scratch.m->FloorOffsetApplied = false;
        EvaluateState(scratch);
        const float deltaY = (m->floorIndex >= 0)
            ? scratch.GetMesh(0).Vertices().row(1)[m->floorIndex]
            : scratch.GetMesh(0).Vertices().row(1).minCoeff();
        result.row(1).array() -= deltaY;
    }
    return result;
}

Eigen::Matrix3Xf BodyShapeEditor::BindToRigidAttachment(
    const State& state,
    const std::string& jointName,
    const Eigen::Ref<const Eigen::Matrix3Xf>& bindVertices) const
{
    const int j = GetJointIndex(jointName);
    if (j < 0) return {};
    const auto frame = ComposePoseFrame(state);
    if (j >= (int)frame.bind.size() || j >= (int)frame.world.size()) return {};

    // bind -> pose = M_world * M_bind^-1
    const Eigen::Transform<float, 3, Eigen::Affine> M =
        frame.world[(size_t)j] * frame.bind[(size_t)j].inverse();
    return TransformColumns(M, bindVertices);
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)

