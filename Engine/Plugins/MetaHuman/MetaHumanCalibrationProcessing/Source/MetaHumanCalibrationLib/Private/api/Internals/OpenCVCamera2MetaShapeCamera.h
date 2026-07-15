// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "../Defs.h"
#include "../OpenCVCamera.h"

#include <nls/geometry/MetaShapeCamera.h>

namespace TITAN_API_NAMESPACE
{

template <class T, class C = float>
TITAN_NAMESPACE::MetaShapeCamera<T> OpenCVCamera2MetaShapeCamera(const char* InCameraName, const OpenCVCameraTemplate<C>& InCameraParameters)
{
    TITAN_NAMESPACE::MetaShapeCamera<T> camera;
    camera.SetLabel(InCameraName);
    camera.SetWidth(InCameraParameters.width);
    camera.SetHeight(InCameraParameters.height);
    Eigen::Matrix<T, 3, 3> intrinsics = Eigen::Matrix3<T>::Identity();
    intrinsics(0, 0) = T(InCameraParameters.fx);
    intrinsics(1, 1) = T(InCameraParameters.fy);
    intrinsics(0, 2) = T(InCameraParameters.cx);
    intrinsics(1, 2) = T(InCameraParameters.cy);
    camera.SetIntrinsics(intrinsics);
    const Eigen::Matrix4<T> extrinsics = Eigen::Map<const Eigen::Matrix4<C>>(InCameraParameters.Extrinsics).template cast<T>();
    camera.SetExtrinsics(extrinsics);
    camera.SetRadialDistortion(Eigen::Vector4<C>(InCameraParameters.k1, InCameraParameters.k2, InCameraParameters.k3, 0.0).template cast<T>());
    // note that metashape camera has swapped tangential distortion compared to opencv
    camera.SetTangentialDistortion(Eigen::Vector4<C>(InCameraParameters.p2, InCameraParameters.p1, 0.0, 0.0).template cast<T>());

    return camera;
}

template <class T, class C = float>
static std::pair<std::string, OpenCVCameraTemplate<C>> MetaShape2OpenCVCamera(const TITAN_NAMESPACE::MetaShapeCamera<T>& InCamera)
{
    OpenCVCameraTemplate<C> camera;
    camera.fx = C(InCamera.Intrinsics()(0, 0));
    camera.fy = C(InCamera.Intrinsics()(1, 1));
    camera.cx = C(InCamera.Intrinsics()(0, 2));
    camera.cy = C(InCamera.Intrinsics()(1, 2));
    camera.width = InCamera.Width();
    camera.height = InCamera.Height();
    camera.k1 = C(InCamera.RadialDistortion()[0]);
    camera.k2 = C(InCamera.RadialDistortion()[1]);
    camera.k3 = C(InCamera.RadialDistortion()[2]);

    // Swapped tangetial distortion
    camera.p1 = C(InCamera.TangentialDistortion()[1]);
    camera.p2 = C(InCamera.TangentialDistortion()[0]);

    Eigen::Map<Eigen::Matrix4<C>> mappedExtrinsics(camera.Extrinsics);
    mappedExtrinsics = InCamera.Extrinsics().Matrix().template cast<C>();
    return std::make_pair(InCamera.Label(), camera);
}

} // namespace TITAN_API_NAMESPACE
