// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Future.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Templates/ValueOrError.h"

class IWebSocket;

namespace OBS
{
	/** A scene in OBS. */
	struct FScene
	{
		/** Unique name of the scene. */
		FString Name;

		/** Unique identifier of the scene. */
		FString UniqueId;

		/**
		 * Decodes a JSON object into an FScene instance.
		 *
		 * @param InJsonObject The JSON object to decode.
		 * @return The decoded FScene instance.
		 */
		static FScene Decode(const TSharedPtr<FJsonObject>& InJsonObject);
	};

	/** An item within a scene in OBS, for example Display Capture. */
	struct FSceneItem
	{
		/** The transform properties of a scene item. Only optional properties set are encoded. */
		struct FSceneItemTransform
		{
			/** Alignment options for the scene item. @see: obs-defs.h */
			enum class EAlignment : uint8
			{
				Center = 0,
				Left = 1 << 0,
				Right = 1 << 1,
				Top = 1 << 2,
				Bottom = 1 << 3,

				TopLeft = Top | Left,
				TopCenter = Top | Center,
				TopRight = Top | Right,
				CenterLeft = Center | Left,
				CenterRight = Center | Right,
				BottomLeft = Bottom | Left,
				BottomCenter = Bottom | Center,
				BottomRight = Bottom | Right
			};

			/** Types of bounds for the scene item. */
			enum class EBoundsType : uint8
			{
				None = 0,
				StretchToBounds = 1,
				ScaleToInnerBounds = 2,
				ScaleToOuterBounds = 3,
				ScaleToWidthOfBounds = 4,
				ScaleToHeightOfBounds = 5,
				MaximumSizeOnly = 6
			};

			/** Alignment of the scene item. */
			TOptional<EAlignment> Alignment;

			/** Position of the scene item (in pixels). */
			TOptional<FVector2D> Position;

			/** Scale of the scene item. */
			TOptional<FVector2D> Scale;

			/** Width of the scene item (in pixels). */
			TOptional<int32> Width;

			/** Height of the scene item (in pixels). */
			TOptional<int32> Height;

			/** Type of bounds applied to the scene item. */
			TOptional<EBoundsType> BoundsType;

			/** Alignment of the bounds. */
			TOptional<EAlignment> BoundsAlignment;

			/** Width of the bounds (in pixels). */
			TOptional<int32> BoundsWidth;

			/** Height of the bounds (in pixels). */
			TOptional<int32> BoundsHeight;

			/**
			 * Encodes the transformation properties into a JSON object.
			 *
			 * @return A shared pointer to the encoded JSON object.
			 */
			TSharedPtr<FJsonObject> Encode() const;

			/**
			 * Decodes a JSON object into an FSceneItemTransform instance.
			 *
			 * @param InJsonObject The JSON object to decode.
			 * @return The decoded FSceneItemTransform instance.
			 */
			static FSceneItemTransform Decode(const TSharedPtr<FJsonObject>& InJsonObject);
		};

		/** The kind of input associated with the scene item. */
		FString InputKind;

		/** Whether the scene item is enabled. */
		bool bIsSceneItemEnabled = true;

		/** Unique identifier for the scene item. */
		int32 SceneItemId = INDEX_NONE;

		/** Index of the scene item within the scene. */
		int32 SceneItemIndex = INDEX_NONE;

		/** Whether the scene item is locked. */
		bool bIsSceneItemLocked = false;
			
		/** Name of the source associated with the scene item. */
		FString SourceName;

		/** Type of the source associated with the scene item. */
		FString SourceType;

		/** Unique identifier for the source. */
		FString SourceUniqueId;

		/** Transformation properties of the scene item. */
		FSceneItemTransform SceneItemTransform;

		/** Arbitrary settings object, if any. */
		TSharedPtr<FJsonObject> Settings = nullptr;

		/**
		 * Decodes a JSON object into an FSceneItem instance.
		 *
		 * @param InJsonObject The JSON object to decode.
		 * @return The decoded FSceneItem instance.
		 */
		static FSceneItem Decode(const TSharedPtr<FJsonObject>& InJsonObject);

		/**
		 * Compares two scene items for equality based on their input kind and source name.
		 *
		 * @param InOther The other scene item to compare.
		 * @return True if the scene items are equal, false otherwise.
		 */
		bool operator==(const FSceneItem& InOther) const
		{
			return InputKind == InOther.InputKind 
				&& SourceName == InOther.SourceName;
		}
	};
	
	ENUM_CLASS_FLAGS(OBS::FSceneItem::FSceneItemTransform::EAlignment)

	/** Used to both request and receive parameters. */
	struct FParameter
	{
		/** Category of the parameter. */
		FString Category;

		/** Name of the parameter. */
		FString Name;

		/** Value of the parameter, if available. */
		TOptional<FString> Value;

		/**
		 * Compares two parameters for equality based on their category and name.
		 *
		 * @param InOther The other parameter to compare.
		 * @return True if the parameters are equal, false otherwise.
		 */
		bool operator==(const FParameter& InOther) const
		{
			return Category == InOther.Category && Name == InOther.Name;
		}
	};	
}
