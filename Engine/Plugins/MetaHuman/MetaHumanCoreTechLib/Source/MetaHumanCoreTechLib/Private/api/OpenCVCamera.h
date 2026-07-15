// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Defs.h"
#include <stdint.h>

namespace TITAN_API_NAMESPACE
{

template<typename T>
struct OpenCVCameraTemplate
{
    int32_t width;
    int32_t height;
    T fx;
    T fy;
    T cx;
    T cy;
    T k1;
    T k2;
    T k3;
    T p1;
    T p2;
    //! Transform from world coordinates to camera coordinates in column-major format.
    T Extrinsics[16];
};

using OpenCVCameraD = OpenCVCameraTemplate<double>;
using OpenCVCamera = OpenCVCameraTemplate<float>;

} // namespace TITAN_API_NAMESPACE
