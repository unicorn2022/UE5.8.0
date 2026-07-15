// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#ifndef OBJECT_TRACE_ENABLED
#define OBJECT_TRACE_ENABLED ((UE_TRACE_ENABLED && !IS_PROGRAM && !UE_BUILD_SHIPPING) || UE_TRACE_MINIMAL_ENABLED)
#endif
