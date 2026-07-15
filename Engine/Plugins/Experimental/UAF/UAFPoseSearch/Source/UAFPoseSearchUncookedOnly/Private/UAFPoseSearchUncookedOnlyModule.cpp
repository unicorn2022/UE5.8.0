// Copyright Epic Games, Inc. All Rights Reserved.

#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"

namespace UE::PoseSearch
{

//////////////////////////////////////////////////////////////////////////
// FPoseSearchEditorModule
class FUAFPoseSearchUncookedOnlyModule : public IModuleInterface
{
};

} // namespace UE::PoseSearch

IMPLEMENT_MODULE(UE::PoseSearch::FUAFPoseSearchUncookedOnlyModule, UAFPoseSearchUncookedOnly)

