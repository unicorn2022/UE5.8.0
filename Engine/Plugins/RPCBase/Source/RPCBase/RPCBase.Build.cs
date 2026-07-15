// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RPCBase : ModuleRules
{
	public RPCBase(ReadOnlyTargetRules target) : base(target)
	{
		PrivateDependencyModuleNames.AddRange(
		[
			"Core",
			"CoreUObject",
			"Json"
		]);

		if (Target.Configuration < UnrealTargetConfiguration.Shipping)
		{
			PublicDependencyModuleNames.Add("ExternalRpcRegistry");
			PublicDependencyModuleNames.Add("HTTPServer");
			PublicDefinitions.Add("WITH_RPC_REGISTRY=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_RPC_REGISTRY=0");
		}
	}
}