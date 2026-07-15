// Copyright Epic Games, Inc. All Rights Reserved.

#include "FBBundleAdjustment.h"
#include "Common.h"
#include "Internals/OpenCVCamera2MetaShapeCamera.h"

#include <nls/geometry/MetaShapeCamera.h>
#include <calib/Defs.h>
#include <calib/Calibration.h>
#include <calib/Utilities.h>
#include <robustfeaturematcher/UtilsCamera.h>

using namespace TITAN_NAMESPACE;

namespace TITAN_API_NAMESPACE
{

static Eigen::MatrixX<double> PointsVectorToEigen(const std::vector<double>& InPoints)
{
    Eigen::MatrixX<double> OutPoints;

    size_t PointsSize = InPoints.size() / 2;
    OutPoints.resize(PointsSize, 2);

    for (size_t Index = 0; Index < PointsSize; ++Index)
    {
        OutPoints(Index, 0) = InPoints[(Index * 2)];
        OutPoints(Index, 1) = InPoints[(Index * 2) + 1];
    }

    return OutPoints;
}

static std::vector<Eigen::Vector2<double>> PointsVectorToEigenVector(const std::vector<double>& InPoints)
{
    std::vector<Eigen::Vector2<double>> OutPoints;

    size_t PointsSize = InPoints.size() / 2;
    OutPoints.resize(PointsSize);

    for (size_t Index = 0; Index < PointsSize; ++Index)
    {
        OutPoints[Index](0) = InPoints[(Index * 2)];
        OutPoints[Index](1) = InPoints[(Index * 2) + 1];
    }

    return OutPoints;
}

static Eigen::MatrixX<double> Points3dVectorToEigen(const std::vector<double>& InPoints)
{
    Eigen::MatrixX<double> OutPoints;

    size_t PointsSize = InPoints.size() / 3;
    OutPoints.resize(PointsSize, 3);

    for (size_t Index = 0; Index < PointsSize; ++Index)
    {
        OutPoints(Index, 0) = InPoints[(Index * 3)];
        OutPoints(Index, 1) = InPoints[(Index * 3) + 1];
        OutPoints(Index, 2) = InPoints[(Index * 3) + 2];
    }

    return OutPoints;
}

static void PrepackBundleAdjustmentParameters(const std::map<size_t, std::vector<double>>& InPoints3d,
    const std::map<size_t, std::vector<std::vector<double>>>& InCameraPoints,
    const std::map<size_t, std::vector<std::vector<bool>>>& InVisibility,
    const FBBundleAdjustmentParameters& InParams,
    std::vector<Eigen::MatrixX<double>>& OutPoints3d,
    std::vector<std::vector<Eigen::MatrixX<double>>>& OutImagePoints,
    std::vector<std::vector<std::vector<bool>>>& OutVisibility,
    calib::FBBAParams& OutParams)
{
    OutParams.iterations = InParams.Iterations;
    OutParams.frameNum = InParams.NumberOfFrames;
    OutParams.fixedIntrinsicIndices = InParams.FixedIntrinsicIndices;
    OutParams.fixedDistortionIndices = InParams.FixedDistortionIndices;
    OutParams.fixedScale = InParams.bFixedScale && !InParams.ReferencePoints.empty();
    OutParams.referenceDistance = InParams.ReferenceDistance;

    std::transform(InParams.ReferencePoints.begin(),
        InParams.ReferencePoints.end(),
        std::back_inserter(OutParams.referencePoints),
        [](const std::vector<std::vector<double>>& InPerFrameReferencePoints)
        {
            std::vector<std::vector<Eigen::Vector2<double>>> CameraReferencePoints;

            for (const std::vector<double>& VectorCameraPoints : InPerFrameReferencePoints)
            {
                CameraReferencePoints.push_back(PointsVectorToEigenVector(VectorCameraPoints));
            }

            return CameraReferencePoints;
        });

    std::transform(InPoints3d.begin(),
        InPoints3d.end(),
        std::back_inserter(OutPoints3d),
        [](const std::pair<size_t, std::vector<double>>& InPoint3dPair)
        {
            return Points3dVectorToEigen(InPoint3dPair.second);
        });

    std::transform(InCameraPoints.begin(),
        InCameraPoints.end(),
        std::back_inserter(OutImagePoints),
        [](const std::pair<size_t, std::vector<std::vector<double>>>& InPerCameraPointsPair)
        {
            std::vector<Eigen::MatrixX<double>> CameraPoints;

            for (const std::vector<double>& VectorCameraPoints : InPerCameraPointsPair.second)
            {
                CameraPoints.push_back(PointsVectorToEigen(VectorCameraPoints));
            }

            return CameraPoints;
        });

    std::transform(InVisibility.begin(),
        InVisibility.end(),
        std::back_inserter(OutVisibility), [](const std::pair<size_t, std::vector<std::vector<bool>>>& InPerCameraVisibilityPair)
        {
            std::vector<std::vector<bool>> PerCameraVisibility;

            for (const std::vector<bool>& VectorCameraVisibility : InPerCameraVisibilityPair.second)
            {
                PerCameraVisibility.push_back(VectorCameraVisibility);
            }

            return PerCameraVisibility; 
        });
}

struct FeatureBasedBundleAdjustment::Private
{
    std::vector<MetaShapeCamera<double>> MetaShapeCalibrationInformation;

    void UnpackCameras(std::vector<Eigen::Matrix3<double>>& OutIntrisicsVector,
                       std::vector<Eigen::VectorX<double>>& OutDistortionVector,
                       std::vector<Eigen::Matrix4<double>>& OutExtrinsicsVector);

    void PackCameras(const std::vector<Eigen::Matrix3<double>>& InIntrisicsVector,
                     const std::vector<Eigen::VectorX<double>>& InDistortionVector,
                     const std::vector<Eigen::Matrix4<double>>& InExtrinsicsVector);
};

void FeatureBasedBundleAdjustment::Private::UnpackCameras(std::vector<Eigen::Matrix3<double>>& OutIntrisicsVector,
                                                          std::vector<Eigen::VectorX<double>>& OutDistortionVector,
                                                          std::vector<Eigen::Matrix4<double>>& OutExtrinsicsVector)
{
    for (size_t Index = 0; Index < MetaShapeCalibrationInformation.size(); ++Index)
    {
        Eigen::Matrix3<double> Intrisics;
        Eigen::VectorX<double> Distortion;
        Eigen::Matrix4<double> Extrinsics;

        const MetaShapeCamera<double>& Camera = MetaShapeCalibrationInformation[Index];

        calib::rfm::adjustNlsCamera(Camera, Intrisics, Distortion);
        Extrinsics = Camera.Extrinsics().Matrix();

        OutIntrisicsVector.push_back(std::move(Intrisics));
        OutDistortionVector.push_back(std::move(Distortion));
        OutExtrinsicsVector.push_back(std::move(Extrinsics));
    }
}

void FeatureBasedBundleAdjustment::Private::PackCameras(const std::vector<Eigen::Matrix3<double>>& InIntrisicsVector,
                                                        const std::vector<Eigen::VectorX<double>>& InDistortionVector,
                                                        const std::vector<Eigen::Matrix4<double>>& InExtrinsicsVector)
{
    for (size_t Index = 0; Index < MetaShapeCalibrationInformation.size(); ++Index)
    {
        MetaShapeCamera<double>& Camera = MetaShapeCalibrationInformation[Index];

        Camera.SetIntrinsics(InIntrisicsVector[Index]);
        Camera.SetRadialDistortion(InDistortionVector[Index]);
        Camera.SetExtrinsics(InExtrinsicsVector[Index]);
    }
}

FeatureBasedBundleAdjustment::FeatureBasedBundleAdjustment()
    : pImpl(new Private())
{
}

FeatureBasedBundleAdjustment::~FeatureBasedBundleAdjustment()
{
    if (pImpl)
    {
        delete pImpl;
        pImpl = nullptr;
    }
}

FeatureBasedBundleAdjustment::FeatureBasedBundleAdjustment(FeatureBasedBundleAdjustment&& InOther) noexcept
    : pImpl(InOther.pImpl)
{
    InOther.pImpl = nullptr;
}

FeatureBasedBundleAdjustment& FeatureBasedBundleAdjustment::operator=(FeatureBasedBundleAdjustment&& InOther) noexcept
{
    if (this == &InOther)
    {
        return *this;
    }

    if (pImpl)
    {
        delete pImpl;
        pImpl = nullptr;
    }

    pImpl = InOther.pImpl;
    InOther.pImpl = nullptr;

    return *this;
}

bool FeatureBasedBundleAdjustment::Init(const std::map<std::string, OpenCVCameraD>& InCalibrationInformation)
{
    if (InCalibrationInformation.empty())
    {
        return false;
    }

    std::transform(InCalibrationInformation.begin(), 
                   InCalibrationInformation.end(), 
                   std::back_inserter(pImpl->MetaShapeCalibrationInformation), 
                   [](const std::pair<std::string, OpenCVCameraD>& InElem) 
    { 
        return OpenCVCamera2MetaShapeCamera<double, double>(InElem.first.c_str(), InElem.second); 
    });

    return true;
}

bool FeatureBasedBundleAdjustment::BundleAdjustment(const std::map<size_t, std::vector<double>>& InPoints3d,
                                                    const std::map<size_t, std::vector<std::vector<double>>>& InCameraPoints,
                                                    const std::map<size_t, std::vector<std::vector<bool>>>& InVisibility,
                                                    const FBFullBundleAdjustmentParameters& InParams,
                                                    double& OutMse)
{
    if (InParams.NumberOfFrames == 0)
    {
        return false;
    }

    try
    {
        TITAN_RESET_ERROR;

        std::vector<Eigen::Matrix3<double>> IntrisicsVector;
        std::vector<Eigen::VectorX<double>> DistortionVector;
        std::vector<Eigen::Matrix4<double>> ExtrinsicsVector;
        pImpl->UnpackCameras(IntrisicsVector, DistortionVector, ExtrinsicsVector);

        calib::FBBAParams Params;
        std::vector<Eigen::MatrixX<double>> Points3d;
        std::vector<std::vector<Eigen::MatrixX<double>>> ImagePoints;
        std::vector<std::vector<std::vector<bool>>> Visibility;

        PrepackBundleAdjustmentParameters(InPoints3d, InCameraPoints, InVisibility, InParams, 
                                          Points3d, ImagePoints, Visibility, Params);

        Params.optimizeIntrinsics = InParams.bOptimizeIntrinsics;
        Params.optimizeDistortion = InParams.bOptimizeDistortion;
        Params.optimizePoints = InParams.bOptimizePoints;

        std::optional<double> Mse = calib::featureBundleAdjustment(Points3d, ImagePoints, Visibility, IntrisicsVector, ExtrinsicsVector, DistortionVector, Params);
        if (!Mse.has_value())
        {
            return false;
        }

        OutMse = Mse.value();
        pImpl->PackCameras(IntrisicsVector, DistortionVector, ExtrinsicsVector);

        return true;
    }
    catch (const std::exception& InException)
    {
        TITAN_HANDLE_EXCEPTION("Failed to do the bundle adjustment: {}", InException.what());
    }
}

bool FeatureBasedBundleAdjustment::RunPipeline(const std::map<size_t, std::vector<double>>& InPoints3d,
                                               const std::map<size_t, std::vector<std::vector<double>>>& InCameraPoints,
                                               const std::map<size_t, std::vector<std::vector<bool>>>& InVisibility,
                                               const FBBundleAdjustmentParameters& InParams,
                                               double& OutMse)
{
    if (InParams.NumberOfFrames == 0)
    {
        return false;
    }

    try
    {
        TITAN_RESET_ERROR;

        std::vector<Eigen::Matrix3<double>> IntrisicsVector;
        std::vector<Eigen::VectorX<double>> DistortionVector;
        std::vector<Eigen::Matrix4<double>> ExtrinsicsVector;
        pImpl->UnpackCameras(IntrisicsVector, DistortionVector, ExtrinsicsVector);

        calib::FBBAParams Params;
        std::vector<Eigen::MatrixX<double>> Points3d;
        std::vector<std::vector<Eigen::MatrixX<double>>> ImagePoints;
        std::vector<std::vector<std::vector<bool>>> Visibility;

        PrepackBundleAdjustmentParameters(InPoints3d, InCameraPoints, InVisibility, InParams, 
                                          Points3d, ImagePoints, Visibility, Params);

        bool fixedScale = Params.fixedScale;

        // Stage 1
        Params.optimizeIntrinsics = false;
        Params.optimizeDistortion = false;
        Params.optimizePoints = true;

        // Don't constraint scale when optimizing points
        Params.fixedScale = false;

        std::optional<double> Mse = calib::featureBundleAdjustment(Points3d, ImagePoints, Visibility, IntrisicsVector, ExtrinsicsVector, DistortionVector, Params);
        if (!Mse.has_value())
        {
            return false;
        }

        // Stage 2
        Params.optimizeIntrinsics = true;
        Params.optimizeDistortion = true;
        Params.optimizePoints = false;
        Params.fixedScale = fixedScale;

        Mse = calib::featureBundleAdjustment(Points3d, ImagePoints, Visibility, IntrisicsVector, ExtrinsicsVector, DistortionVector, Params);
        if (!Mse.has_value())
        {
            return false;
        }

        // Stage 3
        Params.optimizeIntrinsics = true;
        Params.optimizeDistortion = true;
        Params.optimizePoints = true;
        Params.fixedScale = fixedScale;

        Mse = calib::featureBundleAdjustment(Points3d, ImagePoints, Visibility, IntrisicsVector, ExtrinsicsVector, DistortionVector, Params);
        if (!Mse.has_value())
        {
            return false;
        }

        OutMse = Mse.value();
        pImpl->PackCameras(IntrisicsVector, DistortionVector, ExtrinsicsVector);

        return true;
    }
    catch (const std::exception& InException)
    {
        TITAN_HANDLE_EXCEPTION("Failed to do the bundle adjustment: {}", InException.what());
    }
}

bool FeatureBasedBundleAdjustment::GetCalibrationInfo(std::map<std::string, OpenCVCameraD>& OutCalibrationInformation)
{
    if (pImpl->MetaShapeCalibrationInformation.empty())
    {
        return false;
    }

    std::transform(pImpl->MetaShapeCalibrationInformation.begin(), 
                   pImpl->MetaShapeCalibrationInformation.end(), 
                   std::inserter(OutCalibrationInformation, OutCalibrationInformation.end()), 
                   [](const MetaShapeCamera<double>& InCamera)
    { 
        return MetaShape2OpenCVCamera<double, double>(InCamera);
    });

    return true;
}

} // namespace TITAN_API_NAMESPACE
