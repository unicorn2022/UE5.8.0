// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorCommands.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "Tools/MetaHumanCharacterEditorBodyEditingTools.h"
#include "Tools/MetaHumanCharacterEditorCostumeTools.h"
#include "Tools/MetaHumanCharacterEditorEyesTool.h"
#include "Tools/MetaHumanCharacterEditorFaceEditingTools.h"
#include "Tools/MetaHumanCharacterEditorHeadModelTool.h"
#include "Tools/MetaHumanCharacterEditorImportTools.h"
#include "Tools/MetaHumanCharacterEditorMakeupTool.h"
#include "Tools/MetaHumanCharacterEditorPresetsTool.h"
#include "Tools/MetaHumanCharacterEditorSkinTool.h"
#include "Tools/MetaHumanCharacterEditorTextureMaterialOverrideTool.h"
#include "Tools/MetaHumanCharacterEditorWardrobeTools.h"
#include "Tools/MetaHumanCharacterEditorExportTools.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

FMetaHumanCharacterEditorCommands::FMetaHumanCharacterEditorCommands()
	: TCommands<FMetaHumanCharacterEditorCommands>(
		TEXT("MetaHumanCharacterEditor"),
		LOCTEXT("MetaHumanCharacterEditorCommandsContext", "MetaHuman Character Editor"),
		NAME_None,
		FMetaHumanCharacterEditorStyle::Get().GetStyleSetName()
	)
{
}

void FMetaHumanCharacterEditorCommands::RegisterCommands()
{
	// These are part of the asset editor UI
	UI_COMMAND(SaveThumbnail, "Save Thumbnail", "Save the character preview thumbnail.", EUserInterfaceActionType::Button, FInputChord{});
	UI_COMMAND(AutoRigFaceBlendShapes, "Create Full Rig", "Calls Auto-Rigging service and retrieves full DNA (blend shapes included).", EUserInterfaceActionType::Button, FInputChord{});
	UI_COMMAND(AutoRigFaceJointsOnly, "Create Joints Only Rig", "Calls Auto-Rigging service and retrieves joints-only DNA.", EUserInterfaceActionType::Button, FInputChord{});
	UI_COMMAND(RemoveFaceRig, "Remove Rig", "Remove rig from the character allowing it to be edited", EUserInterfaceActionType::Button, FInputChord{});
	UI_COMMAND(DownloadTextureSources, "Download Texture Sources", "Request to download texture sources", EUserInterfaceActionType::Button, FInputChord{});

	UI_COMMAND(RefreshPreview, "Refresh Preview", "Rebuild the preview actor", EUserInterfaceActionType::Button, FInputChord{});
}

FMetaHumanCharacterEditorDebugCommands::FMetaHumanCharacterEditorDebugCommands()
	: TCommands<FMetaHumanCharacterEditorDebugCommands>(
		TEXT("MetaHumanCharacterEditorDebug"),
		LOCTEXT("MetaHumanCharacterEditorDebugCommandsContext", "MetaHuman Character Editor Debug"),
		NAME_None,
		FMetaHumanCharacterEditorStyle::Get().GetStyleSetName()
	)
{
}

void FMetaHumanCharacterEditorDebugCommands::RegisterCommands()
{
	UI_COMMAND(SaveFaceState, "Save Face State", "Saves the internal state of the edited face to a file", EUserInterfaceActionType::Button, FInputChord{});
	UI_COMMAND(SaveFaceStateToDNA, "Save Face State to DNA", "Saves the internal state of the edited face to a DNA file", EUserInterfaceActionType::Button, FInputChord{});
	UI_COMMAND(DumpFaceStateDataForAR, "Dump Face Data for AR", "Dumps Auto Rigging debug data for the face state to a folder", EUserInterfaceActionType::Button, FInputChord{});
	UI_COMMAND(SaveBodyState, "Save Body State", "Saves the internal state of the edited body to a file", EUserInterfaceActionType::Button, FInputChord{});
	UI_COMMAND(SaveFaceTextures, "Save Face Textures", "Saves all the synthesized textures of the face (if any) to a target folder", EUserInterfaceActionType::Button, FInputChord{});
	UI_COMMAND(SaveEyePreset, "Save Eye Preset", "Saves the current eye settings as a preset", EUserInterfaceActionType::Button, FInputChord{});
	UI_COMMAND(TakeHighResScreenshot, "Take High Res Screenshot", "Takes a high resolution screenshot of the MetaHuman Character viewport", EUserInterfaceActionType::Button, FInputChord{});
}

FMetaHumanCharacterEditorToolCommands::FMetaHumanCharacterEditorToolCommands()
	: TInteractiveToolCommands<FMetaHumanCharacterEditorToolCommands>(
		TEXT("MetaHumanCharacterEditorTools"), // Context name for fast lookup and in the style to assign icons to commands
		LOCTEXT("MetaHumanCharacterEditorToolsCommandsContext", "MetaHumanh Character Editor Tools"),
		NAME_None,
		FMetaHumanCharacterEditorStyle::Get().GetStyleSetName()
	)
{
}

void FMetaHumanCharacterEditorToolCommands::RegisterCommands()
{
	TInteractiveToolCommands<FMetaHumanCharacterEditorToolCommands>::RegisterCommands();

	// These allow us to link up to pressed keys
	UI_COMMAND(AcceptOrCompleteActiveTool, "Accept", "Accept the active tool", EUserInterfaceActionType::Button, FInputChord{ EKeys::Enter });
	UI_COMMAND(CancelOrCompleteActiveTool, "Cancel", "Cancel the active tool or clear current selection", EUserInterfaceActionType::Button, FInputChord{ EKeys::Escape });

	// These get linked to various tool buttons.
	UI_COMMAND(LoadPresetsTools, "Presets", "Browse and apply pre-built MetaHuman templates. Gives a starting point for appearance before diving into custom adjustments.", EUserInterfaceActionType::RadioButton, FInputChord{});
	UI_COMMAND(BeginPresetsTool, "Edit Presets", "Edit Presets Library", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(PresetProperties, "Presets Properties", "Edit Preset Properties", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(ApplyPreset, "Apply Preset", "Apply the Preset", EUserInterfaceActionType::ToggleButton, FInputChord{});

	UI_COMMAND(LoadMeshImportTools, "Import", "Import external mesh or data to drive your MetaHuman's head and body shape.", EUserInterfaceActionType::RadioButton, FInputChord{});
	UI_COMMAND(BeginImportFromDNATool, "From DNA", "Import a MetaHuman directly from a DNA file.", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginImportFromIdentityTool, "From Identity", "Create a MetaHuman derived from an existing MetaHuman Identity asset.", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginImportFromTemplateTool, "From Template", "Generate a MetaHuman starting from a predefined template shape.", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginImportFromCustomMeshTool, "From Custom Mesh", "Create a MetaHuman by uploading your own 3D mesh.", EUserInterfaceActionType::ToggleButton, FInputChord{});

	UI_COMMAND(LoadHeadAndBodyTools, "Head & Body", "Main editing workspace for sculpting the head shape, blending features, and configuring body proportions.", EUserInterfaceActionType::RadioButton, FInputChord{});
	UI_COMMAND(BeginBodyModelTool, "Body Params", "Control your character’s body shape using parametric or fixed modes.", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginBodyModelParametricTool, "Parametric", "Parametric Body Tool", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginBodyFixedCompatibilityTool, "Fixed (Compatibility)", "Fixed Compatibility Body Tool", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginHeadAndBodyBlendTool, "Blend", "Blend between up to four existing MetaHuman presets to create a unique face and body. Drag blending handles to weight the influence of each source character.", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginHeadModelTools, "Teeth & Eyelashes", "Granular customization for your character’s teeth and eyelashes.", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginHeadModelEyelashesTool, "Eyelashes", "Eyelashes, selection of eyelash presets with corresponding grooms", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginHeadModelTeethTool, "Teeth", "Teeth, parametrically adjust the teeth geometry", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginFaceMoveTool, "Head Transform", "Make broad adjustments to the position of your character’s facial features.", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginFaceSculptTool, "Head Sculpt", "Make fine adjustments to a smaller area of your character’s face.", EUserInterfaceActionType::ToggleButton, FInputChord{});

	UI_COMMAND(LoadMaterialsTools, "Materials", "Use the materials controls to customize the character’s skin, eyes, makeup, and teeth.", EUserInterfaceActionType::RadioButton, FInputChord{});
	UI_COMMAND(BeginSkinTool, "Skin", "Skin, edit the Character’s look through skin parameters, textures, and accents", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginEyesTool, "Eyes", "Eyes, select from presets and customize the look of the Character’s eyes", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginMakeupTool, "Makeup", "Makeup, select from presets and customize the makeup of the face’s regions", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginTextureMaterialOverrideTool, "Texture & Material Overrides", "Texture & Material Overrides, override textures (skin textures only) and materials", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginHeadMaterialsTools, "Teeth & Eyelashes", "Teeth and Eyelashes, select and configure the materials details of teeth and eyelashes", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginHeadMaterialsTeethTool, "Teeth", "Teeth, customize the teeth’ material parameters", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginHeadMaterialsEyelashesTool, "Eyelashes", "Eyelashes, customize the eyelashes’ material parameters", EUserInterfaceActionType::ToggleButton, FInputChord{});

	UI_COMMAND(LoadHairAndClothingTools, "Hair & Clothing", "Select and style your MetaHuman's hairstyle, eyebrows, eyelashes, facial hair and clothing from a library of groom assets.", EUserInterfaceActionType::RadioButton, FInputChord{});
	UI_COMMAND(BeginWardrobeSelectionTool, "Selection", "Selection, Select clothing and hair to accessorize the Character", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginCostumeDetailsTool, "Details", "Details, change the parameters for each selected clothing and groom accessory", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(PrepareAccessory, "Prepare", "Prepare the selected accessories", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(UnprepareAccessory, "Unprepare", "Unprepare the selected accessories", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(WearAcceessory, "Wear", "Wear the selected accessories", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(RemoveAccessory, "Remove", "Remove the selected accessories", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(AccessoryProperties, "Accessory Properties", "Open accessory properties", EUserInterfaceActionType::ToggleButton, FInputChord{});

	UI_COMMAND(LoadPipelineTools, "Assembly", "Review and configure how your MetaHuman's components (head, body, grooms, materials, clothing) are assembled together to be used in Unreal Engine.", EUserInterfaceActionType::RadioButton, FInputChord{});
	UI_COMMAND(BeginPipelineTool, "Edit Assembly", "Assembly Tool", EUserInterfaceActionType::ToggleButton, FInputChord{});

	UI_COMMAND(LoadExportTools, "Export", "Download the MetaHuman assets to your Unreal Engine project or export source files for use in a DCC pipeline.", EUserInterfaceActionType::RadioButton, FInputChord{});
	UI_COMMAND(BeginDCCExportTool, "DCC Export", "Export character data for DCC tools", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginDNAExportTool, "DNA Export", "Export character DNA data", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginGeometryExportTool, "Geometry Export", "Export character geometry data", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BeginMaterialsExportTool, "Materials Export", "Export character materials data", EUserInterfaceActionType::ToggleButton, FInputChord{});
}

void FMetaHumanCharacterEditorToolCommands::GetToolDefaultObjectList(TArray<UInteractiveTool*>& OutToolCDOs)
{
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorPresetsTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorBodyModelTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorHeadAndBodyBlendTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorHeadModelTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorImportFromDNATool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorImportFromIdentityTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorImportFromTemplateTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorFaceMoveTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorFaceSculptTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorSkinTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorEyesTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorMakeupTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorTextureMaterialOverrideTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorWardrobeTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorCostumeTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorDCCExportTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorDNAExportTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorGeometryExportTool>());
	OutToolCDOs.Add(GetMutableDefault<UMetaHumanCharacterEditorMaterialsExportTool>());
}

FMetaHumanCharacterEditorViewportToolbarCommands::FMetaHumanCharacterEditorViewportToolbarCommands()
	: TCommands<FMetaHumanCharacterEditorViewportToolbarCommands>(
		TEXT("MetaHumanCharacterEditorViewportToolbar"),
		LOCTEXT("MetaHumanCharacterEditorViewportToolbarCommandsContext", "MetaHumanh Character Editor Viewport Toolbar commands"),
		NAME_None,
		FMetaHumanCharacterEditorStyle::Get().GetStyleSetName()
	)
{
}

void FMetaHumanCharacterEditorViewportToolbarCommands::RegisterCommands()
{

	UI_COMMAND(FocusFace, "Face", "Focus camera on character face.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(FocusBody, "Body", "Focus camera on character body", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(FocusFar, "Far", "Far out look at the character", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(FocusHands, "Hands", "Focus camera on character hands", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(FocusFeet, "Feet", "Focus camera on character feet", EUserInterfaceActionType::RadioButton, FInputChord());
}

#undef LOCTEXT_NAMESPACE