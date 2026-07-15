// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/VCamConnectionStructs.h"

#include "VCamComponent.h"

#include "Algo/AllOf.h"
#include "InputAction.h"

DEFINE_LOG_CATEGORY(LogVCamConnection);

bool FVCamConnection::IsConnected() const
{
	// If we have a valid modifier then we can assume we have a successful connection
	return IsValid(ConnectedModifier);
}

bool FVCamConnection::AttemptConnection(UVCamComponent* VCamComponent)
{
	UVCamModifier* Modifier = VCamComponent->GetModifierByName(ConnectionTargetSettings.TargetModifierName);
	
	UE_CLOGF(!Modifier, LogVCamConnection, Warning, "Connection Failed: Failed to find Modifier with name %ls in VCam Component", *ConnectionTargetSettings.TargetModifierName.ToString());
	constexpr bool bLogWarnings = true;
	if (Modifier && IsConnectionValid(*Modifier, ConnectionTargetSettings.TargetConnectionPoint, bLogWarnings))
	{
		ConnectedModifier = Modifier;
		ConnectedAction = Modifier->ConnectionPoints[ConnectionTargetSettings.TargetConnectionPoint].AssociatedAction;
		return true;
	}

	return false;
}

bool FVCamConnection::IsConnectionValid(UVCamModifier& Modifier, FName ConnectionPointName, bool bLogWarnings) const
{
	const bool bImplementsAllRequiredInterfaces = Algo::AllOf(RequiredInterfaces, [&Modifier](const TSubclassOf<UInterface>& Interface)
	{
		return !Interface.Get() || Modifier.GetClass()->ImplementsInterface(Interface);
	});
	if (!bImplementsAllRequiredInterfaces)
	{
		UE_CLOGF(bLogWarnings, LogVCamConnection, Warning, "Connection Failed: Modifier with name %ls does not implement all required interfaces for this connection.", *ConnectionTargetSettings.TargetModifierName.ToString());
		return false;
	}
	
	FVCamModifierConnectionPoint* ConnectionPoint = Modifier.ConnectionPoints.Find(ConnectionPointName);
	if (!ConnectionPoint)
	{
		UE_CLOGF(bLogWarnings, LogVCamConnection, Warning, "Connection Failed: Failed to find Connection Point with name %ls in Modifier %ls",
			*ConnectionTargetSettings.TargetConnectionPoint.ToString(),
			*ConnectionTargetSettings.TargetModifierName.ToString()
			);
		return false;
	}
	
	// The Connection Point is considered valid if either we don't require an action or the required ActionType matches the ActionType inside the Connection Point
	const bool bConnectionPointHasRequiredActionType = !bRequiresInputAction || (ConnectionPoint->AssociatedAction && ConnectionPoint->AssociatedAction->ValueType == ActionType);
	if (!bConnectionPointHasRequiredActionType)
	{
		UE_CLOGF(bLogWarnings, LogVCamConnection, Warning, "Connection Failed: Connection Point with name %ls does not provide an action of the required type. Expected %ls but found %ls",
			*ConnectionTargetSettings.TargetConnectionPoint.ToString(),
			*UEnum::GetValueAsString(ActionType),
			ConnectionPoint->AssociatedAction ? *UEnum::GetValueAsString(ConnectionPoint->AssociatedAction->ValueType) : TEXT("None")
			);
		return false;
	}
	
	return true;
}

void FVCamConnection::ResetConnection()
{
	ConnectedModifier = nullptr;
	ConnectedAction = nullptr;
}
