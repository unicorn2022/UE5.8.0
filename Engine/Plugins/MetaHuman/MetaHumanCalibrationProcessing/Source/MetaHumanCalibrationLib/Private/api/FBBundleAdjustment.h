// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Defs.h"
#include "OpenCVCamera.h"

#include <map>
#include <string>
#include <vector>

namespace TITAN_API_NAMESPACE
{

struct FBBundleAdjustmentParameters
{
    size_t Iterations = 50;
    size_t NumberOfFrames = 0;

    std::vector<int> FixedIntrinsicIndices = {};
    std::vector<int> FixedDistortionIndices = {};

    bool bFixedScale = false;
    // Two 2d points per camera for every frame
    // Two 2d points are packed in vector [Point1.X, Point1.Y, Point2.X, Point2.Y]
    std::vector<std::vector<std::vector<double>>> ReferencePoints;

    double ReferenceDistance;
};

struct FBFullBundleAdjustmentParameters : 
    public FBBundleAdjustmentParameters
{
    bool bOptimizeIntrinsics = false;
    bool bOptimizeDistortion = false;
    bool bOptimizePoints = false;
};

class TITAN_API FeatureBasedBundleAdjustment
{
public:
    FeatureBasedBundleAdjustment();
    ~FeatureBasedBundleAdjustment();

    FeatureBasedBundleAdjustment(FeatureBasedBundleAdjustment&& InOther) noexcept;
    FeatureBasedBundleAdjustment& operator=(FeatureBasedBundleAdjustment&& InOther) noexcept;

    FeatureBasedBundleAdjustment(const FeatureBasedBundleAdjustment&) = delete;
    FeatureBasedBundleAdjustment& operator=(const FeatureBasedBundleAdjustment&) = delete;

    /**
     * Initializes Feature Based Bundle Adjustment using calibration information from each camera
     * @param[in] InCalibrationInformation A map of calibration information for each camera
     * @returns True if initialization is successful, False otherwise.
     */
    bool Init(const std::map<std::string, OpenCVCameraD>& InCalibrationInformation);

    /**
     * Runs the bundle adjustment process using the detected features (check RobustFeatureMatcher)
     * @param[in] InPoints3d 3D points per frame
     * @param[in] InCameraPoints Camera points per frame
     * @param[in] InVisibility Visibility of camera points per frame
     * @param[in] InParams FeatureBased Bundle Adjustment parameters with flexibility of specifying what to optimize
     * @param[out] OutMse MSE calculated after bundle adjustment process
     * @returns True if initialization is successful, False otherwise.
     */
    bool BundleAdjustment(const std::map<size_t, std::vector<double>>& InPoints3d,
                          const std::map<size_t, std::vector<std::vector<double>>>& InCameraPoints,
                          const std::map<size_t, std::vector<std::vector<bool>>>& InVisibility,
                          const FBFullBundleAdjustmentParameters& InParams,
                          double& OutMse);

    /**
     * Runs the 3 stage pipeline process using the detected features (check RobustFeatureMatcher)
     * @param[in] InPoints3d 3D points per frame
     * @param[in] InCameraPoints Camera points per frame
     * @param[in] InVisibility Visibility of camera points per frame
     * @param[in] InParams FeatureBased Bundle Adjustment parameters
     * @param[out] OutMse MSE calculated after bundle adjustment process
     * @returns True if initialization is successful, False otherwise.
     */
    bool RunPipeline(const std::map<size_t, std::vector<double>>& InPoints3d,
                     const std::map<size_t, std::vector<std::vector<double>>>& InCameraPoints,
                     const std::map<size_t, std::vector<std::vector<bool>>>& InVisibility,
                     const FBBundleAdjustmentParameters& InParams,
                     double& OutMse);

    /**
     * Provides the current map of calibration information for each camera
     * @param[out] OutCalibrationInformation A map of calibration information for each camera
     * @returns True if initialization is successful, False otherwise.
     */
    bool GetCalibrationInfo(std::map<std::string, OpenCVCameraD>& OutCalibrationInformation);

private:
    struct Private;
    Private* pImpl;
};

} // namespace TITAN_API_NAMESPACE
