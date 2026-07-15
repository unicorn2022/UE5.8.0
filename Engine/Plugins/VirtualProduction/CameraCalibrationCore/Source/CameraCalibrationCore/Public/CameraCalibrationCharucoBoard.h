// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "GameFramework/Actor.h"

#include "CameraCalibrationCharucoBoard.generated.h"

#define UE_API CAMERACALIBRATIONCORE_API

class UCalibrationPointComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UTexture2D;


/* This avoids direct dependency on OpenCV which currently needs to be avoided in UEFN */
UENUM(BlueprintType)
enum class EArucoDictionaryShadow : uint8
{
	None                 UMETA(DisplayName = "None"),
	DICT_4X4_50          UMETA(DisplayName = "DICT_4X4_50"),
	DICT_4X4_100         UMETA(DisplayName = "DICT_4X4_100"),
	DICT_4X4_250         UMETA(DisplayName = "DICT_4X4_250"),
	DICT_4X4_1000        UMETA(DisplayName = "DICT_4X4_1000"),
	DICT_5X5_50          UMETA(DisplayName = "DICT_5X5_50"),
	DICT_5X5_100         UMETA(DisplayName = "DICT_5X5_100"),
	DICT_5X5_250         UMETA(DisplayName = "DICT_5X5_250"),
	DICT_5X5_1000        UMETA(DisplayName = "DICT_5X5_1000"),
	DICT_6X6_50          UMETA(DisplayName = "DICT_6X6_50"),
	DICT_6X6_100         UMETA(DisplayName = "DICT_6X6_100"),
	DICT_6X6_250         UMETA(DisplayName = "DICT_6X6_250"),
	DICT_6X6_1000        UMETA(DisplayName = "DICT_6X6_1000"),
	DICT_7X7_50          UMETA(DisplayName = "DICT_7X7_50"),
	DICT_7X7_100         UMETA(DisplayName = "DICT_7X7_100"),
	DICT_7X7_250         UMETA(DisplayName = "DICT_7X7_250"),
	DICT_7X7_1000        UMETA(DisplayName = "DICT_7X7_1000"),
	DICT_ARUCO_ORIGINAL  UMETA(DisplayName = "DICT_ARUCO_ORIGINAL")
};

/** Shadow structure representing a Charuco board configuration (avoids OpenCV dependency) */
USTRUCT(BlueprintType)
struct FCharucoBoardConfigShadow
{
	GENERATED_BODY()

	/** Number of chessboard squares in X direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration")
	int32 SquaresX = 5;
	
	/** Number of chessboard squares in Y direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration")
	int32 SquaresY = 7;
	
	/** Size of each square in the board (in cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration")
	float SquareSize = 4.0f;
	
	/** Size of the ArUco markers (in cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration")
	float MarkerSize = 3.0f;
	
	/** ArUco dictionary to use for the markers */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration")
	EArucoDictionaryShadow Dictionary = EArucoDictionaryShadow::DICT_4X4_50;
};

/**
 * Dynamic Charuco board actor for camera calibration
 */
UCLASS(MinimalAPI)
class ACameraCalibrationCharucoBoard : public AActor
{
	GENERATED_BODY()

public:
	UE_API ACameraCalibrationCharucoBoard();

public:
	/** Rebuilds the board mesh and generates the Charuco pattern texture */
	UFUNCTION(BlueprintCallable, Category = "Calibration")
	UE_API void Rebuild();

	/** Get the generated Charuco board texture */
	UFUNCTION(BlueprintCallable, Category = "Calibration")
	UTexture2D* GetCharucoBoardTexture() const { return CharucoBoardTexture; }

	/** Get the total number of corners in the Charuco board */
	UFUNCTION(BlueprintCallable, Category = "Calibration")
	UE_API int32 GetNumCorners() const;

	/** Get the 3D position of a specific corner by its ID */
	UFUNCTION(BlueprintCallable, Category = "Calibration")
	UE_API FVector GetCornerPosition(int32 CornerId) const;

	//~ Begin UObject Interface
	UE_API virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface

private:
	/** Creates the board mesh */
	void CreateBoardMesh();
	
	/** Generates the Charuco pattern texture */
	void GenerateCharucoTexture();
	
	/** Positions the calibration point components */
	void UpdateCalibrationPointPositions();
	
	/** Clears generated mesh component */
	void ClearGeneratedMeshComponent();

public:
	/** Root component, gives the Actor a transform */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	TObjectPtr<USceneComponent> Root;

	/** TopLeft calibration point */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	TObjectPtr<UCalibrationPointComponent> TopLeft;

	/** TopRight calibration point */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	TObjectPtr<UCalibrationPointComponent> TopRight;

	/** BottomLeft calibration point */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	TObjectPtr<UCalibrationPointComponent> BottomLeft;

	/** BottomRight calibration point */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	TObjectPtr<UCalibrationPointComponent> BottomRight;

	/** Center calibration point */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	TObjectPtr<UCalibrationPointComponent> Center;

	/** Number of inner corner rows (same convention as checkerboard) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration", meta = (ClampMin = "2", ClampMax = "63"))
	int32 NumCornerRows = 5;

	/** Number of inner corner columns (same convention as checkerboard) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration", meta = (ClampMin = "2", ClampMax = "63"))
	int32 NumCornerCols = 7;

	/** Length of the side of each square (in cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration", meta = (ClampMin = "1.0", ClampMax = "100.0"))
	float SquareSideLength = 4.0f;

	/** Size of the ArUco markers relative to square size (0.0 - 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float MarkerSizeRatio = 0.75f;

	/** ArUco dictionary to use for markers */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration")
	EArucoDictionaryShadow ArucoDictionary;

	/** Thickness of the board (not used for calibration) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration", meta = (ClampMin = "0.01", ClampMax = "10.0"))
	float Thickness = 0.1f;

	/** The static mesh to use for the board */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	TObjectPtr<UStaticMesh> BoardMesh;

	/** The material to apply the Charuco pattern to */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	TObjectPtr<UMaterialInterface> BoardMaterial;

	/** Maximum dimension of the generated texture. The actual resolution will maintain the board's aspect ratio. */
	UPROPERTY(EditAnywhere, Category = "Calibration", meta = (ClampMin = "256", ClampMax = "4096", UIMin = "256", UIMax = "4096"))
	int32 MaxTextureResolution = 2048;

private:
	/** Generated texture containing the Charuco pattern */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CharucoBoardTexture;

	/** Mesh component for the board */
	UPROPERTY(VisibleAnywhere, Category = "Calibration")
	TObjectPtr<UStaticMeshComponent> BoardMeshComponent;

	/** Dynamic material instance for displaying the pattern */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicBoardMaterial;
};

#undef UE_API
