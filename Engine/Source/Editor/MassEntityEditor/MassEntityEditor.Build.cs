// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	[SupportedTargetTypes(TargetType.Editor, TargetType.Program)]
	public class MassEntityEditor : ModuleRules
	{
		public MassEntityEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Warning;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"MassEntity",
					"SlateCore",
					"UnrealEd",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"AIGraph",
					"AssetTools",
					"ComponentVisualizers",
					"DetailCustomizations",
					"EditorSubsystem",
					"GraphEditor",
					"InputCore",
					"KismetWidgets",
					"MassCore",
					"MassDeveloper",
					"Projects",
					"PropertyEditor",
					"RenderCore",
					"RewindDebuggerInterface",
					"RewindDebuggerRuntimeInterface",
					"Slate",
					"ToolMenus",
					"TraceLog",
					"TraceServices",
				}
			);

			if (Target.bBuildDeveloperTools == true)
			{
				PrivateDependencyModuleNames.Add("MessageLog");
				DynamicallyLoadedModuleNames.Add("MassEntityDebugger");
			}
		}
	}
}
