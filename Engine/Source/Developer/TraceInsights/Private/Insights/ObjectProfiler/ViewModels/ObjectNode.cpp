// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectNode.h"

// TraceInsights
#include "Insights/InsightsStyle.h"
#include "Insights/ObjectProfiler/IAssetInfoProvider.h"
#include "Insights/ObjectProfiler/ObjectProfilerManager.h"
#include "Insights/ObjectProfiler/ViewModels/ObjectTable.h"

#define LOCTEXT_NAMESPACE "UE::Insights::ObjectProfiler::FObjectNode"

namespace UE::Insights::ObjectProfiler
{

INSIGHTS_IMPLEMENT_RTTI(FObjectNode)
INSIGHTS_IMPLEMENT_RTTI(FFieldObjectNode)
INSIGHTS_IMPLEMENT_RTTI(FStructObjectNode)
INSIGHTS_IMPLEMENT_RTTI(FClassObjectNode)
INSIGHTS_IMPLEMENT_RTTI(FFunctionObjectNode)
INSIGHTS_IMPLEMENT_RTTI(FPackageObjectNode)

////////////////////////////////////////////////////////////////////////////////////////////////////
// FObjectNode
////////////////////////////////////////////////////////////////////////////////////////////////////

FObjectNode::FObjectNode(TWeakPtr<FObjectTable> InParentTable, const TraceServices::FObjectInfo& ObjectInfo)
	: FTableTreeNode(InParentTable, int32(ObjectInfo.Id))
	, ObjectId(ObjectInfo.Id)
	, ClassId(ObjectInfo.ClassId)
	, OuterId(ObjectInfo.OuterId)
	, ObjectFlags(ObjectInfo.Flags)
	, ObjectName(ObjectInfo.Name)
	, VersePath(ObjectInfo.VersePath)
	, SystemMemorySize(int64(ObjectInfo.SystemMemoryBytes))
	, VideoMemorySize(int64(ObjectInfo.VideoMemoryBytes))
	, TotalSystemMemorySize(int64(ObjectInfo.TotalSystemMemoryBytes))
	, TotalVideoMemorySize(int64(ObjectInfo.TotalVideoMemoryBytes))
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FObjectNode::FObjectNode(const FObjectNode& Clone)
	: FTableTreeNode(Clone.GetParentTable(), int32(Clone.ObjectId))
	, ObjectId(Clone.ObjectId)
	, ClassId(Clone.ClassId)
	, OuterId(Clone.OuterId)
	, ObjectFlags(Clone.ObjectFlags)
	, ClassName(Clone.ClassName)
	, Class(Clone.Class)
	, Outer(Clone.Outer)
	, ObjectName(Clone.ObjectName)
	, VersePath(Clone.VersePath)
	, ObjectPath(Clone.ObjectPath)
	, SystemMemorySize(Clone.SystemMemorySize)
	, VideoMemorySize(Clone.VideoMemorySize)
	, TotalSystemMemorySize(Clone.TotalSystemMemorySize)
	, TotalVideoMemorySize(Clone.TotalVideoMemorySize)
	, InternalFlags(Clone.InternalFlags)
	, MatchedAsset(Clone.MatchedAsset)
	, MatchedActors(Clone.MatchedActors)
	, bIdentityMasked(Clone.bIdentityMasked)
	, bIsOwnedByCurrentProject(Clone.bIsOwnedByCurrentProject)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FObjectNode::FObjectNode(TWeakPtr<FObjectTable> InParentTable)
	: FTableTreeNode(InParentTable)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FObjectNode::GetDisplayName() const
{
	if (bIdentityMasked)
	{
		return NSLOCTEXT("UE::Insights::ObjectProfiler", "MaskedFortniteAsset", "Fortnite Asset");
	}
	return FText::FromString(ObjectName);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FObjectNode::GetExtraDisplayName() const
{
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FObjectNode::HasExtraDisplayName() const
{
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FObjectNode::GetTooltipText() const
{
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* FObjectNode::GetIcon() const
{
	return FInsightsStyle::GetBrush("Icons.UObject.TreeItem");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FObjectNode::GetIconColor() const
{
	return GetDefaultColor(IsGroup());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FObjectNode::GetColor() const
{
	if (ObjectId == InvalidObjectId)
	{
		return GetDefaultIconColor(true);
	}
	else if (IsMissing())
	{
		return FLinearColor(1.0f, 0.7f, 0.7f, 1.0f);
	}
	else
	{
		return GetDefaultIconColor(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FObjectNode> FObjectNode::GetPackage() const
{
	TSharedPtr<FObjectNode> Package = GetOuter();
	if (Package.IsValid())
	{
		return Package->IsPackage() ? Package : Package->GetPackage();
	}
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FObjectNode::InitObjectPath()
{
	if (!ObjectPath.IsEmpty())
	{
		return;
	}

	auto GetResolvedObjectPath = [](const FObjectNode* Node)
		{
			return Node->GetSourcePackageName() ? Node->GetSourcePackageName() : Node->GetObjectName();
		};

	TSharedPtr<FObjectNode> ObjectOuter = GetOuter();

	// Fast path: Current object is a package
	if (!ObjectOuter)
	{
		// check(Is<FPackageObjectNode>());
		ObjectPath = GetResolvedObjectPath(this);
		return;
	}

	// Fast path: Current object is a top-level asset
	if (!ObjectOuter->GetOuter())
	{
		// check(ObjectOuter->Is<FPackageObjectNode>());
		TStringBuilder<1024> Path;
		Path.Append(GetResolvedObjectPath(ObjectOuter.Get()));
		Path.AppendChar(TEXT('.'));
		Path.Append(GetObjectName());
		ObjectPath = Path.ToView();
		return;
	}

	// Slow path: walk the outer chain and construct the soft object path parts
	TArray<const TCHAR*, TInlineAllocator<64>> SubObjectPath;

	const FObjectNode* CurrentObject = this;
	const FObjectNode* CurrentObjectOuter = ObjectOuter.Get();

	while (CurrentObjectOuter->GetOuter())
	{
		SubObjectPath.Push(CurrentObject->GetObjectName());
		CurrentObject = CurrentObjectOuter;
		CurrentObjectOuter = CurrentObject->GetOuter().Get();
	}

	TStringBuilder<1024> Path;
	Path.Append(GetResolvedObjectPath(CurrentObjectOuter));
	Path.AppendChar(TEXT('.'));
	Path.Append(CurrentObject->GetObjectName());
	Path.AppendChar(TEXT(':'));
	while (!SubObjectPath.IsEmpty())
	{
		const TCHAR* ObjName = SubObjectPath.Pop(EAllowShrinking::No);
		Path.Append(ObjName);
		Path.AppendChar(TEXT('.'));
	}
	Path.RemoveSuffix(1);
	ObjectPath = Path.ToView();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FObjectNode::ConvertObjectPath(const TSharedPtr<IAssetInfoProvider>& Provider)
{
	Provider->ConvertRuntimePathToEditorPath(ObjectPath);
}

// static
int64 FObjectNode::GetEstimatedMemorySize(const FBaseTreeNode& Node)
{
	if (Node.Is<FObjectNode>())
	{
		return Node.As<FObjectNode>().GetEstimatedMemorySize();
	}

	if (Node.IsGroup() && Node.Is<FTableTreeNode>())
	{
		const FTableTreeNode& TableNode = Node.As<FTableTreeNode>();
		if (TableNode.HasAggregatedValue(FObjectTableColumns::EstimatedSizeColumnId))
		{
			const FTableCellValue& Size = TableNode.GetAggregatedValue(FObjectTableColumns::EstimatedSizeColumnId);
			if (Size.DataType == ETableCellDataType::Int64)
			{
				return Size.Int64;
			}
		}
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

// static
int64 FObjectNode::GetTotalEstimatedMemorySize(const FBaseTreeNode& Node)
{
	if (Node.Is<FObjectNode>())
	{
		return Node.As<FObjectNode>().GetTotalEstimatedMemorySize();
	}

	if (Node.IsGroup() && Node.Is<FTableTreeNode>())
	{
		const FTableTreeNode& TableNode = Node.As<FTableTreeNode>();
		if (TableNode.HasAggregatedValue(FObjectTableColumns::TotalEstimatedSizeColumnId))
		{
			const FTableCellValue& Size = TableNode.GetAggregatedValue(FObjectTableColumns::TotalEstimatedSizeColumnId);
			if (Size.DataType == ETableCellDataType::Int64)
			{
				return Size.Int64;
			}
		}
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

// static
TOptional<double> FObjectNode::GetEstimatedMemoryImpact(const FBaseTreeNode& Node)
{
	const int64 EstimatedSize = FObjectNode::GetEstimatedMemorySize(Node);
	if (EstimatedSize > 0)
	{
		TSharedPtr<FBaseTreeNode> ParentNode = Node.GetParent();
		if (ParentNode.IsValid())
		{
			// GetRoot
			while (true)
			{
				TSharedPtr<FBaseTreeNode> ParentParentNode = ParentNode->GetParent();
				if (!ParentParentNode.IsValid())
				{
					break;
				}
				ParentNode = ParentParentNode;
			}

			const int64 ParentEstimatedMemSize = FObjectNode::GetEstimatedMemorySize(*ParentNode);
			if (ParentEstimatedMemSize > 0)
			{
				const double Ratio = double(EstimatedSize) / double(ParentEstimatedMemSize);
				return Ratio;
			}
		}
	}
	return TOptional<double>();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double FObjectNode::GetEstimatedMemoryImpact() const
{
	const int64 EstimatedMemSize = GetEstimatedMemorySize();
	if (EstimatedMemSize > 0)
	{
		TSharedPtr<FBaseTreeNode> ParentNode = GetParent();
		if (ParentNode.IsValid())
		{
			// GetRoot
			while (true)
			{
				TSharedPtr<FBaseTreeNode> ParentParentNode = ParentNode->GetParent();
				if (!ParentParentNode.IsValid())
				{
					break;
				}
				ParentNode = ParentParentNode;
			}

			const int64 ParentEstimatedMemSize = FObjectNode::GetEstimatedMemorySize(*ParentNode);
			if (ParentEstimatedMemSize > 0)
			{
				return double(EstimatedMemSize) / double(ParentEstimatedMemSize);
			}
		}
	}
	return 0.0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

// static
TOptional<double> FObjectNode::GetTotalEstimatedMemoryImpact(const FBaseTreeNode& Node)
{
	const int64 EstimatedSize = FObjectNode::GetTotalEstimatedMemorySize(Node);
	if (EstimatedSize > 0)
	{
		TSharedPtr<FBaseTreeNode> ParentNode = Node.GetParent();
		if (ParentNode.IsValid())
		{
			// GetRoot
			while (true)
			{
				TSharedPtr<FBaseTreeNode> ParentParentNode = ParentNode->GetParent();
				if (!ParentParentNode.IsValid())
				{
					break;
				}
				ParentNode = ParentParentNode;
			}

			const int64 ParentEstimatedMemSize = FObjectNode::GetTotalEstimatedMemorySize(*ParentNode);
			if (ParentEstimatedMemSize > 0)
			{
				const double Ratio = double(EstimatedSize) / double(ParentEstimatedMemSize);
				return Ratio;
			}
		}
	}
	return TOptional<double>();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double FObjectNode::GetTotalEstimatedMemoryImpact() const
{
	const int64 EstimatedMemSize = GetTotalEstimatedMemorySize();
	if (EstimatedMemSize > 0)
	{
		TSharedPtr<FBaseTreeNode> ParentNode = GetParent();
		if (ParentNode.IsValid())
		{
			// GetRoot
			while (true)
			{
				TSharedPtr<FBaseTreeNode> ParentParentNode = ParentNode->GetParent();
				if (!ParentParentNode.IsValid())
				{
					break;
				}
				ParentNode = ParentParentNode;
			}

			const int64 ParentEstimatedMemSize = FObjectNode::GetTotalEstimatedMemorySize(*ParentNode);
			if (ParentEstimatedMemSize > 0)
			{
				return double(EstimatedMemSize) / double(ParentEstimatedMemSize);
			}
		}
	}
	return 0.0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

// static
uint32 FObjectNode::GetNumReferences(const FBaseTreeNode& Node)
{
	if (Node.Is<FObjectNode>())
	{
		return Node.As<FObjectNode>().GetNumReferences();
	}

	if (Node.IsGroup() && Node.Is<FTableTreeNode>())
	{
		const FTableTreeNode& TableNode = Node.As<FTableTreeNode>();
		if (TableNode.HasAggregatedValue(FObjectTableColumns::ReferencesColumnId))
		{
			const FTableCellValue& NumRefs = TableNode.GetAggregatedValue(FObjectTableColumns::ReferencesColumnId);
			if (NumRefs.DataType == ETableCellDataType::Int64)
			{
				return uint32(NumRefs.Int64);
			}
		}
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAssetData FObjectNode::MakeAssetData()
{
	FString LongPackageName;
	TSharedPtr<FObjectNode> Package = GetPackage();
	if (Package.IsValid())
	{
		LongPackageName = Package->GetObjectPath();
	}

	FName AssetClassPackageName;
	FName AssetClassName;
	TSharedPtr<FObjectNode> ClassObject = GetClass();
	if (ClassObject.IsValid())
	{
		TSharedPtr<FObjectNode> ClassPackage = ClassObject->GetPackage();
		if (ClassPackage)
		{
			AssetClassPackageName = ClassPackage->GetObjectName();
		}
		AssetClassName = ClassObject->GetObjectName();
	}
	FTopLevelAssetPath AssetClassPathName{ AssetClassPackageName , AssetClassName };

	return FAssetData(LongPackageName, ObjectPath, AssetClassPathName);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

//static
TSharedRef<FObjectNode> FObjectNode::Clone(FObjectNode& InNode)
{
	if (InNode.Is<FPackageObjectNode>())
	{
		return MakeShared<FPackageObjectNode>(InNode.As<FPackageObjectNode>());
	}
	if (InNode.Is<FFunctionObjectNode>())
	{
		return MakeShared<FFunctionObjectNode>(InNode.As<FFunctionObjectNode>());
	}
	if (InNode.Is<FClassObjectNode>())
	{
		return MakeShared<FClassObjectNode>(InNode.As<FClassObjectNode>());
	}
	if (InNode.Is<FStructObjectNode>())
	{
		return MakeShared<FStructObjectNode>(InNode.As<FStructObjectNode>());
	}
	if (InNode.Is<FFieldObjectNode>())
	{
		return MakeShared<FFieldObjectNode>(InNode.As<FFieldObjectNode>());
	}
	return MakeShared<FObjectNode>(InNode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FFieldObjectNode
////////////////////////////////////////////////////////////////////////////////////////////////////

FFieldObjectNode::FFieldObjectNode(TWeakPtr<FObjectTable> InParentTable, const TraceServices::FObjectInfo& ObjectInfo)
	: FObjectNode(InParentTable, ObjectInfo)
{
	ensure(EnumHasAnyFlags(ObjectInfo.FlagsEx, TraceServices::EObjectInfoFlags::IsField));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FFieldObjectNode::FFieldObjectNode(const FFieldObjectNode& Clone)
	: FObjectNode(Clone)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* FFieldObjectNode::GetIcon() const
{
	return FInsightsStyle::GetBrush("Icons.UField.TreeItem");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FFieldObjectNode::GetIconColor() const
{
	return FLinearColor(0.7f, 1.0f, 1.0f, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FStructObjectNode
////////////////////////////////////////////////////////////////////////////////////////////////////

FStructObjectNode::FStructObjectNode(TWeakPtr<FObjectTable> InParentTable, const TraceServices::FObjectInfo& ObjectInfo)
	: FFieldObjectNode(InParentTable, ObjectInfo)
	, SuperId(ObjectInfo.SuperId)
	, InheritanceSuperId(ObjectInfo.InheritanceSuperId)
	, StructureSize(ObjectInfo.StructureSize)
{
	ensure(EnumHasAnyFlags(ObjectInfo.FlagsEx, TraceServices::EObjectInfoFlags::IsStruct));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FStructObjectNode::FStructObjectNode(const FStructObjectNode& Clone)
	: FFieldObjectNode(Clone)
	, SuperId(Clone.SuperId)
	, InheritanceSuperId(Clone.InheritanceSuperId)
	, StructureSize(Clone.StructureSize)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* FStructObjectNode::GetIcon() const
{
	return FInsightsStyle::GetBrush("Icons.UStruct.TreeItem");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FStructObjectNode::GetIconColor() const
{
	return FLinearColor(0.6f, 1.0f, 1.0f, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FClassObjectNode
////////////////////////////////////////////////////////////////////////////////////////////////////

FClassObjectNode::FClassObjectNode(TWeakPtr<FObjectTable> InParentTable, const TraceServices::FObjectInfo& ObjectInfo)
	: FStructObjectNode(InParentTable, ObjectInfo)
{
	ensure(EnumHasAnyFlags(ObjectInfo.FlagsEx, TraceServices::EObjectInfoFlags::IsClass));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FClassObjectNode::FClassObjectNode(const FClassObjectNode& Clone)
	: FStructObjectNode(Clone)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* FClassObjectNode::GetIcon() const
{
	return FInsightsStyle::GetBrush("Icons.UClass.TreeItem");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FClassObjectNode::GetIconColor() const
{
	return FLinearColor(0.5f, 1.0f, 1.0f, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FFunctionObjectNode
////////////////////////////////////////////////////////////////////////////////////////////////////

FFunctionObjectNode::FFunctionObjectNode(TWeakPtr<FObjectTable> InParentTable, const TraceServices::FObjectInfo& ObjectInfo)
	: FStructObjectNode(InParentTable, ObjectInfo)
	, FunctionFlags(ObjectInfo.FunctionFlags)
	, FunctionNumParms(ObjectInfo.FunctionNumParms)
	, FunctionParmsSize(ObjectInfo.FunctionParmsSize)
{
	ensure(EnumHasAnyFlags(ObjectInfo.FlagsEx, TraceServices::EObjectInfoFlags::IsFunction));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FFunctionObjectNode::FFunctionObjectNode(const FFunctionObjectNode& Clone)
	: FStructObjectNode(Clone)
	, FunctionFlags(Clone.FunctionFlags)
	, FunctionNumParms(Clone.FunctionNumParms)
	, FunctionParmsSize(Clone.FunctionParmsSize)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* FFunctionObjectNode::GetIcon() const
{
	return FInsightsStyle::GetBrush("Icons.UFunction.TreeItem");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FFunctionObjectNode::GetIconColor() const
{
	return FLinearColor(0.7f, 0.7f, 1.0f, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FPackageObjectNode
////////////////////////////////////////////////////////////////////////////////////////////////////

FPackageObjectNode::FPackageObjectNode(TWeakPtr<FObjectTable> InParentTable, const TraceServices::FObjectInfo& ObjectInfo)
	: FObjectNode(InParentTable, ObjectInfo)
	, PackageId(ObjectInfo.PackageId)
	, Path(ObjectInfo.PackagePath)
	, SourcePackageName(ObjectInfo.SourcePackageName)
{
	ensure(EnumHasAnyFlags(ObjectInfo.FlagsEx, TraceServices::EObjectInfoFlags::IsPackage));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FPackageObjectNode::FPackageObjectNode(const FPackageObjectNode& Clone)
	: FObjectNode(Clone)
	, PackageId(Clone.PackageId)
	, Path(Clone.Path)
	, SourcePackageName(Clone.SourcePackageName)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* FPackageObjectNode::GetIcon() const
{
	return FInsightsStyle::GetBrush("Icons.UPackage.TreeItem");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FPackageObjectNode::GetIconColor() const
{
	return FLinearColor(1.0f, 1.0f, 0.5f, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::ObjectProfiler

#undef LOCTEXT_NAMESPACE
