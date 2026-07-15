// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Defs.h"
#include <nrr/landmarks/LandmarkInstance.h>
#include <nls/geometry/MetaShapeCamera.h>
#include <LandmarkData.h>

#include <string>
#include <vector>

namespace TITAN_API_NAMESPACE
{

std::shared_ptr<const TITAN_NAMESPACE::LandmarkInstance<float, 2>> CreateLandmarkInstanceForCamera(const std::map<std::string, FaceTrackingLandmarkData>& perCameraLandmarkData,
                                                                                  const std::map<std::string, std::vector<std::string>>& curvesToMerge,
                                                                                  const TITAN_NAMESPACE::MetaShapeCamera<float>& camera);

std::shared_ptr<const TITAN_NAMESPACE::LandmarkInstance<float, 3>> Create3dLandmarkInstance(const std::map<std::string, const FaceTrackingLandmarkData>& landmarkData,
                                                                           const std::map<std::string, std::vector<std::string>>& curvesToMerge);

} // namespace TITAN_API_NAMESPACE
