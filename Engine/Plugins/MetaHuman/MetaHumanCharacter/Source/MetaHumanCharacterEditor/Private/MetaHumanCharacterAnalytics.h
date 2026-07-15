// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Misc/NotNull.h"
#include "Misc/Optional.h"
#include "AnalyticsEventAttribute.h"

class UMetaHumanCharacter;
class UMetaHumanCharacterPipeline;
enum class ERequestTextureResolution;

namespace UE::MetaHuman
{
	enum class ERigType;
}

class FToolkitBuilder;
class UMetaHumanCharacterEditorMode;

namespace UE::MetaHuman::Analytics
{
	/**
	 * The user-facing operation that triggered the conform event.
	 *
	 * Describes the button the user clicked, NOT the per-part code path taken
	 * internally. (e.g. Template `ImportMesh` button does "assign-as-is" for body
	 * and a real conform for head, both are reported as Operation=ImportMesh
	 * because that's what the user clicked.)
	 *
	 * Note: ImportJoints is NOT a value here, it has its own event family
	 * (see RecordImportJointsEvent / FImportJointsEventExtras).
	 */
	enum class EConformOperation : uint8
	{
		Conform,         // "Conform" button (DNA, Template)
		ImportMesh,      // "Import Mesh" button (DNA, Identity, Template)
		ImportWholeRig,  // "Import Whole Rig" button (DNA only)
	};

	/**
	 * Which mesh parts the user's click conformed.
	 */
	enum class EConformParts : uint8
	{
		Head,         // head conform only
		Body,         // body conform only
		HeadAndBody,  // head AND body conformed in the same click
	};

	/**
	 * Extra attributes for the Conform analytics event.
	 *
	 * The wire event is always `Editor.MetaHumanCharacter.ToolActivate.Conform`
	 * regardless of source; the source is encoded by the choice of recorder
	 * (`RecordConformFromDnaEvent` / `RecordConformFromIdentityEvent` /
	 * `RecordConformFromTemplateEvent`).
	 *
	 * Emission rules (see RecordConformEvent in the .cpp):
	 *  - Operation, Parts always emitted.
	 *  - bHeadSuccess emitted iff Parts is Head or HeadAndBody.
	 *  - bBodySuccess emitted iff Parts is Body or HeadAndBody.
	 *  - bImportEyes / bImportTeeth emitted only when explicitly set (Template path).
	 *  - ExtraAttributes appended verbatim use for non-universal additions
	 */
	struct FConformEventExtras
	{
		/** Which user-facing button triggered this event. */
		EConformOperation Operation = EConformOperation::Conform;

		/** Which parts of the mesh the click conformed. */
		EConformParts Parts = EConformParts::Head;

		/**
		 * Whether the head/face conform succeeded. Set iff `Parts` is Head or HeadAndBody.
		 * Default is unset, defensive, since a forgotten field should NOT silently
		 * report success.
		 */
		TOptional<bool> bHeadSuccess;

		/**
		 * Whether the body conform succeeded. Set iff `Parts` is Body or HeadAndBody.
		 * Default is unset.
		 */
		TOptional<bool> bBodySuccess;

		/** Template-only: whether eye meshes were imported as part of this conform. */
		TOptional<bool> bImportEyes;

		/** Template-only: whether the teeth mesh was imported as part of this conform. */
		TOptional<bool> bImportTeeth;

		/**
		 * Future-extension bag — non-universal attributes appended verbatim.
		 */
		TArray<FAnalyticsEventAttribute> ExtraAttributes;
	};

	/**
	 * Optional extra attributes for the ImportJoints analytics event.
	 *
	 * ImportJoints patches joint transforms only it does not run the conform
	 * solver so it has its own event family separate from Conform. This keeps
	 * conform success-rate dashboards honest.
	 */
	struct FImportJointsEventExtras
	{
		/** Source label: "DNA" or "Template". */
		FString Source = TEXT("DNA");

		bool bSuccess = false;
	};

	/**
	 * Optional extra attributes for the MeshConform (M2MH / "From Custom Mesh") events.
	 *
	 * The MeshImport tool's three click paths (AutoSolve, Face Solve Step, Body Solve Step)
	 * all funnel through `StartMeshConform`, then `ConformTargetMeshesAsync` (worker thread).
	 * This event family is separate from `ToolActivate.Conform` because:
	 *   - Different solver path (M2MH ICP / NRR fitting, not the Import-From-X conform).
	 *   - Different user gesture (provide an arbitrary input mesh, not a DNA/Identity/Template).
	 *
	 * `bSuccess` here means "the async conform task was queued successfully",
	 * NOT "the conform converged". Convergence isn't observable from the click site.
	 * A future revision could hook the task-completion delegate for a real success bit.
	 */
	struct FMeshConformEventExtras
	{
		/**
		 * Which of the three buttons fired this event:
		 *   "Auto"    
		 *   "FaceOnly" 
		 *   "BodyOnly" 
		 */
		FString Step = TEXT("Auto");

		/**
		 * The user's target-parts selection on the MeshImport tool:
		 *   "Combined" | "BodyOnly" | "HeadOnly" | "HeadAndBody"
		 * (Mirror of `ETargetPartsType`.)
		 */
		FString TargetPartsType = TEXT("Combined");

		bool bSuccess = false;

		/** Whether the user populated any keypoint correspondences before clicking. */
		bool bHasKeyPoints = false;
	};

	void RecordNewCharacterEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter);
	void RecordOpenCharacterEditorEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter);
	void RecordCloseCharacterEditorEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter);
	void RecordBuildPipelineCharacterEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const TSubclassOf<UMetaHumanCharacterPipeline> InMaybePipeline);
	void RecordRequestAutorigEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, ERigType RigType);
	void RecordRemoveFaceRigEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter);
	void RecordRequestHighResolutionTexturesEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, ERequestTextureResolution RequestTextureResolution);
	void RecordSaveFaceDNAEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter);
	void RecordSaveBodyDNAEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter);
	void RecordSaveHighResolutionTexturesEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter);
	void RecordCreateMeshFromDNAEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter);
	void RecordWardrobeItemPreparedEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FName& ItemSlotName, const FName& ItemAssetName);
	void RecordWardrobeItemWornEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FName& ItemSlotName, const FName& ItemAssetName);
	void RecordImportFaceDNAEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter);
	void RecordImportBodyDNAEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter);
	void RecordOnToolStartEvent(UMetaHumanCharacterEditorMode* MetaHumanCharacterEditorMode, FToolkitBuilder* ToolkitBuilder, const FString& ToolName);
	/**
	 * Recorders for the unified Conform event family.
	 *
	 * Each call emits exactly one `Editor.MetaHumanCharacter.ToolActivate.Conform`
	 * row, carrying `Source` (encoded by the recorder), `Operation`, `Parts`,
	 * per-part success bits, and any template-only/extra attrs.
	 */
	void RecordConformFromDnaEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FConformEventExtras& InExtras = {});
	void RecordConformFromIdentityEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FConformEventExtras& InExtras = {});
	void RecordConformFromTemplateEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FConformEventExtras& InExtras = {});

	void RecordImportJointsEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FImportJointsEventExtras& InExtras = {});
	void RecordMeshConformEvent(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FMeshConformEventExtras& InExtras = {});
}