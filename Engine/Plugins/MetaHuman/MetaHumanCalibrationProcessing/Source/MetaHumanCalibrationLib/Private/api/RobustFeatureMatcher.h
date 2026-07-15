// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Defs.h"

#include "OpenCVCamera.h"

#include <map>
#include <vector>
#include <string>

namespace TITAN_API_NAMESPACE
{

/**
 * Feature detector and matcher type.
 * SlowPrecise: SIFT with BRUTEFORCE (L2 distance) - more accurate, slower.
 * FastLessPrecise: AKAZE with BRUTEFORCE_HAMMING - faster, less accurate.
 */
enum class FeatureDetectorType
{
    SlowPrecise,
    FastLessPrecise
};

class TITAN_API RobustFeatureMatcher
{
public:
    RobustFeatureMatcher();
    ~RobustFeatureMatcher();

    RobustFeatureMatcher(RobustFeatureMatcher&& InOther) noexcept;
    RobustFeatureMatcher& operator=(RobustFeatureMatcher&& InOther) noexcept;

    RobustFeatureMatcher(const RobustFeatureMatcher&) = delete;
    RobustFeatureMatcher& operator=(const RobustFeatureMatcher&) = delete;

    /**
     * Initializes Robust Feature Matcher using calibration json file path
     * @param[in] InCalibrationFilePath Absolute path to the calibration json file
     * @param[in] InReprojectionThreshold Reprojection threshold
     * @param[in] InRatioThreshold Ratio threshold
     * @param[in] InFeatureDetectorType SlowPrecise (SIFT) or FastLessPrecise (AKAZE)
     * @returns True if initialization is successful, False otherwise.
     */
    bool Init(const std::string& InCalibrationFilePath,
              double InReprojectionThreshold,
              double InRatioThreshold,
              FeatureDetectorType InFeatureDetectorType = FeatureDetectorType::SlowPrecise);

    /**
     * Initializes Robust Feature Matcher using calibration information from each camera
     * @param[in] InCalibrationInformation A map of calibration information for each camera
     * @param[in] InReprojectionThreshold Reprojection threshold
     * @param[in] InRatioThreshold Ratio threshold
     * @param[in] InFeatureDetectorType SlowPrecise (SIFT) or FastLessPrecise (AKAZE)
     * @returns True if initialization is successful, False otherwise.
     */
    bool Init(const std::map<std::string, OpenCVCameraD>& InCalibrationInformation,
             double InReprojectionThreshold,
             double InRatioThreshold,
             FeatureDetectorType InFeatureDetectorType = FeatureDetectorType::SlowPrecise);

    /**
     * Adds camera information
     * @param[in] InCameraName Camera name/label/user id
     * @param[in] InWidth Image width
     * @param[in] InHeight Image height
     * @returns True if successful, False otherwise
     */
    bool AddCamera(const std::string& InCameraName, int32_t InWidth, int32_t InHeight);

    /**
     * Provides the detected feature for a specific frame
     * @param[in] InFrame Frame Id
     * @param[out] OutPoints3d 3D points
     * @param[out] OutCameraPoints Camera points
     * @param[out] OutPoints3dReprojected Reprojected camera points
     * @param[out] OutVisibility Visibility of camera points
     * @returns True if successful, False otherwise
     */
    bool GetDetectedFeatures(size_t InFrame,
                             std::vector<double>& OutPoints3d,
                             std::vector<std::vector<double>>& OutCameraPoints,
                             std::vector<std::vector<double>>& OutPoints3dReprojected,
                             std::vector<std::vector<bool>>& OutVisibility);

    /**
     * Provides the detected feature for an array of frames
     * @param[in] InFrames Frames
     * @param[out] OutPoints3d 3D points
     * @param[out] OutCameraPoints Camera points
     * @param[out] OutPoints3dReprojected Reprojected camera points
     * @param[out] OutVisibility Visibility of camera points
     * @returns True if successful, False otherwise
     */
    bool GetDetectedFeatures(const std::vector<size_t>& InFrames,
                             std::map<size_t, std::vector<double>>& OutPoints3d,
                             std::map<size_t, std::vector<std::vector<double>>>& OutCameraPoints,
                             std::map<size_t, std::vector<std::vector<double>>>& OutPoints3dReprojected,
                             std::map<size_t, std::vector<std::vector<bool>>>& OutVisibility);

    /**
     * Detects the matching features for a single frame
     * @param[in] InFrame Frame Id
     * @param[in] InImages An array of stereo pair frame data
     * @returns True if successful, False otherwise
     */
    bool DetectFeatures(size_t InFrame, 
                        const std::vector<const unsigned char*>& InImages);

    /**
     * Detects the matching features an array of stereo pair frames
     * @param[in] InImages Map containing all the frame Ids and stereo pair frame data
     * @returns True if successful, False otherwise
     */
    bool DetectFeatures(const std::map<size_t, std::vector<const unsigned char*>>& InImages);

private:
    struct Private;
    Private* pImpl;
};

}
