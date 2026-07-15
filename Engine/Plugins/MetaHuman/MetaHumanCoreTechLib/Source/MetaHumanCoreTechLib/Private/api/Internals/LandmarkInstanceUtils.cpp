// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandmarkInstanceUtils.h"
#include <nrr/landmarks/LandmarkConfiguration.h>
#include <carbon/Common.h>
#include <pma/PolyAllocator.h>

using namespace TITAN_NAMESPACE;

namespace TITAN_API_NAMESPACE
{
std::shared_ptr<const LandmarkInstance<float, 2>> CreateLandmarkInstanceForCamera(const std::map<std::string, FaceTrackingLandmarkData>& perCameraLandmarkData,
                                                                                  const std::map<std::string, std::vector<std::string>>& curvesToMerge,
                                                                                  const MetaShapeCamera<float>& camera)
{
    std::shared_ptr<LandmarkConfiguration> landmarkConfiguration = std::allocate_shared<LandmarkConfiguration>(pma::PolyAllocator<LandmarkConfiguration>(
                                                                                                                   MEM_RESOURCE));
    for (const auto& [landmarkOrCurveName, faceTrackingLandmarkData] : perCameraLandmarkData)
    {
        if (faceTrackingLandmarkData.NumPoints() == 1)
        {
            landmarkConfiguration->AddLandmark(landmarkOrCurveName);
        }
        else if (faceTrackingLandmarkData.NumPoints() > 1)
        {
            landmarkConfiguration->AddCurve(landmarkOrCurveName, faceTrackingLandmarkData.NumPoints());
        }
        else
        {
            CARBON_CRITICAL("at least one point per landmark/curve required");
        }
    }

    Eigen::Matrix<float, 2, -1> landmarks(2, landmarkConfiguration->NumPoints());
    Eigen::Vector<float, -1> confidence(landmarkConfiguration->NumPoints());
    for (const auto& [landmarkOrCurveName, faceTrackingLandmarkData] : perCameraLandmarkData)
    {
        if (faceTrackingLandmarkData.PointsDimension() != 2)
        {
            CARBON_CRITICAL("input landmark data is not in 2D.");
        }
        if (faceTrackingLandmarkData.NumPoints() == 1)
        {
            const int index = landmarkConfiguration->IndexForLandmark(landmarkOrCurveName);
            for (int d = 0; d < 2; ++d)
            {
                landmarks(d, index) = faceTrackingLandmarkData.PointsData()[d];
            }
            confidence[index] = faceTrackingLandmarkData.ConfidenceData()[0];
        }
        else
        {
            const std::vector<int>& indices = landmarkConfiguration->IndicesForCurve(landmarkOrCurveName);
            for (int32_t i = 0; i < faceTrackingLandmarkData.NumPoints(); ++i)
            {
                const int index = indices[i];
                for (int d = 0; d < 2; ++d)
                {
                    landmarks(d, index) = faceTrackingLandmarkData.PointsData()[2 * i + d];
                }
                confidence[index] = faceTrackingLandmarkData.ConfidenceData()[i];
            }
        }
    }
    for (const auto& [mergedCurve, listOfCurves] : curvesToMerge)
    {
        landmarkConfiguration->MergeCurves(listOfCurves, mergedCurve, landmarks, /*ignoreMissingCurves=*/true);
    }

    std::shared_ptr<LandmarkInstance<float, 2>> landmarkInstance = std::allocate_shared<LandmarkInstance<float, 2>>(pma::PolyAllocator<LandmarkInstance<float, 2>>(
                                                                                                                        MEM_RESOURCE),
                                                                                                                    landmarks,
                                                                                                                    confidence);
    landmarkInstance->SetLandmarkConfiguration(landmarkConfiguration);
    for (int i = 0; i < landmarkInstance->NumLandmarks(); ++i)
    {
        const Eigen::Vector2f pix = camera.Undistort(landmarkInstance->Points().col(i));
        landmarkInstance->SetLandmark(i,
                                      pix,
                                      landmarkInstance->Confidence()[i]);
    }
    return landmarkInstance;
}

std::shared_ptr<const LandmarkInstance<float, 3>> Create3dLandmarkInstance(const std::map<std::string, const FaceTrackingLandmarkData>& landmarkData,
                                                                           const std::map<std::string, std::vector<std::string>>& curvesToMerge)
{
    if (landmarkData.empty())
    {
        return std::shared_ptr<const LandmarkInstance<float, 3>>{};
    }

    std::shared_ptr<LandmarkConfiguration> landmarkConfiguration = std::allocate_shared<LandmarkConfiguration>(pma::PolyAllocator<LandmarkConfiguration>(
                                                                                                                   MEM_RESOURCE));
    for (const auto& [landmarkOrCurveName, faceTrackingLandmarkData] : landmarkData)
    {
        if (faceTrackingLandmarkData.NumPoints() == 1)
        {
            landmarkConfiguration->AddLandmark(landmarkOrCurveName);
        }
        else if (faceTrackingLandmarkData.NumPoints() > 1)
        {
            landmarkConfiguration->AddCurve(landmarkOrCurveName, faceTrackingLandmarkData.NumPoints());
        }
        else
        {
            CARBON_CRITICAL("at least one point per landmark/curve required");
        }
    }

    Eigen::Matrix<float, 3, -1> landmarks(3, landmarkConfiguration->NumPoints());
    Eigen::Vector<float, -1> confidence(landmarkConfiguration->NumPoints());
    for (const auto& [landmarkOrCurveName, faceTrackingLandmarkData] : landmarkData)
    {
        if (faceTrackingLandmarkData.PointsDimension() != 3)
        {
            CARBON_CRITICAL("input landmark data is not in 3D.");
        }
        if (faceTrackingLandmarkData.NumPoints() == 1)
        {
            const int index = landmarkConfiguration->IndexForLandmark(landmarkOrCurveName);
            landmarks(0, index) = faceTrackingLandmarkData.PointsData()[0];
            landmarks(1, index) = faceTrackingLandmarkData.PointsData()[1];
            landmarks(2, index) = faceTrackingLandmarkData.PointsData()[2];
            confidence[index] = faceTrackingLandmarkData.ConfidenceData()[0];
        }
        else
        {
            const std::vector<int>& indices = landmarkConfiguration->IndicesForCurve(landmarkOrCurveName);
            for (int32_t i = 0; i < faceTrackingLandmarkData.NumPoints(); ++i)
            {
                const int index = indices[i];
                landmarks(0, index) = faceTrackingLandmarkData.PointsData()[3 * i + 0];
                landmarks(1, index) = faceTrackingLandmarkData.PointsData()[3 * i + 1];
                landmarks(2, index) = faceTrackingLandmarkData.PointsData()[3 * i + 2];
                confidence[index] = faceTrackingLandmarkData.ConfidenceData()[i];
            }
        }
    }
    for (const auto& [mergedCurve, listOfCurves] : curvesToMerge)
    {
        landmarkConfiguration->MergeCurves(listOfCurves, mergedCurve, landmarks, /*ignoreMissingCurves=*/true);
    }

    std::shared_ptr<LandmarkInstance<float, 3>> landmarkInstance = std::allocate_shared<LandmarkInstance<float, 3>>(pma::PolyAllocator<LandmarkInstance<float, 3>>(
                                                                                                                        MEM_RESOURCE),
                                                                                                                    landmarks,
                                                                                                                    confidence);

    landmarkInstance->SetLandmarkConfiguration(landmarkConfiguration);

    return landmarkInstance;
}
} // namespace TITAN_API_NAMESPACE
