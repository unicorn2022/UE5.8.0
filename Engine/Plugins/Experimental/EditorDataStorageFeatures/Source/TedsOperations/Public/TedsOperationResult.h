// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementRowHandleArray.h"

namespace UE::Editor::DataStorage::Operations
{
	
struct FResult
{
	FRowHandleArray Created;
	FRowHandleArray Changed;
	FRowHandleArray Removed;
};

}
