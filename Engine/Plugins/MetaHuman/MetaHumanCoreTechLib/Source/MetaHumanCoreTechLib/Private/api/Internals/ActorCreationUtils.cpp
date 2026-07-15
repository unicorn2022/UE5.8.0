// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorCreationUtils.h"

#include <carbon/Common.h>
#include <nls/geometry/MetaShapeCamera.h>
#include <pma/PolyAllocator.h>
#include <nls/geometry/Procrustes.h>

using namespace TITAN_NAMESPACE;

namespace TITAN_API_NAMESPACE
{

MultiCameraSetup<float> ScaledCameras(const MultiCameraSetup<float>& cameras, float scale)
{
    MultiCameraSetup<float> outputCameras;
    std::vector<MetaShapeCamera<float>> currentCameras = cameras.GetCamerasAsVector();

    for (auto& cam : currentCameras)
    {
        // scale from meter to cm
        Affine<float, 3, 3> extrinsics = cam.Extrinsics();
        extrinsics.SetTranslation(extrinsics.Translation() * scale);
        cam.SetExtrinsics(extrinsics);
    }
    outputCameras.Init(currentCameras);

    return outputCameras;
}

std::vector<MultiCameraSetup<float>> ScaledCamerasPerFrame(const MultiCameraSetup<float>& cameras, const std::vector<float>& scales)
{
    std::vector<MultiCameraSetup<float>> camerasPerFrame(scales.size());

    for (size_t i = 0; i < scales.size(); ++i)
    {
        camerasPerFrame[i] = ScaledCameras(cameras, scales[i]);
    }

    return camerasPerFrame;
}

std::pair<std::vector<Eigen::VectorX<float>>, std::vector<std::shared_ptr<const Mesh<float>>>> CollectMeshes(
    const std::vector<std::shared_ptr<FrameInputData>>& frameData,
    std::vector<float> scale)
{
    std::vector<Eigen::VectorX<float>> weights;
    std::vector<std::shared_ptr<const Mesh<float>>> meshes;

    for (size_t frameNum = 0; frameNum < frameData.size(); ++frameNum)
    {
        if (scale.size() > 0)
        {
            std::shared_ptr<Mesh<float>> currentData = std::make_shared<Mesh<float>>(*frameData[frameNum]->Scan().mesh);
            currentData->SetVertices(scale[frameNum] * currentData->Vertices());
            currentData->CalculateVertexNormals();
            meshes.emplace_back(currentData);
        }
        else
        {
            meshes.emplace_back(frameData[frameNum]->Scan().mesh);
        }
        weights.emplace_back(frameData[frameNum]->Scan().weights);
    }

    return std::pair<std::vector<Eigen::VectorX<float>>, std::vector<std::shared_ptr<const Mesh<float>>>>(weights,
                                                                                                          meshes);
}

std::pair<std::vector<Eigen::VectorX<float>>, std::vector<std::shared_ptr<const Mesh<float>>>> CollectDepthmapsAsMeshes(
    const std::vector<std::shared_ptr<FrameInputData>>& frameData)
{
    std::vector<Eigen::VectorX<float>> weights;
    std::vector<std::shared_ptr<const Mesh<float>>> meshes;
    for (auto& frame : frameData)
    {
        for (const auto& [cameraName, depthAsMeshData] : frame->DepthmapsAsMeshes())
        {
            meshes.emplace_back(depthAsMeshData.mesh);
            weights.emplace_back(depthAsMeshData.weights);
        }
    }

    return std::pair<std::vector<Eigen::VectorX<float>>, std::vector<std::shared_ptr<const Mesh<float>>>>(weights,
                                                                                                          meshes);
}

std::pair<LandmarkInstance<float, 2>, Camera<float>> Extract2DLandmarksForCamera(const std::shared_ptr<FrameInputData>& frameData,
                                                                                 const MultiCameraSetup<float>& cameras,
                                                                                 const std::string& cameraName)
{
    auto it = frameData->LandmarksPerCamera().find(cameraName);
    if (it == frameData->LandmarksPerCamera().end())
    {
        CARBON_CRITICAL("No camera {} in frame data", cameraName);
    }

    return std::pair<LandmarkInstance<float, 2>, Camera<float>>(*it->second, cameras.GetCamera(cameraName));
}

std::vector<std::vector<std::pair<LandmarkInstance<float, 2>, Camera<float>>>> Collect2DLandmarks(const std::vector<std::shared_ptr<FrameInputData>>& frameData,
                                                                                                  const MultiCameraSetup<float>& cameras)
{
    std::vector<std::vector<std::pair<LandmarkInstance<float, 2>, Camera<float>>>> output;

    for (auto& frame : frameData)
    {
        std::vector<std::pair<LandmarkInstance<float, 2>, Camera<float>>> landmarks;
        for (const auto& [cameraName, landmarkInstance] : frame->LandmarksPerCamera())
        {
            landmarks.emplace_back(std::pair<LandmarkInstance<float, 2>, Camera<float>>(*landmarkInstance,
                                                                                        cameras.GetCamera(cameraName)));
        }
        output.emplace_back(landmarks);
    }

    return output;
}

std::vector<std::vector<std::pair<LandmarkInstance<float, 2>, Camera<float>>>> Collect2DLandmarks(const std::vector<std::shared_ptr<FrameInputData>>& frameData,
                                                                                                  const std::vector<MultiCameraSetup<float>>& camerasPerFrame)
{
    CARBON_ASSERT(frameData.size() == camerasPerFrame.size(), "inputs size misalignment");
    std::vector<std::vector<std::pair<LandmarkInstance<float, 2>, Camera<float>>>> output;

    for (int frameNum = 0; frameNum < int(frameData.size()); ++frameNum)
    {
        MultiCameraSetup<float> cameras = camerasPerFrame[frameNum];

        std::vector<std::pair<LandmarkInstance<float, 2>, Camera<float>>> landmarks;
        for (const auto& [cameraName, landmarkInstance] : frameData[frameNum]->LandmarksPerCamera())
        {
            landmarks.emplace_back(std::pair<LandmarkInstance<float, 2>, Camera<float>>(*landmarkInstance,
                                                                                        cameras.GetCamera(cameraName)));
        }
        output.emplace_back(landmarks);
    }

    return output;
}

std::vector<LandmarkInstance<float, 3>> Collect3DLandmarks(const std::vector<std::shared_ptr<FrameInputData>>& frameData)
{
    std::vector<LandmarkInstance<float, 3>> output;
    for (auto& frame : frameData)
    {
        auto landmarks = frame->Landmarks3D();
        if (!landmarks)
        {
            continue;
        }
        output.emplace_back(*landmarks);
    }

    return output;
}

} // namespace TITAN_API_NAMESPACE
