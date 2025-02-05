// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOSRuntimeSettings.generated.h"


UENUM()
enum class EPowerUsageFrameRateLock : uint8
{
    /** Frame rate is not limited */
    PUFRL_None = 0 UMETA(DisplayName="None"),
        
    /** Frame rate is limited to a maximum of 20 frames per second */
    PUFRL_20 = 20 UMETA(DisplayName="20 FPS"),
    
    /** Frame rate is limited to a maximum of 30 frames per second */
    PUFRL_30 = 30 UMETA(DisplayName="30 FPS"),
    
    /** Frame rate is limited to a maximum of 60 frames per second */
    PUFRL_60 = 60 UMETA(DisplayName="60 FPS"),
};

UENUM()
	enum class EIOSVersion : uint8
{
	/** iOS 6.1 */
	IOS_61 = 6 UMETA(DisplayName="6.1"),

	/** iOS 7 */
	IOS_7 = 7 UMETA(DisplayName="7.0"),

	/** iOS 8 */
	IOS_8 = 8 UMETA(DisplayName="8.0"),
};


/**
 *	IOS Build resource file struct, used to serialize filepaths to the configs for use in the build system,
 */
USTRUCT()
struct FIOSBuildResourceFilePath
{
	GENERATED_USTRUCT_BODY()
	
	/**
	 * Custom export item used to serialize FIOSBuildResourceFilePath types as only a filename, no garland.
	 */
	bool ExportTextItem(FString& ValueStr, FIOSBuildResourceFilePath const& DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const
	{
		ValueStr += FilePath;
		return true;
	}

	/**
	 * Custom import item used to parse ini entries straight into the filename.
	 */
	bool ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText)
	{
		FilePath = Buffer;
		return true;
	}

	/**
	 * The path to the file.
	 */
	UPROPERTY(EditAnywhere, Category = FilePath)
	FString FilePath;
};

/**
 *	Setup our resource filepath to make it easier to parse in UBT
 */
template<>
struct TStructOpsTypeTraits<FIOSBuildResourceFilePath> : public TStructOpsTypeTraitsBase
{
	enum
	{
		WithExportTextItem = true,
		WithImportTextItem = true,
	};
};



/**
 *	IOS Build resource file struct, used to serialize Directorys to the configs for use in the build system,
 */
USTRUCT()
struct FIOSBuildResourceDirectory
{
	GENERATED_USTRUCT_BODY()

	/**
	 * Custom export item used to serialize FIOSBuildResourceDirectory types as only a filename, no garland.
	 */
	bool ExportTextItem(FString& ValueStr, FIOSBuildResourceDirectory const& DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const
	{
		ValueStr += Path;
		return true;
	}

	/**
	 * Custom import item used to parse ini entries straight into the filename.
	 */
	bool ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText)
	{
		Path = Buffer;
		return true;
	}

	/**
	* The path to the file.
	*/
	UPROPERTY(EditAnywhere, Category = Directory)
	FString Path;
};

/**
*	Setup our resource Directory to make it easier to parse in UBT
*/
template<>
struct TStructOpsTypeTraits<FIOSBuildResourceDirectory> : public TStructOpsTypeTraitsBase
{
	enum
	{
		WithExportTextItem = true,
		WithImportTextItem = true,
	};
};



/**
 * Implements the settings for the iOS target platform.
 */
UCLASS(config=Engine, defaultconfig)
class IOSRUNTIMESETTINGS_API UIOSRuntimeSettings : public UObject
{
public:
	GENERATED_UCLASS_BODY()

	// Should Game Center support (iOS Online Subsystem) be enabled?
	UPROPERTY(GlobalConfig, EditAnywhere, Category = Online, meta = (ConfigHierarchyEditable))
	uint32 bEnableGameCenterSupport : 1;
	
	// Whether or not to add support for Metal API (requires IOS8 and A7 processors).
	UPROPERTY(GlobalConfig, EditAnywhere, Category = Rendering, meta = (DisplayName = "Support Forward Rendering with Metal (A7 and up devices)"))
	bool bSupportsMetal;

	// Whether or not to add support for deferred rendering Metal API (requires IOS8 and A8 processors)
	UPROPERTY(GlobalConfig, EditAnywhere, Category = Rendering, meta = (DisplayName = "[Work in Progress] Support Deferred Rendering with Metal (A8 and up devices)"))
	bool bSupportsMetalMRT;
	
	// Whether or not to add support for OpenGL ES2 (if this is false, then your game should specify minimum IOS8 version)
	UPROPERTY(GlobalConfig, EditAnywhere, Category = Rendering)
	bool bSupportsOpenGLES2;

	// Enable ArmV7 support? (this will be used if all type are unchecked)
	UPROPERTY(GlobalConfig, EditAnywhere, Category = Build, meta = (DisplayName = "Support armv7 in Development"))
	bool bDevForArmV7;

	// Enable Arm64 support?
	UPROPERTY(GlobalConfig, EditAnywhere, Category = Build, meta = (DisplayName = "Support arm64 in Development"))
	bool bDevForArm64;

	// Enable ArmV7s support?
	UPROPERTY(GlobalConfig, EditAnywhere, Category = Build, meta = (DisplayName = "Support armv7s in Development"))
	bool bDevForArmV7S;

	// Enable ArmV7 support? (this will be used if all type are unchecked)
	UPROPERTY(GlobalConfig, EditAnywhere, Category = Build, meta = (DisplayName = "Support armv7 in Shipping"))
	bool bShipForArmV7;

	// Enable Arm64 support?
	UPROPERTY(GlobalConfig, EditAnywhere, Category = Build, meta = (DisplayName = "Support arm64 in Shipping"))
	bool bShipForArm64;

	// Enable ArmV7s support?
	UPROPERTY(GlobalConfig, EditAnywhere, Category = Build, meta = (DisplayName = "Support armv7s in Shipping"))
	bool bShipForArmV7S;
	
	// The name or ip address of the remote mac which will be used to build IOS
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Build", meta = (ConfigHierarchyEditable))
	FString RemoteServerName;

	// Enable the use of RSync for remote builds on a mac
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Build", meta = (DisplayName = "Use RSync for building IOS", ConfigHierarchyEditable))
	bool bUseRSync;

	// The mac users name which matches the SSH Private Key, for remote builds using RSync.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Build", meta = (EditCondition = "bUseRSync", DisplayName = "Username on Remote Server.", ConfigHierarchyEditable))
	FString RSyncUsername;

	// The install directory of DeltaCopy.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Build", meta = (EditCondition = "bUseRSync", ConfigHierarchyEditable))
	FIOSBuildResourceDirectory DeltaCopyInstallPath;

	// The existing location of an SSH Key found by UE4.
	UPROPERTY(VisibleAnywhere, Category = "Build", meta = (DisplayName = "Found Existing SSH permissions file"))
	FString SSHPrivateKeyLocation;

	// The path of the ssh permissions key to be used when connecting to the remote server.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Build", meta = (EditCondition = "bUseRSync", DisplayName = "Override existing SSH permissions file", ConfigHierarchyEditable))
	FIOSBuildResourceFilePath SSHPrivateKeyOverridePath;

	// Supports default portrait orientation. Landscape will not be supported.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = DeviceOrientations)
	uint32 bSupportsPortraitOrientation : 1;

	// Supports upside down portrait orientation. Landscape will not be supported.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = DeviceOrientations)
	uint32 bSupportsUpsideDownOrientation : 1;

	// Supports left landscape orientation. Protrait will not be supported.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = DeviceOrientations)
	uint32 bSupportsLandscapeLeftOrientation : 1;

	// Supports right landscape orientation. Protrait will not be supported.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = DeviceOrientations)
	uint32 bSupportsLandscapeRightOrientation : 1;

	// Specifies the the display name for the application. This will be displayed under the icon on the device.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = BundleInformation)
	FString BundleDisplayName;

	// Specifies the the name of the application bundle. This is the short name for the application bundle.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = BundleInformation)
	FString BundleName;

	// Specifies the bundle identifier for the application.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = BundleInformation)
	FString BundleIdentifier;

	// Specifies the version for the application.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = BundleInformation)
	FString VersionInfo;
    
    /** Set the maximum frame rate to save on power consumption */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = PowerUsage, meta = (ConfigHierarchyEditable))
    TEnumAsByte<EPowerUsageFrameRateLock> FrameRateLock;

	// Minimum iOS version this game supports
	UPROPERTY(GlobalConfig, EditAnywhere, Category = OSInfo)
	TEnumAsByte<EIOSVersion> MinimumiOSVersion;

	// Whether or not to add support for iPad devices
	UPROPERTY(GlobalConfig, EditAnywhere, Category = DeviceUsage)
	uint32 bSupportsIPad : 1;

	// Whether or not to add support for iPhone devices
	UPROPERTY(GlobalConfig, EditAnywhere, Category = DeviceUsage)
	uint32 bSupportsIPhone : 1;

	// Any additional plist key/value data utilizing \n for a new line
	UPROPERTY(GlobalConfig, EditAnywhere, Category = ExtraData)
	FString AdditionalPlistData;

#if WITH_EDITOR
	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostInitProperties() override;
	// End of UObject interface
#endif
};
