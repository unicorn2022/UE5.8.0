// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/MathFwd.h"

class FName;

namespace UE::Mutable::Private
{
class FMesh;

/**  */
extern void MeshTransformWithBoneInline(FMesh* Mesh, const FMatrix44f& Transform, FName BoneName, const float Threshold);

}
