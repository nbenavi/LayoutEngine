// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/**
 * Import data and options used when importing a static mesh from fbx
 */

#pragma once
#include "FbxStaticMeshImportData.generated.h"

UENUM()
namespace EVertexColorImportOption
{
	enum Type
	{
		/** Import the static mesh using the vertex colors from the FBX file */
		Replace,
		/** Ignore vertex colors from the FBX file, and keep the existing mesh vertex colors */
		Ignore,
		/** Override all vertex colors with the specified color */
		Override
	};
}

UCLASS(config=EditorPerProjectUserSettings, AutoExpandCategories=(Options), MinimalAPI)
class UFbxStaticMeshImportData : public UFbxMeshImportData
{
	GENERATED_UCLASS_BODY()

	/** For static meshes, enabling this option will combine all meshes in the FBX into a single monolithic mesh in Unreal */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category=ImportSettings, meta=(ImportType="StaticMesh"))
	FName StaticMeshLODGroup;

	/** Specify how vertex colors should be imported */
	UPROPERTY(EditAnywhere, AdvancedDisplay, config, Category=ImportSettings, meta=(OBJRestrict="true", ImportType="StaticMesh"))
	TEnumAsByte<EVertexColorImportOption::Type> VertexColorImportOption;

	/** Specify override color in the case that VertexColorImportOption is set to Override */
	UPROPERTY(EditAnywhere, AdvancedDisplay, config, Category=ImportSettings, meta=(OBJRestrict="true", ImportType="StaticMesh"))
	FColor VertexOverrideColor;

	/** Disabling this option will keep degenerate triangles found.  In general you should leave this option on. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = ImportSettings, meta = (ImportType = "StaticMesh"))
	uint32 bRemoveDegenerates:1;
	
	UPROPERTY(EditAnywhere, config, AdvancedDisplay, Category=ImportSettings, meta=(ImportType="StaticMesh"))
	uint32 bGenerateLightmapUVs:1;

	/** If checked, one convex hull per UCX_ prefixed collision mesh will be generated instead of decomposing into multiple hulls */
	UPROPERTY(EditAnywhere, AdvancedDisplay, config, Category=ImportSettings, meta=(OBJRestrict="true", ImportType="StaticMesh"))
	uint32 bOneConvexHullPerUCX:1;

	/** If checked, collision will automatically be generated (ignored if custom collision is imported or used). */
	UPROPERTY(EditAnywhere, config, Category = ImportSettings, meta=(OBJRestrict="true", ImportType="StaticMesh"))
	uint32 bAutoGenerateCollision : 1;

	/** Gets or creates fbx import data for the specified static mesh */
	static UFbxStaticMeshImportData* GetImportDataForStaticMesh(UStaticMesh* StaticMesh, UFbxStaticMeshImportData* TemplateForCreation);

	bool CanEditChange( const UProperty* InProperty ) const override;
};



