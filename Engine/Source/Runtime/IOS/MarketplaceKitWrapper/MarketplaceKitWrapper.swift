// Copyright Epic Games, Inc. All Rights Reserved.

import Foundation
#if canImport(MarketplaceKit)
import MarketplaceKit
#endif
import os

@objc
enum AppDistributorType : Int
{
	case AppStore
	case TestFlight
	case Marketplace
	case Web
	case Other
	case NotAvailable
}

@objcMembers class AppDistributorWrapper : NSObject
{
	public static func getCurrent(CompletionHandler: @escaping (AppDistributorType, String) -> Void)
	{
		Task
		{
#if canImport(MarketplaceKit)
			let (Distributor, Name) = if #available(iOS 17.4, *)
			{
				try await AppDistributor.current.distributorType
			}
			else
			{
				(AppDistributorType.NotAvailable, String())
			}
#else
			let (Distributor, Name) = (AppDistributorType.NotAvailable, String());
#endif

			DispatchQueue.main.async
			{
				CompletionHandler(Distributor, Name)
			}
		}
	}
	
	private static let log = Logger(subsystem: "com.epicgames.FortniteGame", category: "MarketplaceKitWrapper")

	public static func requestCTToken(_ completionHandler: @escaping (Bool, String) -> Void)
	{
		Task
		{
#if canImport(MarketplaceKit)
			
			let result : (Bool, String)
			
			if #available(iOS 26.0, *)
			{
				do 
				{
					// Retrieve a token for a possible purchase.
					let token = try await TransactionReporting.token(for: .coreTechnology)
					result = (true, token)
				} 
				catch let error as MarketplaceKitError 
				{
					log.error("MarketplaceKit error retrieving CT Token: \(error.localizedDescription, privacy: .public)")
					result = (false, error.localizedDescription)
				}
				catch
				{ 
					log.error("Unexpected error retrieving CT Token: \(error, privacy: .public)")
					result = (false, String(describing: error))
				}
			}
			else
			{
				result = (false, "IOS 26.0 not available")
			}
			
			DispatchQueue.main.async 
			{
				completionHandler(result.0, result.1)
			}
#else
			DispatchQueue.main.async 
			{
				completionHandler(false, "MarketplaceKit not imported")
			}
#endif
		}
	}
	
	public static func getEligibilityRegion(_ completionHandler: @escaping (Bool, String) -> Void)
	{
		Task
		{
#if canImport(MarketplaceKit)			
			let result : (Bool, String)
//#if swift(>=6.3) NOTE: we want this compile to fail if not using Xcode26.4+
			if #available(iOS 26.4, *)
			{
				// Get the eligibility region
				let optionalRegion = await AppDistributor.eligibilityRegion
				if let region = optionalRegion
				{
					result = (true, region)
				}
				else
				{
					result = (false, "Unable to obtain region");
				}
			}
			else
			{
				result = (false, "IOS 26.4 not available")
			}
//#else
//			result = (false, "Swift 6.3+ not available")
//#endif  swift(>=6.3)
			DispatchQueue.main.async 
			{
				completionHandler(result.0, result.1)
			}
#else // canImport(MarketplaceKit)
			DispatchQueue.main.async 
			{
				completionHandler(false, "MarketplaceKit not imported")
			}
#endif // canImport(MarketplaceKit)
		}
	}
}

// MARK: - C-exported entry points for dlopen/dlsym

public typealias MKW_GetCurrentCallback = @convention(c) (Int, UnsafePointer<CChar>?) -> Void
public typealias MKW_BoolStringCallback = @convention(c) (Bool, UnsafePointer<CChar>?) -> Void

@_cdecl("MKW_GetCurrent")
public func MKW_GetCurrent(_ callback: @escaping MKW_GetCurrentCallback)
{
	AppDistributorWrapper.getCurrent
	{ (type: AppDistributorType, name: String) in
		name.withCString
		{ cStr in
			callback(type.rawValue, cStr)
		}
	}
}

@_cdecl("MKW_RequestCTToken")
public func MKW_RequestCTToken(_ callback: @escaping MKW_BoolStringCallback)
{
	AppDistributorWrapper.requestCTToken
	{ (success: Bool, output: String) in
		output.withCString
		{ cStr in
			callback(success, cStr)
		}
	}
}

@_cdecl("MKW_GetEligibilityRegion")
public func MKW_GetEligibilityRegion(_ callback: @escaping MKW_BoolStringCallback)
{
	AppDistributorWrapper.getEligibilityRegion
	{ (success: Bool, output: String) in
		output.withCString
		{ cStr in
			callback(success, cStr)
		}
	}
}

// MARK: - MarketplaceKit extension

#if canImport(MarketplaceKit)
@available(iOS 17.4, *)
private extension AppDistributor
{
	var distributorType: (AppDistributorType, String)
	{
		switch self
		{
		case .appStore:					return (AppDistributorType.AppStore, String())
		case .testFlight:				return (AppDistributorType.TestFlight, String())
		case .marketplace(let Name):	return (AppDistributorType.Marketplace, Name)
		case .web:						return (AppDistributorType.Web, String())
		case .other:					return (AppDistributorType.Other, String())
		default:						return (AppDistributorType.Other, String())
		}
	}
}
#endif

