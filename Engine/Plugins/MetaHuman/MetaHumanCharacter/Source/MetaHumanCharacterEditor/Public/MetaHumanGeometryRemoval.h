// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanGeometryRemovalTypes.h"

#include "Containers/ArrayView.h"
#include "Misc/NotNull.h"
#include "Math/IntVector.h"

struct FImage;
class FText;
class USkeletalMesh;
class UTexture2D;

namespace UE::MetaHuman::GeometryRemoval
{
	/**
	 * Combines the source hidden face maps onto a provided destination map, so that multiple sets 
	 * of hidden faces can be removed in one call.
	 * 
	 * OutDestinationMap will be initialized by this function.
	 * 
	 * Returns false if the inputs are invalid in some way (e.g. zero size source texture) and sets
	 * OutFailureReason to an explanation for the failure.
	 */
	bool METAHUMANCHARACTEREDITOR_API TryCombineHiddenFaceMaps(TConstArrayView<FHiddenFaceMapImage> SourceMaps, FHiddenFaceMapImage& OutDestinationMap, FText& OutFailureReason);

	/**
	 * Converts the provided textures to images and copies the accompanying settings over.
	 * 
	 * Any null textures will be skipped, in which case the resulting image array will be smaller
	 * than the provided texture array.
	 * 
	 * Any failures in retrieving the image data from a non-null texture will cause this function
	 * to fail and return false. OutFailureReason will be set to an explanation for the failure.
	 */
	bool METAHUMANCHARACTEREDITOR_API TryConvertHiddenFaceMapTexturesToImages(TConstArrayView<FHiddenFaceMapTexture> SourceMaps, TArray<FHiddenFaceMapImage>& OutImages, FText& OutFailureReason);

	/**
	 * Copies the image contents into the given texture.
	 * 
	 * Texture only needs to be a valid UTexture2D object. It doesn't need to be initialized 
	 * in any particular way.
	 */
	void METAHUMANCHARACTEREDITOR_API UpdateHiddenFaceMapTextureFromImage(const FImage& Image, TNotNull<UTexture2D*> Texture);

	/**
	 * Removes and shrinks geometry in a skeletal mesh LOD according to the given HiddenFaceMap.
	 * 
	 * This is used to remove geometry that will be hidden, e.g. geometry of a body that is hidden
	 * by the clothing being worn on the body. This is done to avoid wasting performance and memory
	 * on geometry that will never be seen, but also to stop unseen geometry from intersecting with
	 * the geometry in front of it, e.g. the body showing through the clothes due to Z-fighting or 
	 * coarser geometry being used at lower LODs.
	 * 
	 * At the edges of the hidden area, e.g. around the edges of the clothes, there will be 
	 * geometry that is partially visible and therefore can't be removed, but could still intersect
	 * with geometry in front of it. For this reason, this function provides the ability to 
	 * "shrink" geometry by moving it a small distance in the opposite direction of its normal.
	 * 
	 * HiddenFaceMap is an image and some settings that control which geometry will be removed or 
	 * shrunk. For each vertex that's processed, the vertex's UV will be used to look up the image 
	 * to determine what modification to make to the vertex, if any.
	 * 
	 * Note that for backwards compatibility with the previous geometry removal system, all three
	 * color channels are sampled and the highest of the three values is used.
	 * 
	 * MaterialSlotsToProcess is an optional list of names of material slots (which correspond to 
	 * mesh sections) to operate on. If the list is empty, all material slots will be processed.
	 * 
	 * To understand how the hidden face map pixel values are used, see the comments on
	 * FHiddenFaceMapSettings.
	 */
	bool METAHUMANCHARACTEREDITOR_API RemoveAndShrinkGeometry(
		TNotNull<USkeletalMesh*> SkeletalMesh, 
		int32 LODIndex,
		const FHiddenFaceMapImage& HiddenFaceMap,
		TConstArrayView<FName> MaterialSlotsToProcess = TConstArrayView<FName>());

	// --------------------------------------------------------------------------------
	// Low-level utilities shared with other code that needs to remove hidden geometry
	//
	// Sharing this functionality here allows us to have precisely consistent behavior
	// across different code systems.
	// --------------------------------------------------------------------------------

	/**
	 * Samples a hidden face map image at the given pixel coordinates.
	 */
	float METAHUMANCHARACTEREDITOR_API SampleHiddenFaceMap(const FImage& Image, int32 PixelX, int32 PixelY);

	/**
	 * Converts a UV coordinate to pixel coordinates in a hidden face map image.
	 */
	FIntVector2 METAHUMANCHARACTEREDITOR_API GetHiddenFaceMapPixelForUV(const FVector2f& UV, float ResX, float ResY);

	/**
	 * Returns true if the triangle (identified by its three UV coordinates) should be
	 * hidden (fully hidden) according to the hidden face map.
	 *
	 * A triangle is stripped only if every pixel that overlaps it has a value at or below
	 * MaxCullValue. Pixel-triangle overlap is tested precisely, not just by bounding box.
	 *
	 * UVs are wrapped to the 0-1 range internally. Triangles that cross an integer UV
	 * boundary will be handled incorrectly (known limitation).
	 */
	bool METAHUMANCHARACTEREDITOR_API ShouldHideTriangle(
		const FVector2f& UvA,
		const FVector2f& UvB,
		const FVector2f& UvC,
		const FImage& Image,
		float MaxCullValue);

	/**
	 * Computes the shrink weight for a pixel value.
	 *
	 * Returns 0 if the pixel is at or above MinKeepValue (unaffected),
	 * 1 if at or below MaxCullValue (fully hidden / max shrink),
	 * and linearly interpolates in between.
	 */
	float METAHUMANCHARACTEREDITOR_API ComputeShrinkWeight(float PixelValue, float MaxCullValue, float MinKeepValue);

	/**
	 * Returns true if the pixel value indicates the geometry should be culled (fully hidden).
	 */
	inline bool IsPixelCulled(float PixelValue, float MaxCullValue)
	{
		return PixelValue <= MaxCullValue;
	}

} // namespace UE::MetaHuman::GeometryRemoval
