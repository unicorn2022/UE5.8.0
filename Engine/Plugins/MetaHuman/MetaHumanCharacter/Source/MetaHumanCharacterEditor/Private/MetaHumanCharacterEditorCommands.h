// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Tools/InteractiveToolsCommands.h"

class FMetaHumanCharacterEditorCommands : public TCommands<FMetaHumanCharacterEditorCommands>
{
public:

	FMetaHumanCharacterEditorCommands();

	virtual void RegisterCommands() override;

public:

	TSharedPtr<FUICommandInfo> SaveThumbnail;
	TSharedPtr<FUICommandInfo> AutoRigFaceBlendShapes;
	TSharedPtr<FUICommandInfo> AutoRigFaceJointsOnly;
	TSharedPtr<FUICommandInfo> RemoveFaceRig;
	TSharedPtr<FUICommandInfo> DownloadTextureSources;
	TSharedPtr<FUICommandInfo> RefreshPreview;
};

class FMetaHumanCharacterEditorDebugCommands : public TCommands<FMetaHumanCharacterEditorDebugCommands>
{
public:

	FMetaHumanCharacterEditorDebugCommands();

	virtual void RegisterCommands() override;

public:

	TSharedPtr<FUICommandInfo> SaveFaceState;
	TSharedPtr<FUICommandInfo> SaveFaceStateToDNA;
	TSharedPtr<FUICommandInfo> DumpFaceStateDataForAR;
	TSharedPtr<FUICommandInfo> SaveBodyState;
	TSharedPtr<FUICommandInfo> SaveFaceTextures;
	TSharedPtr<FUICommandInfo> SaveEyePreset;
	TSharedPtr<FUICommandInfo> TakeHighResScreenshot;
};

class FMetaHumanCharacterEditorToolCommands : public TInteractiveToolCommands<FMetaHumanCharacterEditorToolCommands>
{
public:

	FMetaHumanCharacterEditorToolCommands();

	virtual void RegisterCommands() override;

	virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& OutToolCDOs) override;

public:

	TSharedPtr<FUICommandInfo> AcceptOrCompleteActiveTool;
	TSharedPtr<FUICommandInfo> CancelOrCompleteActiveTool;

	// Preset tools
	TSharedPtr<FUICommandInfo> LoadPresetsTools;
	TSharedPtr<FUICommandInfo> BeginPresetsTool;
	TSharedPtr<FUICommandInfo> PresetProperties;
	TSharedPtr<FUICommandInfo> ApplyPreset;

	// Import tools
	TSharedPtr<FUICommandInfo> LoadMeshImportTools;
	TSharedPtr<FUICommandInfo> BeginImportFromDNATool;
	TSharedPtr<FUICommandInfo> BeginImportFromIdentityTool;
	TSharedPtr<FUICommandInfo> BeginImportFromTemplateTool;
	TSharedPtr<FUICommandInfo> BeginImportFromCustomMeshTool;

	// Head and Body tools
	TSharedPtr<FUICommandInfo> LoadHeadAndBodyTools;
	TSharedPtr<FUICommandInfo> BeginBodyModelTool;
	TSharedPtr<FUICommandInfo> BeginBodyModelParametricTool;
	TSharedPtr<FUICommandInfo> BeginBodyFixedCompatibilityTool;
	TSharedPtr<FUICommandInfo> BeginHeadAndBodyBlendTool;
	TSharedPtr<FUICommandInfo> BeginHeadModelTools;
	TSharedPtr<FUICommandInfo> BeginHeadModelEyelashesTool;
	TSharedPtr<FUICommandInfo> BeginHeadModelTeethTool;
	TSharedPtr<FUICommandInfo> BeginHeadMaterialsTools;
	TSharedPtr<FUICommandInfo> BeginHeadMaterialsTeethTool;
	TSharedPtr<FUICommandInfo> BeginHeadMaterialsEyelashesTool;
	TSharedPtr<FUICommandInfo> BeginFaceMoveTool;
	TSharedPtr<FUICommandInfo> BeginFaceSculptTool;

	// Materials tools
	TSharedPtr<FUICommandInfo> LoadMaterialsTools;
	TSharedPtr<FUICommandInfo> BeginSkinTool;
	TSharedPtr<FUICommandInfo> BeginEyesTool;
	TSharedPtr<FUICommandInfo> BeginMakeupTool;
	TSharedPtr<FUICommandInfo> BeginTextureMaterialOverrideTool;

	// Hair & Clothing tools
	TSharedPtr<FUICommandInfo> LoadHairAndClothingTools;
	TSharedPtr<FUICommandInfo> BeginWardrobeSelectionTool;
	TSharedPtr<FUICommandInfo> BeginCostumeDetailsTool;
	TSharedPtr<FUICommandInfo> PrepareAccessory;
	TSharedPtr<FUICommandInfo> UnprepareAccessory;
	TSharedPtr<FUICommandInfo> WearAcceessory;
	TSharedPtr<FUICommandInfo> RemoveAccessory;
	TSharedPtr<FUICommandInfo> AccessoryProperties;

	// Pipeline tools
	TSharedPtr<FUICommandInfo> LoadPipelineTools;
	TSharedPtr<FUICommandInfo> BeginPipelineTool;

	// Export tools
	TSharedPtr<FUICommandInfo> LoadExportTools;
	TSharedPtr<FUICommandInfo> BeginDCCExportTool;
	TSharedPtr<FUICommandInfo> BeginDNAExportTool;
	TSharedPtr<FUICommandInfo> BeginGeometryExportTool;
	TSharedPtr<FUICommandInfo> BeginMaterialsExportTool;
};

class FMetaHumanCharacterEditorViewportToolbarCommands : public TCommands<FMetaHumanCharacterEditorViewportToolbarCommands>
{
public:
	FMetaHumanCharacterEditorViewportToolbarCommands();

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> PerspectiveProjection;
	TSharedPtr<FUICommandInfo> OrthographicProjection;

	TSharedPtr<FUICommandInfo> FocusFace;
	TSharedPtr<FUICommandInfo> FocusBody;
	TSharedPtr<FUICommandInfo> FocusFar;
	TSharedPtr<FUICommandInfo> FocusHands;
	TSharedPtr<FUICommandInfo> FocusFeet;

	TSharedPtr<FUICommandInfo> OrthoTop;
	TSharedPtr<FUICommandInfo> OrthoBottom;
	TSharedPtr<FUICommandInfo> OrthoLeft;
	TSharedPtr<FUICommandInfo> OrthoRight;
	TSharedPtr<FUICommandInfo> OrthoFront;
	TSharedPtr<FUICommandInfo> OrthoBack;


};
