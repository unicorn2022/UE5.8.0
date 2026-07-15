// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class DatasmithMax2027Target : DatasmithMaxBaseTarget
{
	public DatasmithMax2027Target(TargetInfo Target) : base(Target)
	{
		LaunchModuleName = "DatasmithMax2027";
		ExeBinariesSubFolder = @"3DSMax\2027";

		AddCopyPostBuildStep(Target);
	}
}
