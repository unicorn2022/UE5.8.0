// Copyright Epic Games, Inc. All Rights Reserved.

#ifndef OPENCV_HEADERS_GUARD
#define OPENCV_HEADERS_GUARD
#else
#error Nesting BeforeOpenCvHeaders.h is not allowed!
#endif

#include <calib/Defs.h>

#ifdef WITH_EDITOR
#include "PreOpenCVHeaders.h"
#else
CALIB_PUSH_MACRO("check")
#undef check
#endif
