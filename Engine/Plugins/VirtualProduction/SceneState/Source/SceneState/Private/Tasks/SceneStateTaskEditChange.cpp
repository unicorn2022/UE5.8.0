// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/SceneStateTaskEditChange.h"

namespace UE::SceneState
{

#if WITH_EDITOR
FTaskEditChange::FTaskEditChange()
	: FPropertyChangedEvent(nullptr)
{
}

bool FTaskEditChange::IsTaskChange() const
{
	return ChangedObject == ETaskObjectType::Task;
}

bool FTaskEditChange::IsTaskInstanceChange() const
{
	return ChangedObject == ETaskObjectType::TaskInstance;
}
#endif // WITH_EDITOR

} // UE::SceneState
