// Copyright Epic Games, Inc.All Rights Reserved.

#include "PaletteEditor/MetaHumanCharacterPaletteEditorViewportClient.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "PreviewScene.h"

FMetaHumanCharacterPaletteViewportClient::FMetaHumanCharacterPaletteViewportClient(FEditorModeTools* InModeTools, FPreviewScene* InPreviewScene)
	: FEditorViewportClient{ InModeTools, InPreviewScene }
{
	// The real time override is required to make sure the world ticks while the viewport is not active
	// or this requires the user to interact with the viewport to get up to date lighting and textures
	AddRealtimeOverride(true, NSLOCTEXT("FMetaHumanCharacterPaletteViewportClient", "RealTimeOverride", "Real-time Override"));
	SetRealtime(true);
}

void FMetaHumanCharacterPaletteViewportClient::Tick(float InDeltaSeconds)
{
	FEditorViewportClient::Tick(InDeltaSeconds);

	if (!GIntraFrameDebuggingGameThread && GetPreviewScene() != nullptr)
	{
		GetPreviewScene()->GetWorld()->Tick(LEVELTICK_All, InDeltaSeconds);
	}
}

void FMetaHumanCharacterPaletteViewportClient::Draw(FViewport* InViewport, FCanvas* InCanvas)
{
	FEditorViewportClient::Draw(InViewport, InCanvas);

	// This is temp UI that will most likely be moved out of the viewport to a status bar or similar

	if (!OverlayText.IsEmpty())
	{
		const int32 X = InViewport->GetSizeXY().X / InCanvas->GetDPIScale() - 210;
		const int32 Y = 20;

		FCanvasTextItem TextItem(FVector2D(X, Y), OverlayText, GEngine->GetLargeFont(), FLinearColor::Gray);
		InCanvas->DrawItem(TextItem);
	}
}
