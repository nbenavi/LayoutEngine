// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Runtime/InputCore/Classes/InputCoreTypes.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Components/SceneComponent.h"
#include "SceneTypes.h"
#include "Engine/EngineTypes.h"
#include "AI/Navigation/NavRelevantInterface.h"

#include "PrimitiveComponent.generated.h"

class FPrimitiveSceneProxy;
class AController; 
class UTexture;
struct FEngineShowFlags;
struct FConvexVolume;
struct FNavigableGeometryExport;

/** Information about a streaming texture that a primitive uses for rendering. */
struct FStreamingTexturePrimitiveInfo
{
	UTexture* Texture;
	FSphere Bounds;
	float TexelFactor;

	FStreamingTexturePrimitiveInfo()
		: Texture(nullptr)
		, Bounds(0)
		, TexelFactor(1.0f)
	{
	}
};

/** Determines whether a Character can attempt to step up onto a component when they walk in to it. */
UENUM()
enum ECanBeCharacterBase
{
	/** Character cannot step up onto this Component. */
	ECB_No,
	/** Character can step up onto this Component. */
	ECB_Yes,
	/**
	 * Owning actor determines whether character can step up onto this Component (default true unless overridden in code).
	 * @see AActor::CanBeBaseForCharacter()
	 */
	ECB_Owner,
	ECB_MAX,
};

UENUM()
namespace EHasCustomNavigableGeometry
{
	enum Type
	{
		// Primitive doesn't have custom navigation geometry, if collision is enabled then its convex/trimesh collision will be used for generating the navmesh
		No,

		// If primitive would normally affect navmesh, DoCustomNavigableGeometryExport() should be called to export this primitive's navigable geometry
		Yes,

		// DoCustomNavigableGeometryExport() should be called even if the mesh is non-collidable and wouldn't normally affect the navmesh
		EvenIfNotCollidable,

		// Don't export navigable geometry even if primitive is relevant for navigation (can still add modifiers)
		DontExport,
	};
}

/** Information about the sprite category */
USTRUCT()
struct FSpriteCategoryInfo
{
	GENERATED_USTRUCT_BODY()

	/** Sprite category that the component belongs to */
	UPROPERTY()
	FName Category;

	/** Localized name of the sprite category */
	UPROPERTY()
	FText DisplayName;

	/** Localized description of the sprite category */
	UPROPERTY()
	FText Description;
};

/**
 * Delegate for notification of blocking collision against a specific component.  
 * NormalImpulse will be filled in for physics-simulating bodies, but will be zero for swept-component blocking collisions. 
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams( FComponentHitSignature, class AActor*, OtherActor, class UPrimitiveComponent*, OtherComp, FVector, NormalImpulse, const FHitResult&, Hit );
/** Delegate for notification of start of overlap with a specific component */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams( FComponentBeginOverlapSignature,class AActor*, OtherActor, class UPrimitiveComponent*, OtherComp, int32, OtherBodyIndex, bool, bFromSweep, const FHitResult &, SweepResult);
/** Delegate for notification of end of overlap with a specific component */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams( FComponentEndOverlapSignature, class AActor*, OtherActor, class UPrimitiveComponent*, OtherComp, int32, OtherBodyIndex);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam( FComponentBeginCursorOverSignature, UPrimitiveComponent*, TouchedComponent );
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam( FComponentEndCursorOverSignature, UPrimitiveComponent*, TouchedComponent );
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam( FComponentOnClickedSignature, UPrimitiveComponent*, TouchedComponent );
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam( FComponentOnReleasedSignature, UPrimitiveComponent*, TouchedComponent );
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams( FComponentOnInputTouchBeginSignature, ETouchIndex::Type, FingerIndex, UPrimitiveComponent*, TouchedComponent );
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams( FComponentOnInputTouchEndSignature, ETouchIndex::Type, FingerIndex, UPrimitiveComponent*, TouchedComponent );
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams( FComponentBeginTouchOverSignature, ETouchIndex::Type, FingerIndex, UPrimitiveComponent*, TouchedComponent );
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams( FComponentEndTouchOverSignature, ETouchIndex::Type, FingerIndex, UPrimitiveComponent*, TouchedComponent );

/**
 * PrimitiveComponents are SceneComponents that contain or generate some sort of geometry, generally to be rendered or used as collision data.
 * There are several subclasses for the various types of geometry, but the most common by far are the ShapeComponents (Capsule, Sphere, Box), StaticMeshComponent, and SkeletalMeshComponent.
 * ShapeComponents generate geometry that is used for collision detection but are not rendered, while StaticMeshComponents and SkeletalMeshComponents contain pre-built geometry that is rendered, but can also be used for collision detection.
 */
UCLASS(abstract, HideCategories=(Mobility), ShowCategories=(PhysicsVolume))
class ENGINE_API UPrimitiveComponent : public USceneComponent, public INavRelevantInterface
{
	GENERATED_BODY()
public:

	/**
	 * Default UObject constructor.
	 */
	UPrimitiveComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Rendering
	
	/**
	 * The minimum distance at which the primitive should be rendered, 
	 * measured in world space units from the center of the primitive's bounding sphere to the camera position.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=LOD)
	float MinDrawDistance;

	/**  Max draw distance exposed to LDs. The real max draw distance is the min (disregarding 0) of this and volumes affecting this object. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=LOD, meta=(DisplayName="Desired Max Draw Distance") )
	float LDMaxDrawDistance;

	/**
	 * The distance to cull this primitive at.  
	 * A CachedMaxDrawDistance of 0 indicates that the primitive should not be culled by distance.
	 */
	UPROPERTY(Category=LOD, AdvancedDisplay, VisibleAnywhere, BlueprintReadOnly, meta=(DisplayName="Current Max Draw Distance") )
	float CachedMaxDrawDistance;

	/** The scene depth priority group to draw the primitive in. */
	UPROPERTY()
	TEnumAsByte<enum ESceneDepthPriorityGroup> DepthPriorityGroup;

	/** The scene depth priority group to draw the primitive in, if it's being viewed by its owner. */
	UPROPERTY()
	TEnumAsByte<enum ESceneDepthPriorityGroup> ViewOwnerDepthPriorityGroup;

public:
	/** 
	 * Indicates if we'd like to create physics state all the time (for collision and simulation). 
	 * If you set this to false, it still will create physics state if collision or simulation activated. 
	 * This can help performance if you'd like to avoid overhead of creating physics state when triggers 
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Collision)
	uint32 bAlwaysCreatePhysicsState:1;

	/**
	 * If true, this component will generate overlap events when it is overlapping other components (eg Begin Overlap).
	 * Both components (this and the other) must have this enabled for overlap events to occur.
	 *
	 * @see [Overlap Events](https://docs.unrealengine.com/latest/INT/Engine/Physics/Collision/index.html#overlapandgenerateoverlapevents)
	 * @see UpdateOverlaps(), BeginComponentOverlap(), EndComponentOverlap()
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Collision)
	uint32 bGenerateOverlapEvents:1;

	/**
	 * If true, this component will generate individual overlaps for each overlapping physics body if it is a multi-body component. When false, this component will
	 * generate only one overlap, regardless of how many physics bodies it has and how many of them are overlapping another component/body. This flag has no
	 * influence on single body components.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=Collision)
	uint32 bMultiBodyOverlap:1;

	/**
	 * If true, this component will look for collisions on both physic scenes during movement.
	 * Only required if the asynchronous physics scene is enabled and has geometry in it, and you wish to test for collisions with objects in that scene.
	 * @see MoveComponent()
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=Collision)
	uint32 bCheckAsyncSceneOnMove:1;

	/**
	 * If true, component sweeps with this component should trace against complex collision during movement (for example, each triangle of a mesh).
	 * If false, collision will be resolved against simple collision bounds instead.
	 * @see MoveComponent()
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=Collision)
	uint32 bTraceComplexOnMove:1;

	/**
	 * If true, component sweeps will return the material in their hit result.
	 * @see MoveComponent(), FHitResult
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=Collision)
	uint32 bReturnMaterialOnMove:1;

	/** True if the primitive should be rendered using ViewOwnerDepthPriorityGroup if viewed by its owner. */
	UPROPERTY()
	uint32 bUseViewOwnerDepthPriorityGroup:1;

	/** Whether to accept cull distance volumes to modify cached cull distance. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=LOD)
	uint32 bAllowCullDistanceVolume:1;

	/** true if the primitive has motion blur velocity meshes */
	UPROPERTY()
	uint32 bHasMotionBlurVelocityMeshes:1;
	
	/** If true, this component will be rendered in the CustomDepth pass (usually used for outlines) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Rendering)
	uint32 bRenderCustomDepth:1;

	/** If true, this component will be rendered in the main pass (z prepass, basepass, transparency) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = Rendering)
	uint32 bRenderInMainPass:1;

	/** Whether the primitive receives decals. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Rendering)
	uint32 bReceivesDecals:1;

	/** If this is True, this component won't be visible when the view actor is the component's owner, directly or indirectly. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = Rendering)
	uint32 bOwnerNoSee:1;

	/** If this is True, this component will only be visible when the view actor is the component's owner, directly or indirectly. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = Rendering)
	uint32 bOnlyOwnerSee:1;

	/** Treat this primitive as part of the background for occlusion purposes. This can be used as an optimization to reduce the cost of rendering skyboxes, large ground planes that are part of the vista, etc. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Rendering)
	uint32 bTreatAsBackgroundForOcclusion:1;

	/** 
	 * Whether to render the primitive in the depth only pass.  
	 * This should generally be true for all objects, and let the renderer make decisions about whether to render objects in the depth only pass.
	 * @todo - if any rendering features rely on a complete depth only pass, this variable needs to go away.
	 */
	UPROPERTY()
	uint32 bUseAsOccluder:1;

	/** If this is True, this component can be selected in the editor. */
	UPROPERTY()
	uint32 bSelectable:1;

	/** If true, forces mips for textures used by this component to be resident when this component's level is loaded. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=TextureStreaming)
	uint32 bForceMipStreaming:1;

	/** If true a hit-proxy will be generated for each instance of instanced static meshes */
	UPROPERTY()
	uint32 bHasPerInstanceHitProxies:1;

	// Lighting flags
	
	/**
	 * Controls whether the primitive component should cast a shadow or not.
	 *
	 * This flag is ignored (no shadows will be generated) if all materials on this component have an Unlit shading model.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Lighting)
	uint32 CastShadow:1;

	/** Controls whether the primitive should inject light into the Light Propagation Volume.  This flag is only used if CastShadow is true. **/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Lighting, AdvancedDisplay, meta=(EditCondition="CastShadow"))
	uint32 bAffectDynamicIndirectLighting:1;

	/** Controls whether the primitive should affect dynamic distance field lighting methods.  This flag is only used if CastShadow is true. **/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Lighting, AdvancedDisplay, meta=(EditCondition="CastShadow"))
	uint32 bAffectDistanceFieldLighting:1;

	/** Controls whether the primitive should cast shadows in the case of non precomputed shadowing.  This flag is only used if CastShadow is true. **/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Lighting, AdvancedDisplay, meta=(EditCondition="CastShadow", DisplayName = "Dynamic Shadow"))
	uint32 bCastDynamicShadow:1;

	/** Whether the object should cast a static shadow from shadow casting lights.  This flag is only used if CastShadow is true. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Lighting, AdvancedDisplay, meta=(EditCondition="CastShadow", DisplayName = "Static Shadow"))
	uint32 bCastStaticShadow:1;

	/** 
	 * Whether the object should cast a volumetric translucent shadow.
	 * Volumetric translucent shadows are useful for primitives with smoothly changing opacity like particles representing a volume, 
	 * But have artifacts when used on highly opaque surfaces.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Lighting, meta=(EditCondition="CastShadow", DisplayName = "Volumetric Translucent Shadow"))
	uint32 bCastVolumetricTranslucentShadow:1;

	/** 
	 * When enabled, the component will only cast a shadow on itself and not other components in the world.  This is especially useful for first person weapons, and forces bCastInsetShadow to be enabled.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Lighting, meta=(EditCondition="CastShadow"))
	uint32 bSelfShadowOnly:1;

	/** 
	 * When enabled, the component will be rendering into the far shadow cascades (only for directional lights).
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Lighting, meta=(EditCondition="CastShadow", DisplayName = "Far Shadow"))
	uint32 bCastFarShadow:1;

	/** 
	 * Whether this component should create a per-object shadow that gives higher effective shadow resolution. 
	 * Useful for cinematic character shadowing. Assumed to be enabled if bSelfShadowOnly is enabled.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Lighting, meta=(EditCondition="CastShadow", DisplayName = "Dynamic Inset Shadow"))
	uint32 bCastInsetShadow:1;

	/** 
	 * Whether this component should cast shadows from lights that have bCastShadowsFromCinematicObjectsOnly enabled.
	 * This is useful for characters in a cinematic with special cinematic lights, where the cost of shadowmap rendering of the environment is undesired.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Lighting, meta=(EditCondition="CastShadow"))
	uint32 bCastCinematicShadow:1;

	/** 
	 *	If true, the primitive will cast shadows even if bHidden is true.
	 *	Controls whether the primitive should cast shadows when hidden.
	 *	This flag is only used if CastShadow is true.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Lighting, meta=(EditCondition="CastShadow", DisplayName = "Hidden Shadow"))
	uint32 bCastHiddenShadow:1;

	/** Whether this primitive should cast dynamic shadows as if it were a two sided material. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Lighting, meta=(EditCondition="CastShadow", DisplayName = "Shadow Two Sided"))
	uint32 bCastShadowAsTwoSided:1;

	/** 
	 * Whether to light this primitive as if it were static, including generating lightmaps.  
	 * This only has an effect for component types that can bake lighting, like static mesh components.
	 * This is useful for moving meshes that don't change significantly.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Lighting)
	uint32 bLightAsIfStatic:1;

	/** 
	 * Whether to light this component and any attachments as a group.  This only has effect on the root component of an attachment tree.
	 * When enabled, attached component shadowing settings like bCastInsetShadow, bCastVolumetricTranslucentShadow, etc, will be ignored.
	 * This is useful for improving performance when multiple movable components are attached together.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Lighting)
	uint32 bLightAttachmentsAsGroup:1;

	/** Quality of indirect lighting for Movable primitives.  This has a large effect on Indirect Lighting Cache update time. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Lighting)
	TEnumAsByte<EIndirectLightingCacheQuality> IndirectLightingCacheQuality;

	UPROPERTY()
	bool bHasCachedStaticLighting;

	/** If true, asynchronous static build lighting will be enqueued to be applied to this */
	UPROPERTY(Transient)
	bool bStaticLightingBuildEnqueued;
	
	// Physics
	
	/** Will ignore radial impulses applied to this component. */
	UPROPERTY()
	uint32 bIgnoreRadialImpulse:1;

	/** Will ignore radial forces applied to this component. */
	UPROPERTY()
	uint32 bIgnoreRadialForce:1;

	// General flags.
	
	/** If this is True, this component must always be loaded on clients, even if Hidden and CollisionEnabled is NoCollision. */
	UPROPERTY()
	uint32 AlwaysLoadOnClient:1;

	/** If this is True, this component must always be loaded on servers, even if Hidden and CollisionEnabled is NoCollision */
	UPROPERTY()
	uint32 AlwaysLoadOnServer:1;

	/** Composite the drawing of this component onto the scene after post processing (only applies to editor drawing) */
	UPROPERTY()
	uint32 bUseEditorCompositing:1;

	/**
	 * Translucent objects with a lower sort priority draw behind objects with a higher priority.
	 * Translucent objects with the same priority are rendered from back-to-front based on their bounds origin.
	 *
	 * Ignored if the object is not translucent.  The default priority is zero.
	 * Warning: This should never be set to a non-default value unless you know what you are doing, as it will prevent the renderer from sorting correctly.  
	 * It is especially problematic on dynamic gameplay effects.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category=Rendering)
	int32 TranslucencySortPriority;

	/** Used for precomputed visibility */
	UPROPERTY()
	int32 VisibilityId;

	/** Used by the renderer, to identify a component across re-registers. */
	FPrimitiveComponentId ComponentId;

	/**
	 * Multiplier used to scale the Light Propagation Volume light injection bias, to reduce light bleeding. 
	 * Set to 0 for no bias, 1 for default or higher for increased biasing (e.g. for 
	 * thin geometry such as walls)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category=Rendering, meta=(UIMin = "0.0", UIMax = "3.0"))
	float LpvBiasMultiplier;

	// Internal physics engine data.
	
	/** Physics scene information for this component, holds a single rigid body with multiple shapes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Collision, meta=(ShowOnlyInnerProperties))
	FBodyInstance BodyInstance;

	/** Whether this component can potentially influence navigation */
	UPROPERTY(EditAnywhere, Category=Collision, AdvancedDisplay)
	uint32 bCanEverAffectNavigation:1;

	/** Cached navigation relevancy flag for collision updates */
	uint32 bNavigationRelevant : 1;

protected:

	virtual void UpdateNavigationData() override;

	/** Returns true if all descendant components that we can possibly collide with use relative location and rotation. */
	virtual bool AreAllCollideableDescendantsRelative(bool bAllowCachedValue = true) const;

	/** Result of last call to AreAllCollideableDescendantsRelative(). */
	uint32 bCachedAllCollideableDescendantsRelative:1;

	/** Last time we checked AreAllCollideableDescendantsRelative(), so we can throttle those tests since it rarely changes once false. */
	float LastCheckedAllCollideableDescendantsTime;

	/** If true then DoCustomNavigableGeometryExport will be called to collect navigable geometry of this component. */
	UPROPERTY()
	TEnumAsByte<EHasCustomNavigableGeometry::Type> bHasCustomNavigableGeometry;

	/** Next id to be used by a component. */
	static FThreadSafeCounter NextComponentId;

public:
	/** 
	 * Scales the bounds of the object.
	 * This is useful when using World Position Offset to animate the vertices of the object outside of its bounds. 
	 * Warning: Increasing the bounds of an object will reduce performance and shadow quality!
	 * Currently only used by StaticMeshComponent and SkeletalMeshComponent.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category=Rendering, meta=(UIMin = "1", UIMax = "10.0"))
	float BoundsScale;

	/** Last time the component was submitted for rendering (called FScene::AddPrimitive). */
	UPROPERTY(transient)
	float LastSubmitTime;

	/**
	 * The value of WorldSettings->TimeSeconds for the frame when this component was last rendered.  This is written
	 * from the render thread, which is up to a frame behind the game thread, so you should allow this time to
	 * be at least a frame behind the game thread's world time before you consider the actor non-visible.
	 */
	UPROPERTY(transient)
	float LastRenderTime;

private:
	UPROPERTY()
	TEnumAsByte<enum ECanBeCharacterBase> CanBeCharacterBase_DEPRECATED;

public:
	/**
	 * Determine whether a Character can step up onto this component.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Collision)
	TEnumAsByte<enum ECanBeCharacterBase> CanCharacterStepUpOn;

	/**
	 * Set of actors to ignore during component sweeps in MoveComponent().
	 * All components owned by these actors will be ignored when this component moves or updates overlaps.
	 * Components on the other Actor may also need to be told to do the same when they move.
	 * @see IgnoreActorWhenMoving()
	 */
	TArray<TWeakObjectPtr<AActor> > MoveIgnoreActors;

	/**
	 * Tells this component whether to ignore collision with all components of a specific Actor when this component is moved.
	 * Components on the other Actor may also need to be told to do the same when they move.
	 */
	UFUNCTION(BlueprintCallable, Category = "Collision", meta=(Keywords="Move MoveIgnore"))
	void IgnoreActorWhenMoving(AActor* Actor, bool bShouldIgnore);

	/**
	 * Returns the list of actors we currently ignore when moving.
	 */
	UFUNCTION(BlueprintCallable, Category = "Collision")
	TArray<TWeakObjectPtr<AActor> > & GetMoveIgnoreActors();

	/**
	 * Clear the list of actors we ignore when moving.
	 */
	UFUNCTION(BlueprintCallable, Category = "Collision")
	void ClearMoveIgnoreActors();


#if WITH_EDITOR
	/** Override delegate used for checking the selection state of a component */
	DECLARE_DELEGATE_RetVal_OneParam( bool, FSelectionOverride, const UPrimitiveComponent* );
	FSelectionOverride SelectionOverrideDelegate;
#endif

protected:
	/** Set of components that this component is currently overlapping. */
	TArray<FOverlapInfo> OverlappingComponents;

public:
	/** 
	 * Begin tracking an overlap interaction with the component specified.
	 * @param OtherComp - The component of the other actor that this component is now overlapping
	 * @param bDoNotifies - True to dispatch appropriate begin/end overlap notifications when these events occur.
	 * @see [Overlap Events](https://docs.unrealengine.com/latest/INT/Engine/Physics/Collision/index.html#overlapandgenerateoverlapevents)
	 */
	void BeginComponentOverlap(const FOverlapInfo& OtherOverlap, bool bDoNotifies);
	
	/** 
	 * Finish tracking an overlap interaction that is no longer occurring between this component and the component specified. 
	 * @param OtherComp - The component of the other actor to stop overlapping
	 * @param bDoNotifies - True to dispatch appropriate begin/end overlap notifications when these events occur.
	 * @param bNoNotifySelf	- True to skip end overlap notifications to this component's.  Does not affect notifications to OtherComp's actor.
	 * @see [Overlap Events](https://docs.unrealengine.com/latest/INT/Engine/Physics/Collision/index.html#overlapandgenerateoverlapevents)
	 */
	void EndComponentOverlap(const FOverlapInfo& OtherOverlap, bool bDoNotifies=true, bool bNoNotifySelf=false);

	/**
	 * Check whether this component is overlapping another component.
	 * @param OtherComp Component to test this component against.
	 * @return Whether this component is overlapping another component.
	 */
	UFUNCTION(BlueprintCallable, Category="Collision", meta=(UnsafeDuringActorConstruction="true"))
	bool IsOverlappingComponent(const UPrimitiveComponent* OtherComp) const;
	
	/** Check whether this component has the specified overlap. */
	bool IsOverlappingComponent(const FOverlapInfo& Overlap) const;

	/**
	 * Check whether this component is overlapping any component of the given Actor.
	 * @param Other Actor to test this component against.
	 * @return Whether this component is overlapping any component of the given Actor.
	 */
	UFUNCTION(BlueprintCallable, Category="Collision", meta=(UnsafeDuringActorConstruction="true"))
	bool IsOverlappingActor(const AActor* Other) const;

	/** Appends list of overlaps with components owned by the given actor to the 'OutOverlaps' array. Returns true if any overlaps were added. */
	bool GetOverlapsWithActor(const AActor* Actor, TArray<FOverlapInfo>& OutOverlaps) const;

	/** 
	 * Returns a list of actors that this component is overlapping.
	 * @param OverlappingActors		[out] Returned list of overlapping actors
	 * @param ClassFilter			[optional] If set, only returns actors of this class or subclasses
	 */
	UFUNCTION(BlueprintCallable, Category="Collision", meta=(UnsafeDuringActorConstruction="true"))
	void GetOverlappingActors(TArray<AActor*>& OverlappingActors, UClass* ClassFilter=NULL) const;

	/** Returns list of components this component is overlapping. */
	UFUNCTION(BlueprintCallable, Category="Collision", meta=(UnsafeDuringActorConstruction="true"))
	void GetOverlappingComponents(TArray<UPrimitiveComponent*>& OverlappingComponents) const;

	/** Returns list of components this component is overlapping. */
	UFUNCTION(BlueprintCallable, Category="Collision")
	const TArray<FOverlapInfo>& GetOverlapInfos() const;

	/** 
	 * Queries world and updates overlap tracking state for this component.
	 * @param PendingOverlaps			An ordered list of components that the MovedComponent overlapped during its movement (i.e. generated during a sweep).
	 *									May not be overlapping them now.
	 * @param bDoNotifies				True to dispatch being/end overlap notifications when these events occur.
	 * @param OverlapsAtEndLocation		If non-null, the given list of overlaps will be used as the overlaps for this component at the current location, rather than checking for them separately.
	 *									Generally this should only be used if this component is the RootComponent of the owning actor and overlaps with other descendant components have been verified.
	 */
	virtual void UpdateOverlaps(TArray<FOverlapInfo> const* PendingOverlaps=NULL, bool bDoNotifies=true, const TArray<FOverlapInfo>* OverlapsAtEndLocation=NULL) override;

	/** Update current physics volume for this component, if bShouldUpdatePhysicsVolume is true. Overridden to use the overlaps to find the physics volume. */
	virtual void UpdatePhysicsVolume( bool bTriggerNotifiers ) override;

	/**
	 *  Test the collision of the supplied component at the supplied location/rotation, and determine the set of components that it overlaps.
	 *  @note This overload taking rotation as a FQuat is slightly faster than the version using FRotator.
	 *  @note This simply calls the virtual ComponentOverlapMultiImpl() which can be overridden to implement custom behavior.
	 *  @param  OutOverlaps     Array of overlaps found between this component in specified pose and the world
	 *  @param  World			World to use for overlap test
	 *  @param  Pos             Location of component's geometry for the test against the world
	 *  @param  Rot             Rotation of component's geometry for the test against the world
	 *  @param  TestChannel		The 'channel' that this ray is in, used to determine which components to hit
	 *  @param	ObjectQueryParams	List of object types it's looking for. When this enters, we do object query with component shape
	 *  @return true if OutOverlaps contains any blocking results
	 */
	bool ComponentOverlapMulti(TArray<struct FOverlapResult>& OutOverlaps, const class UWorld* World, const FVector& Pos, const FQuat& Rot, ECollisionChannel TestChannel, const struct FComponentQueryParams& Params, const struct FCollisionObjectQueryParams& ObjectQueryParams = FCollisionObjectQueryParams::DefaultObjectQueryParam) const;
	bool ComponentOverlapMulti(TArray<struct FOverlapResult>& OutOverlaps, const class UWorld* World, const FVector& Pos, const FRotator& Rot, ECollisionChannel TestChannel, const struct FComponentQueryParams& Params, const struct FCollisionObjectQueryParams& ObjectQueryParams = FCollisionObjectQueryParams::DefaultObjectQueryParam) const;

protected:

	// Override this method for custom behavior.
	virtual bool ComponentOverlapMultiImpl(TArray<struct FOverlapResult>& OutOverlaps, const class UWorld* World, const FVector& Pos, const FQuat& Rot, ECollisionChannel TestChannel, const struct FComponentQueryParams& Params, const struct FCollisionObjectQueryParams& ObjectQueryParams = FCollisionObjectQueryParams::DefaultObjectQueryParam) const;

public:

	/** 
	 *	Event called when a component hits (or is hit by) something solid. This could happen due to things like Character movement, using Set Location with 'sweep' enabled, or physics simulation.
	 *	For events when objects overlap (e.g. walking into a trigger) see the 'Overlap' event.
	 *
	 *	@note For collisions during physics simulation to generate hit events, 'Simulation Generates Hit Events' must be enabled for this component.
	 *	@note When receiving a hit from another object's movement, the directions of 'Hit.Normal' and 'Hit.ImpactNormal'
	 *	will be adjusted to indicate force from the other object against this object.
	 */
	UPROPERTY(BlueprintAssignable, Category="Collision")
	FComponentHitSignature OnComponentHit;

	/** 
	 *	Event called when something starts to overlaps this component, for example a player walking into a trigger.
	 *	For events when objects have a blocking collision, for example a player hitting a wall, see 'Hit' events.
	 *
	 *	@note Both this component and the other one must have bGenerateOverlapEvents set to true to generate overlap events.
	 *	@note When receiving an overlap from another object's movement, the directions of 'Hit.Normal' and 'Hit.ImpactNormal'
	 *	will be adjusted to indicate force from the other object against this object.
	 */
	UPROPERTY(BlueprintAssignable, Category="Collision")
	FComponentBeginOverlapSignature OnComponentBeginOverlap;

	/** 
	 *	Event called when something stops overlapping this component 
	 *	@note Both this component and the other one must have bGenerateOverlapEvents set to true to generate overlap events.
	 */
	UPROPERTY(BlueprintAssignable, Category="Collision")
	FComponentEndOverlapSignature OnComponentEndOverlap;

	/** Event called when the mouse cursor is moved over this component and mouse over events are enabled in the player controller */
	UPROPERTY(BlueprintAssignable, Category="Input|Mouse Input")
	FComponentBeginCursorOverSignature OnBeginCursorOver;
		 
	/** Event called when the mouse cursor is moved off this component and mouse over events are enabled in the player controller */
	UPROPERTY(BlueprintAssignable, Category="Input|Mouse Input")
	FComponentEndCursorOverSignature OnEndCursorOver;

	/** Event called when the left mouse button is clicked while the mouse is over this component and click events are enabled in the player controller */
	UPROPERTY(BlueprintAssignable, Category="Input|Mouse Input")
	FComponentOnClickedSignature OnClicked;

	/** Event called when the left mouse button is released while the mouse is over this component click events are enabled in the player controller */
	UPROPERTY(BlueprintAssignable, Category="Input|Mouse Input")
	FComponentOnReleasedSignature OnReleased;
		 
	/** Event called when a touch input is received over this component when touch events are enabled in the player controller */
	UPROPERTY(BlueprintAssignable, Category="Input|Touch Input")
	FComponentOnInputTouchBeginSignature OnInputTouchBegin;

	/** Event called when a touch input is released over this component when touch events are enabled in the player controller */
	UPROPERTY(BlueprintAssignable, Category="Input|Touch Input")
	FComponentOnInputTouchEndSignature OnInputTouchEnd;

	/** Event called when a finger is moved over this component when touch over events are enabled in the player controller */
	UPROPERTY(BlueprintAssignable, Category="Input|Touch Input")
	FComponentBeginTouchOverSignature OnInputTouchEnter;

	/** Event called when a finger is moved off this component when touch over events are enabled in the player controller */
	UPROPERTY(BlueprintAssignable, Category="Input|Touch Input")
	FComponentEndTouchOverSignature OnInputTouchLeave;

	/**
	 * Returns the material used by the element at the specified index
	 * @param ElementIndex - The element to access the material of.
	 * @return the material used by the indexed element of this mesh.
	 */
	UFUNCTION(BlueprintCallable, Category="Rendering|Material")
	virtual class UMaterialInterface* GetMaterial(int32 ElementIndex) const;

	/**
	 * Changes the material applied to an element of the mesh.
	 * @param ElementIndex - The element to access the material of.
	 * @return the material used by the indexed element of this mesh.
	 */
	UFUNCTION(BlueprintCallable, Category="Rendering|Material")
	virtual void SetMaterial(int32 ElementIndex, class UMaterialInterface* Material);

	/**
	 * Creates a Dynamic Material Instance for the specified element index.  The parent of the instance is set to the material being replaced.
	 * @param ElementIndex - The index of the skin to replace the material for.  If invalid, the material is unchanged and NULL is returned.
	 */
	UFUNCTION(BlueprintCallable, meta=(DisplayName = "CreateMIDForElement", DeprecatedFunction, DeprecationMessage="Use CreateDynamicMaterialInstance instead."), Category="Rendering|Material")
	virtual class UMaterialInstanceDynamic* CreateAndSetMaterialInstanceDynamic(int32 ElementIndex);

	/**
	 * Creates a Dynamic Material Instance for the specified element index.  The parent of the instance is set to the material being replaced.
	 * @param ElementIndex - The index of the skin to replace the material for.  If invalid, the material is unchanged and NULL is returned.
	 */
	UFUNCTION(BlueprintCallable, meta=(DisplayName = "CreateMIDForElementFromMaterial", DeprecatedFunction, DeprecationMessage="Use CreateDynamicMaterialInstance instead."), Category="Rendering|Material")
	virtual class UMaterialInstanceDynamic* CreateAndSetMaterialInstanceDynamicFromMaterial(int32 ElementIndex, class UMaterialInterface* Parent);

	/**
	 * Creates a Dynamic Material Instance for the specified element index, optionally from the supplied material.
	 * @param ElementIndex - The index of the skin to replace the material for.  If invalid, the material is unchanged and NULL is returned.
	 */
	UFUNCTION(BlueprintCallable, Category="Rendering|Material")
	virtual class UMaterialInstanceDynamic* CreateDynamicMaterialInstance(int32 ElementIndex, class UMaterialInterface* SourceMaterial = NULL);

	/** Returns the slope override struct for this component. */
	UFUNCTION(BlueprintCallable, Category="Physics")
	const struct FWalkableSlopeOverride& GetWalkableSlopeOverride() const;

	/** Sets a new slope override for this component instance. */
	UFUNCTION(BlueprintCallable, Category="Physics")
	void SetWalkableSlopeOverride(const FWalkableSlopeOverride& NewOverride);

	/** 
	 *	Sets whether or not a single body should use physics simulation, or should be 'fixed' (kinematic).
	 *
	 *	@param	bSimulate	New simulation state for single body
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void SetSimulatePhysics(bool bSimulate);

	/**
	 * Determines whether or not the simulate physics setting can be edited interactively on this component
	 */
	virtual bool CanEditSimulatePhysics();

	/**
	* Sets the constraint mode of the component.
	* @param ConstraintMode	The type of constraint to use.
	*/
	DEPRECATED(4.8, "This function is deprecated. Please use SetConstraintMode instead.")
	UFUNCTION(BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "Use SetConstraintMode instead", Keywords = "set locked axis constraint physics"), Category = Physics)
	virtual void SetLockedAxis(EDOFMode::Type LockedAxis);

	/**
	 * Sets the constraint mode of the component.
	 * @param ConstraintMode	The type of constraint to use.
	 */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Constraint Mode", Keywords = "set locked axis constraint physics"), Category = Physics)
	virtual void SetConstraintMode(EDOFMode::Type ConstraintMode);

	/**
	 *	Add an impulse to a single rigid body. Good for one time instant burst.
	 *
	 *	@param	Impulse		Magnitude and direction of impulse to apply.
	 *	@param	BoneName	If a SkeletalMeshComponent, name of body to apply impulse to. 'None' indicates root body.
	 *	@param	bVelChange	If true, the Strength is taken as a change in velocity instead of an impulse (ie. mass will have no affect).
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void AddImpulse(FVector Impulse, FName BoneName = NAME_None, bool bVelChange = false);

	/**
	*	Add an angular impulse to a single rigid body. Good for one time instant burst.
	*
	*	@param	AngularImpulse	Magnitude and direction of impulse to apply. Direction is axis of rotation.
	*	@param	BoneName	If a SkeletalMeshComponent, name of body to apply angular impulse to. 'None' indicates root body.
	*	@param	bVelChange	If true, the Strength is taken as a change in angular velocity instead of an impulse (ie. mass will have no affect).
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	virtual void AddAngularImpulse(FVector Impulse, FName BoneName = NAME_None, bool bVelChange = false);

	/**
	 *	Add an impulse to a single rigid body at a specific location. 
	 *
	 *	@param	Impulse		Magnitude and direction of impulse to apply.
	 *	@param	Location	Point in world space to apply impulse at.
	 *	@param	BoneName	If a SkeletalMeshComponent, name of bone to apply impulse to. 'None' indicates root body.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void AddImpulseAtLocation(FVector Impulse, FVector Location, FName BoneName = NAME_None);

	/**
	 * Add an impulse to all rigid bodies in this component, radiating out from the specified position.
	 *
	 * @param Origin		Point of origin for the radial impulse blast, in world space
	 * @param Radius		Size of radial impulse. Beyond this distance from Origin, there will be no affect.
	 * @param Strength		Maximum strength of impulse applied to body.
	 * @param Falloff		Allows you to control the strength of the impulse as a function of distance from Origin.
	 * @param bVelChange	If true, the Strength is taken as a change in velocity instead of an impulse (ie. mass will have no affect).
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void AddRadialImpulse(FVector Origin, float Radius, float Strength, enum ERadialImpulseFalloff Falloff, bool bVelChange = false);

	/**
	 *	Add a force to a single rigid body.
	 *  This is like a 'thruster'. Good for adding a burst over some (non zero) time. Should be called every frame for the duration of the force.
	 *
	 *	@param	Force		 Force vector to apply. Magnitude indicates strength of force.
	 *	@param	BoneName	 If a SkeletalMeshComponent, name of body to apply force to. 'None' indicates root body.
	 *  @param  bAccelChange If true, Force is taken as a change in acceleration instead of a physical force (i.e. mass will have no affect).
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void AddForce(FVector Force, FName BoneName = NAME_None, bool bAccelChange = false);

	/**
	 *	Add a force to a single rigid body at a particular location.
	 *  This is like a 'thruster'. Good for adding a burst over some (non zero) time. Should be called every frame for the duration of the force.
	 *
	 *	@param Force		Force vector to apply. Magnitude indicates strength of force.
	 *	@param Location		Location to apply force, in world space.
	 *	@param BoneName		If a SkeletalMeshComponent, name of body to apply force to. 'None' indicates root body.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void AddForceAtLocation(FVector Force, FVector Location, FName BoneName = NAME_None);

	/**
	 *	Add a force to all bodies in this component, originating from the supplied world-space location.
	 *
	 *	@param Origin		Origin of force in world space.
	 *	@param Radius		Radius within which to apply the force.
	 *	@param Strength		Strength of force to apply.
	 *  @param Falloff		Allows you to control the strength of the force as a function of distance from Origin.
	 *  @param bAccelChange If true, Strength is taken as a change in acceleration instead of a physical force (i.e. mass will have no affect).
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void AddRadialForce(FVector Origin, float Radius, float Strength, enum ERadialImpulseFalloff Falloff, bool bAccelChange = false);

	/**
	 *	Add a torque to a single rigid body.
	 *	@param Torque		Torque to apply. Direction is axis of rotation and magnitude is strength of torque.
	 *	@param BoneName		If a SkeletalMeshComponent, name of body to apply torque to. 'None' indicates root body.
	 *  @param bAccelChange If true, Torque is taken as a change in angular acceleration instead of a physical torque (i.e. mass will have no affect).
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	void AddTorque(FVector Torque, FName BoneName = NAME_None, bool bAccelChange = false);

	/**
	 *	Set the linear velocity of a single body.
	 *	This should be used cautiously - it may be better to use AddForce or AddImpulse.
	 *
	 *	@param NewVel			New linear velocity to apply to physics.
	 *	@param bAddToCurrent	If true, NewVel is added to the existing velocity of the body.
	 *	@param BoneName			If a SkeletalMeshComponent, name of body to modify velocity of. 'None' indicates root body.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	void SetPhysicsLinearVelocity(FVector NewVel, bool bAddToCurrent = false, FName BoneName = NAME_None);

	/** 
	 *	Get the linear velocity of a single body. 
	 *	@param BoneName			If a SkeletalMeshComponent, name of body to get velocity of. 'None' indicates root body.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")	
	FVector GetPhysicsLinearVelocity(FName BoneName = NAME_None);

	/**
	 *	Set the linear velocity of all bodies in this component.
	 *
	 *	@param NewVel			New linear velocity to apply to physics.
	 *	@param bAddToCurrent	If true, NewVel is added to the existing velocity of the body.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void SetAllPhysicsLinearVelocity(FVector NewVel, bool bAddToCurrent = false);

	/**
	 *	Set the angular velocity of a single body.
	 *	This should be used cautiously - it may be better to use AddTorque or AddImpulse.
	 *
	 *	@param NewAngVel		New angular velocity to apply to body, in degrees per second.
	 *	@param bAddToCurrent	If true, NewAngVel is added to the existing angular velocity of the body.
	 *	@param BoneName			If a SkeletalMeshComponent, name of body to modify angular velocity of. 'None' indicates root body.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	void SetPhysicsAngularVelocity(FVector NewAngVel, bool bAddToCurrent = false, FName BoneName = NAME_None);

	/**
	*	Set the maximum angular velocity of a single body.
	*
	*	@param NewMaxAngVel		New maximum angular velocity to apply to body, in degrees per second.
	*	@param bAddToCurrent	If true, NewMaxAngVel is added to the existing maximum angular velocity of the body.
	*	@param BoneName			If a SkeletalMeshComponent, name of body to modify maximum angular velocity of. 'None' indicates root body.
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	void SetPhysicsMaxAngularVelocity(float NewMaxAngVel, bool bAddToCurrent = false, FName BoneName = NAME_None);

	/** 
	 *	Get the angular velocity of a single body, in degrees per second. 
	 *	@param BoneName			If a SkeletalMeshComponent, name of body to get velocity of. 'None' indicates root body.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")	
	FVector GetPhysicsAngularVelocity(FName BoneName = NAME_None);

	/**
	*	Get the center of mass of a single body. In the case of a welded body this will return the center of mass of the entire welded body (including its parent and children)
	*   Objects that are not simulated return (0,0,0) as they do not have COM
	*	@param BoneName			If a SkeletalMeshComponent, name of body to get center of mass of. 'None' indicates root body.
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	FVector GetCenterOfMass(FName BoneName = NAME_None);

	/**
	 *	'Wake' physics simulation for a single body.
	 *	@param	BoneName	If a SkeletalMeshComponent, name of body to wake. 'None' indicates root body.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void WakeRigidBody(FName BoneName = NAME_None);

	/** 
	 *	Force a single body back to sleep. 
	 *	@param	BoneName	If a SkeletalMeshComponent, name of body to put to sleep. 'None' indicates root body.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	void PutRigidBodyToSleep(FName BoneName = NAME_None);

	/** Changes the value of bNotifyRigidBodyCollision
	 * @param bNewNotifyRigidBodyCollision - The value to assign to bNotifyRigidBodyCollision
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void SetNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision);

	/** Changes the value of bOwnerNoSee. */
	UFUNCTION(BlueprintCallable, Category="Rendering")
	void SetOwnerNoSee(bool bNewOwnerNoSee);
	
	/** Changes the value of bOnlyOwnerSee. */
	UFUNCTION(BlueprintCallable, Category="Rendering")
	void SetOnlyOwnerSee(bool bNewOnlyOwnerSee);

	/** Changes the value of CastShadow. */
	UFUNCTION(BlueprintCallable, Category="Rendering")
	void SetCastShadow(bool NewCastShadow);

	/** Changes the value of TranslucentSortPriority. */
	UFUNCTION(BlueprintCallable, Category="Rendering")
	void SetTranslucentSortPriority(int32 NewTranslucentSortPriority);

	/** Controls what kind of collision is enabled for this body */
	UFUNCTION(BlueprintCallable, Category="Collision")
	virtual void SetCollisionEnabled(ECollisionEnabled::Type NewType);

	/**  
	 * Set Collision Profile Name
	 * This function is called by constructors when they set ProfileName
	 * This will change current CollisionProfileName to be this, and overwrite Collision Setting
	 * 
	 * @param InCollisionProfileName : New Profile Name
	 */
	UFUNCTION(BlueprintCallable, Category="Collision")	
	virtual void SetCollisionProfileName(FName InCollisionProfileName);

	/** Get the collision profile name */
	UFUNCTION(BlueprintCallable, Category="Collision")
	FName GetCollisionProfileName();

	/**
	 *	Changes the collision channel that this object uses when it moves
	 *	@param      Channel     The new channel for this component to use
	 */
	UFUNCTION(BlueprintCallable, Category="Collision")	
	void SetCollisionObjectType(ECollisionChannel Channel);

	/** Perform a line trace against a single component */
	UFUNCTION(BlueprintCallable, Category="Collision", meta=(DisplayName = "Line Trace Component", bTraceComplex="true"))	
	bool K2_LineTraceComponent(FVector TraceStart, FVector TraceEnd, bool bTraceComplex, bool bShowTrace, FVector& HitLocation, FVector& HitNormal, FName& BoneName);

	/** Sets the bRenderCustomDepth property and marks the render state dirty. */
	UFUNCTION(BlueprintCallable, Category="Rendering")
	void SetRenderCustomDepth(bool bValue);

	/** Sets bRenderInMainPass property and marks the render state dirty. */
	UFUNCTION(BlueprintCallable, Category = "Rendering")
	void SetRenderInMainPass(bool bValue);

public:
	static int32 CurrentTag;

	/** The primitive's scene info. */
	FPrimitiveSceneProxy* SceneProxy;
	
	/** A fence to track when the primitive is detached from the scene in the rendering thread. */
	FRenderCommandFence DetachFence;

	/**
	 * Incremented by the main thread before being attached to the scene, decremented
	 * by the rendering thread after removal. This counter exists to assert that 
	 * operations are safe in order to help avoid race conditions.
	 *
	 *           *** Runtime logic should NEVER rely on this value. ***
	 *
	 * The only safe assertions to make are:
	 *
	 *     AttachmentCounter == 0: The primitive is not exposed to the rendering
	 *                             thread, it is safe to modify shared members.
	 *                             This assertion is valid ONLY from the main thread.
	 *
	 *     AttachmentCounter >= 1: The primitive IS exposed to the rendering
	 *                             thread and therefore shared members must not
	 *                             be modified. This assertion may be made from
	 *                             any thread. Note that it is valid and expected
	 *                             for AttachmentCounter to be larger than 1, e.g.
	 *                             during reattachment.
	 */
	FThreadSafeCounter AttachmentCounter;

	// Scene data
private:
	/** LOD parent primitive to draw instead of this one (multiple UPrim's will point to the same LODParent ) */
	UPROPERTY(duplicatetransient)
	class UPrimitiveComponent* LODParentPrimitive;

public:
	void SetLODParentPrimitive(UPrimitiveComponent * InLODParentPrimitive);
	UPrimitiveComponent* GetLODParentPrimitive();

#if WITH_EDITOR
	virtual const int32 GetNumUncachedStaticLightingInteractions() const override; // recursive function
	/** This function is used to create hierarchical LOD for the level. You can decide to opt out if you don't want. */
	virtual const bool ShouldGenerateAutoLOD() const;
#endif

	// Begin UActorComponent Interface
	virtual void InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting, bool bTranslationOnly) override;
	virtual bool IsEditorOnly() const override;
	virtual bool ShouldCreatePhysicsState() const override;
	virtual bool HasValidPhysicsState() const override;
	virtual class FActorComponentInstanceData* GetComponentInstanceData() const override;
	virtual FName GetComponentInstanceDataType() const override;
	// End UActorComponent Interface

	/** @return true if the owner is selected and this component is selectable */
	virtual bool ShouldRenderSelected() const;

	/** Component is directly selected in the editor separate from its parent actor */
	bool IsComponentIndividuallySelected() const;

	/**  @return True if a primitive's parameters as well as its position is static during gameplay, and can thus use static lighting. */
	bool HasStaticLighting() const;

	virtual bool HasValidSettingsForStaticLighting() const 
	{
		return HasStaticLighting();
	}

	/**  @return true if only unlit materials are used for rendering, false otherwise. */
	virtual bool UsesOnlyUnlitMaterials() const;

	/**
	 * Returns the lightmap resolution used for this primitive instance in the case of it supporting texture light/ shadow maps.
	 * 0 if not supported or no static shadowing.
	 *
	 * @param	Width	[out]	Width of light/shadow map
	 * @param	Height	[out]	Height of light/shadow map
	 * @return	bool			true if LightMap values are padded, false if not
	 */
	virtual bool GetLightMapResolution( int32& Width, int32& Height ) const;

	/**
	 *	Returns the static lightmap resolution used for this primitive.
	 *	0 if not supported or no static shadowing.
	 *
	 * @return	int32		The StaticLightmapResolution for the component
	 */
	virtual int32 GetStaticLightMapResolution() const { return 0; }

	/**
	 * Returns the light and shadow map memory for this primitive in its out variables.
	 *
	 * Shadow map memory usage is per light whereof lightmap data is independent of number of lights, assuming at least one.
	 *
	 * @param [out] LightMapMemoryUsage		Memory usage in bytes for light map (either texel or vertex) data
	 * @param [out]	ShadowMapMemoryUsage	Memory usage in bytes for shadow map (either texel or vertex) data
	 */
	virtual void GetLightAndShadowMapMemoryUsage( int32& LightMapMemoryUsage, int32& ShadowMapMemoryUsage ) const;


#if WITH_EDITOR
	/**
	 * Requests the information about the component that the static lighting system needs.
	 * @param OutPrimitiveInfo - Upon return, contains the component's static lighting information.
	 * @param InRelevantLights - The lights relevant to the primitive.
	 * @param InOptions - The options for the static lighting build.
	 */
	virtual void GetStaticLightingInfo(struct FStaticLightingPrimitiveInfo& OutPrimitiveInfo,const TArray<class ULightComponent*>& InRelevantLights,const class FLightingBuildOptions& Options) {}
#endif
	/**
	 *	Requests whether the component will use texture, vertex or no lightmaps.
	 *
	 *	@return	ELightMapInteractionType		The type of lightmap interaction the component will use.
	 */
	virtual ELightMapInteractionType GetStaticLightingType() const	{ return LMIT_None;	}

	/**
	 * Enumerates the streaming textures used by the primitive.
	 * @param OutStreamingTextures - Upon return, contains a list of the streaming textures used by the primitive.
	 */
	virtual void GetStreamingTextureInfo(TArray<struct FStreamingTexturePrimitiveInfo>& OutStreamingTextures) const
	{
	}

	/**
	 * Call GetStreamingTextureInfo and remove the elements with a NULL texture
	 * @param OutStreamingTextures - Upon return, contains a list of the non-null streaming textures used by the primitive.
	 */
	void GetStreamingTextureInfoWithNULLRemoval(TArray<struct FStreamingTexturePrimitiveInfo>& OutStreamingTextures) const;

	/**
	 * Determines the DPG the primitive's primary elements are drawn in.
	 * Even if the primitive's elements are drawn in multiple DPGs, a primary DPG is needed for occlusion culling and shadow projection.
	 * @return The DPG the primitive's primary elements will be drawn in.
	 */
	virtual uint8 GetStaticDepthPriorityGroup() const
	{
		return DepthPriorityGroup;
	}

	/** 
	 * Retrieves the materials used in this component 
	 * 
	 * @param OutMaterials	The list of used materials.
	 */
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials) const {}

	/**
	 * Returns the material textures used to render this primitive for the given platform.
	 * Internally calls GetUsedMaterials() and GetUsedTextures() for each material.
	 *
	 * @param OutTextures	[out] The list of used textures.
	 */
	virtual void GetUsedTextures(TArray<UTexture*>& OutTextures, EMaterialQualityLevel::Type QualityLevel);


	/** Tick function for physics ticking **/
	UPROPERTY()
	struct FPrimitiveComponentPostPhysicsTickFunction PostPhysicsComponentTick;

	/** Controls if we get a post physics tick or not. If set during ticking, will take effect next frame **/
	void SetPostPhysicsComponentTickEnabled(bool bEnable);

	/** Returns whether we have the post physics tick enabled **/
	bool IsPostPhysicsComponentTickEnabled() const;

	/** Tick function called after physics (sync scene) has finished simulation */
	virtual void PostPhysicsTick(FPrimitiveComponentPostPhysicsTickFunction &ThisTickFunction) {}

	/** Return the BodySetup to use for this PrimitiveComponent (single body case) */
	virtual class UBodySetup* GetBodySetup() { return NULL; }



	/** Move this component to match the physics rigid body pose. Note, a warning will be generated if you call this function on a component that is attached to something */
	void SyncComponentToRBPhysics();
	
	/** 
	 *	Returns the matrix that should be used to render this component. 
	 *	Allows component class to perform graphical distortion to the component not supported by an FTransform 
	 */
	virtual FMatrix GetRenderMatrix() const;

	/** @return number of material elements in this primitive */
	UFUNCTION(BlueprintCallable, Category="Rendering|Material")
	virtual int32 GetNumMaterials() const;
	
	/** Get a BodyInstance from this component. The supplied name is used in the SkeletalMeshComponent case. A name of NAME_None in the skeletal case gives the root body instance. */


	/**
	* returns BodyInstance of the component.
	*
	* @param BoneName				Used to get body associated with specific bone. NAME_None automatically gets the root most body
	* @param bGetWelded				If the component has been welded to another component and bGetWelded is true we return the single welded BodyInstance that is used in the simulation
	*
	* @return		Returns the BodyInstance based on various states (does component have multiple bodies? Is the body welded to another body?)
	*/

	virtual FBodyInstance* GetBodyInstance(FName BoneName = NAME_None, bool bGetWelded = true) const;

	/** 
	 * returns Distance to closest Body Instance surface. 
	 *
	 * @param Point				World 3D vector
	 * @param OutPointOnBody	Point on the surface of collision closest to Point
	 * 
	 * @return		Success if returns > 0.f, if returns 0.f, it is either not convex or inside of the point
	 *				If returns < 0.f, this primitive does not have collsion
	 */
	virtual float GetDistanceToCollision(const FVector& Point, FVector& ClosestPointOnCollision) const;

	/**
	 * Creates a proxy to represent the primitive to the scene manager in the rendering thread.
	 * @return The proxy object.
	 */
	virtual FPrimitiveSceneProxy* CreateSceneProxy()
	{
		return NULL;
	}

	/**
	 * Determines whether the proxy for this primitive type needs to be recreated whenever the primitive moves.
	 * @return true to recreate the proxy when UpdateTransform is called.
	 */
	virtual bool ShouldRecreateProxyOnUpdateTransform() const
	{
		return false;
	}

	/** 
	 * This isn't bound extent, but for shape component to utilize extent is 0. 
	 * For normal primitive, this is 0, for ShapeComponent, this will have valid information
	 */
	virtual bool IsZeroExtent() const
	{
		return false;
	}

	/** Event called when a component is 'damaged', allowing for component class specific behaviour */
	virtual void ReceiveComponentDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser);

	/**
	*   Welds this component to another scene component, optionally at a named socket. Component is automatically attached if not already
	*	Welding allows the child physics object to become physically connected to its parent. This is useful for creating compound rigid bodies with correct mass distribution.
	*   @param InParent the component to be physically attached to
	*   @param InSocketName optional socket to attach component to
	*/
	virtual void WeldTo(class USceneComponent* InParent, FName InSocketName = NAME_None);

	/**
	*	Does the actual work for welding.
	*	@return true if did a true weld of shapes, meaning body initialization is not needed
	*/
	virtual bool WeldToImplementation(USceneComponent * InParent, FName ParentSocketName = NAME_None, bool bWeldSimulatedChild = true);

	/**
	*   UnWelds this component from its parent component. Attachment is maintained (DetachFromParent automatically unwelds)
	*/
	virtual void UnWeldFromParent();

	/**
	*   Unwelds the children of this component. Attachment is maintained
	*/
	virtual void UnWeldChildren();

	/**
	*	Adds the bodies that are currently welded to the OutWeldedBodies array 
	*/
	virtual void GetWeldedBodies(TArray<FBodyInstance*> & OutWeldedBodies, TArray<FName> & OutLabels);

#if WITH_EDITOR
	/**
	 * Determines whether the supplied bounding box intersects with the component.
	 * Used by the editor in orthographic viewports.
	 *
	 * @param	InSelBBox						Bounding box to test against
	 * @param	ShowFlags						Engine ShowFlags for the viewport
	 * @param	bConsiderOnlyBSP				If only BSP geometry should be tested
	 * @param	bMustEncompassEntireComponent	Whether the component bounding box must lay wholly within the supplied bounding box
	 *
	 * @return	true if the supplied bounding box is determined to intersect the component (partially or wholly)
	 */
	virtual bool ComponentIsTouchingSelectionBox(const FBox& InSelBBox, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const;

	/**
	 * Determines whether the supplied frustum intersects with the component.
	 * Used by the editor in perspective viewports.
	 *
	 * @param	InFrustum						Frustum to test against
	 * @param	ShowFlags						Engine ShowFlags for the viewport
	 * @param	bConsiderOnlyBSP				If only BSP geometry should be tested
	 * @param	bMustEncompassEntireComponent	Whether the component bounding box must lay wholly within the supplied bounding box
	 *
	 * @return	true if the supplied bounding box is determined to intersect the component (partially or wholly)
	 */
	virtual bool ComponentIsTouchingSelectionFrustum(const FConvexVolume& InFrustum, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const;
#endif

protected:

	/** Give the static mesh component recreate render state context access to Create/DestroyRenderState_Concurrent(). */
	friend class FStaticMeshComponentRecreateRenderStateContext;

	// Begin USceneComponent Interface
	virtual void OnUpdateTransform(bool bSkipPhysicsMove) override;

	/** Event called when AttachParent changes, to allow the scene to update its attachment state. */
	virtual void OnAttachmentChanged() override;

	/**
	* Called after a child is attached to this component.
	* Note: Do not change the attachment state of the child during this call.
	*/
	virtual void OnChildAttached(USceneComponent* ChildComponent) override;

	/** Whether the component type supports static lighting. */
	virtual bool SupportsStaticLighting() const 
	{
		return false;
	}

public:
	virtual bool IsSimulatingPhysics(FName BoneName = NAME_None) const override;

	// End USceneComponentInterface


	// Begin UActorComponent Interface
protected:
	virtual void CreateRenderState_Concurrent() override;
	virtual void SendRenderTransform_Concurrent() override;
	virtual void OnRegister()  override;
	virtual void OnUnregister()  override;
	virtual void DestroyRenderState_Concurrent() override;
	virtual void CreatePhysicsState() override;
	virtual void DestroyPhysicsState() override;
	virtual void OnActorEnableCollisionChanged() override;
	/**
	 * Called to get the Component To World Transform from the Root BodyInstance
	 * This needs to be virtual since SkeletalMeshComponent Root has to undo its own transform
	 * Without this, the root LocalToAtom is overriden by physics simulation, causing kinematic velocity to 
	 * accelerate simulation
	 *
	 * @param : UseBI - root body instsance
	 * @return : New ComponentToWorld to use
	 */
	virtual FTransform GetComponentTransformFromBodyInstance(FBodyInstance* UseBI);
public:
	virtual void RegisterComponentTickFunctions(bool bRegister) override;
#if WITH_EDITOR
	virtual void CheckForErrors() override;
#endif // WITH_EDITOR	
	// End UActorComponent Interface

protected:
	/** Internal function that updates physics objects to match the component collision settings. */
	virtual void UpdatePhysicsToRBChannels();

	/** Called to send a transform update for this component to the physics engine */
	void SendPhysicsTransform(bool bTeleport);

	/** Ensure physics state created **/
	void EnsurePhysicsStateCreated();
public:

	// Begin UObject interface.
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const UProperty* InProperty) const override;
	virtual void UpdateCollisionProfile();
#endif // WITH_EDITOR
	virtual void PostLoad() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual SIZE_T GetResourceSize(EResourceSizeMode::Type Mode) override;

	virtual void MarkAsEditorOnlySubobject() override
	{
		AlwaysLoadOnClient = false;
		AlwaysLoadOnServer = false;
	}

#if WITH_EDITOR
	/**
	 * Called after importing property values for this object (paste, duplicate or .t3d import)
	 * Allow the object to perform any cleanup for properties which shouldn't be duplicated or
	 * are unsupported by the script serialization
	 */
	virtual void PostEditImport() override;
#endif

	virtual void BeginDestroy() override;
	virtual void FinishDestroy() override;
	virtual bool IsReadyForFinishDestroy() override;
	virtual bool NeedsLoadForClient() const override;
	virtual bool NeedsLoadForServer() const override;
	// End UObject interface.

	//Begin USceneComponent Interface

protected:
	virtual bool MoveComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit = NULL, EMoveComponentFlags MoveFlags = MOVECOMP_NoFlags) override;
	
public:
	virtual bool IsWorldGeometry() const override;
	virtual ECollisionEnabled::Type GetCollisionEnabled() const override;
	virtual ECollisionResponse GetCollisionResponseToChannel(ECollisionChannel Channel) const override;
	virtual ECollisionChannel GetCollisionObjectType() const override;
	virtual const FCollisionResponseContainer& GetCollisionResponseToChannels() const override;
	virtual FVector GetComponentVelocity() const override;
	//End USceneComponent Interface

	/**
	 * Dispatch notifications for the given HitResult.
	 *
	 * @param Owner: AActor that owns this component
	 * @param BlockingHit: FHitResult that generated the blocking hit.
	 */
	void DispatchBlockingHit(AActor& Owner, FHitResult const& BlockingHit);

	/**
	 * Set collision params on OutParams (such as CollisionResponse, bTraceAsyncScene) to match the settings on this PrimitiveComponent.
	 */
	virtual void InitSweepCollisionParams(FCollisionQueryParams &OutParams, FCollisionResponseParams& OutResponseParam) const;

	/**
	 * Return a CollisionShape that most closely matches this primitive.
	 */
	virtual struct FCollisionShape GetCollisionShape(float Inflation = 0.0f) const;

	/**
	 * Returns true if the given transforms result in the same bounds, due to rotational symmetry.
	 * For example, this is true for a sphere with uniform scale undergoing any rotation.
	 * This is NOT intended to detect every case where this is true, only the common cases to aid optimizations.
	 */
	virtual bool AreSymmetricRotations(const FQuat& A, const FQuat& B, const FVector& Scale3D) const { return A.Equals(B); }

	/**
	 * Pushes new selection state to the render thread primitive proxy
	 */
	void PushSelectionToProxy();

	/**
	 * Pushes new hover state to the render thread primitive proxy
	 * @param bInHovered - true if the proxy should display as if hovered
	 */
	void PushHoveredToProxy(const bool bInHovered);

	/** Sends editor visibility updates to the render thread */
	void PushEditorVisibilityToProxy( uint64 InVisibility );

	/** Gets the emissive boost for the primitive component. */
	virtual float GetEmissiveBoost(int32 ElementIndex) const		{ return 1.0f; };

	/** Gets the diffuse boost for the primitive component. */
	virtual float GetDiffuseBoost(int32 ElementIndex) const		{ return 1.0f; };
	
	virtual bool GetShadowIndirectOnly() const { return false; }

	/**
	 *	Set the angular velocity of all bodies in this component.
	 *
	 *	@param NewAngVel		New angular velocity to apply to physics, in degrees per second.
	 *	@param bAddToCurrent	If true, NewAngVel is added to the existing angular velocity of all bodies.
	 */
	virtual void SetAllPhysicsAngularVelocity(const FVector& NewAngVel, bool bAddToCurrent = false);

	/**
	 *	Set the position of all bodies in this component.
	 *	If a SkeletalMeshComponent, the root body will be placed at the desired position, and the same delta is applied to all other bodies.
	 *
	 *	@param	NewPos		New position for the body
	 */
	virtual void SetAllPhysicsPosition(FVector NewPos);
	
	/**
	 *	Set the rotation of all bodies in this component.
	 *	If a SkeletalMeshComponent, the root body will be changed to the desired orientation, and the same delta is applied to all other bodies.
	 *
	 *	@param NewRot	New orienatation for the body
	 */
	virtual void SetAllPhysicsRotation(FRotator NewRot);
	
	/**
	 *	Ensure simulation is running for all bodies in this component.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void WakeAllRigidBodies();
	
	/** Enables/disables whether this component is affected by gravity. This applies only to components with bSimulatePhysics set to true. */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void SetEnableGravity(bool bGravityEnabled);

	/** Returns whether this component is affected by gravity. Returns always false if the component is not simulated. */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual bool IsGravityEnabled() const;

	/** Sets the linear damping of this component. */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void SetLinearDamping(float InDamping);

	/** Returns the linear damping of this component. */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual float GetLinearDamping() const;

	/** Sets the angular damping of this component. */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void SetAngularDamping(float InDamping);
	
	/** Returns the angular damping of this component. */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual float GetAngularDamping() const;

	/** Change the mass scale used to calculate the mass of a single physics body */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void SetMassScale(FName BoneName = NAME_None, float InMassScale = 1.f);

	/** Change the mass scale used fo all bodies in this component */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual void SetAllMassScale(float InMassScale = 1.f);

	/** Returns the mass of this component in kg. */
	UFUNCTION(BlueprintCallable, Category="Physics")
	virtual float GetMass() const;

	/** Returns the inertia tensor of this component in kg cm^2. The inertia tensor is in local component space.*/
	UFUNCTION(BlueprintCallable, Category = "Physics", meta =(Keywords = "physics moment of inertia tensor MOI"))
	virtual FVector GetInertiaTensor(FName BoneName = NAME_None) const;

	/** Scales the given vector by the world space moment of inertia. Useful for computing the torque needed to rotate an object.*/
	UFUNCTION(BlueprintCallable, Category = "Physics", meta = (Keywords = "physics moment of inertia tensor MOI"))
	virtual FVector ScaleByMomentOfInertia(FVector InputVector, FName BoneName = NAME_None) const;

	/** Returns the calculated mass in kg. This is not 100% exactly the mass physx will calculate, but it is very close ( difference < 0.1kg ). */
	virtual float CalculateMass(FName BoneName = NAME_None);

	/**
	 *	Force all bodies in this component to sleep.
	 */
	virtual void PutAllRigidBodiesToSleep();
	
	/**
	 *	Returns if a single body is currently awake and simulating.
	 *	@param	BoneName	If a SkeletalMeshComponent, name of body to return wakeful state from. 'None' indicates root body.
	 */
	bool RigidBodyIsAwake(FName BoneName = NAME_None);
	
	/**
	 *	Returns if any body in this component is currently awake and simulating.
	 */
	virtual bool IsAnyRigidBodyAwake();
	
	/**
	 *	Changes a member of the ResponseToChannels container for this PrimitiveComponent.
	 *
	 * @param       Channel      The channel to change the response of
	 * @param       NewResponse  What the new response should be to the supplied Channel
	 */
	UFUNCTION(BlueprintCallable, Category="Collision")
	void SetCollisionResponseToChannel(ECollisionChannel Channel, ECollisionResponse NewResponse);
	
	/**
	 *	Changes all ResponseToChannels container for this PrimitiveComponent. to be NewResponse
	 *
	 * @param       NewResponse  What the new response should be to the supplied Channel
	 */
	UFUNCTION(BlueprintCallable, Category="Collision")
	void SetCollisionResponseToAllChannels(ECollisionResponse NewResponse);
	
	/**
	 *	Changes the whole ResponseToChannels container for this PrimitiveComponent.
	 *
	 * @param       NewResponses  New set of responses for this component
	 */
	void SetCollisionResponseToChannels(const FCollisionResponseContainer& NewReponses);
	
private:
	/** Called when the BodyInstance ResponseToChannels, CollisionEnabled or bNotifyRigidBodyCollision changes, in case subclasses want to use that information. */
	virtual void OnComponentCollisionSettingsChanged();

	/**
	 *	Applies a RigidBodyState struct to this Actor.
	 *	When we get an update for the physics, we try to do it smoothly if it is less than ..DeltaThreshold.
	 *	We directly fix ..InterpAlpha * error. The rest is fixed by altering the velocity to correct the actor over 1.0/..RecipFixTime seconds.
	 *	So if ..InterpAlpha is 1, we will always just move the actor directly to its correct position (as it the error was over ..DeltaThreshold)
	 *	If ..InterpAlpha is 0, we will correct just by changing the velocity.
	 *
	 * Returns true if restored state is matching requested one (no velocity corrections required)
	 */
	bool ApplyRigidBodyState(const FRigidBodyState& NewState, const FRigidBodyErrorCorrection& ErrorCorrection, FVector& OutDeltaPos, FName BoneName = NAME_None);

	/** Check if mobility is set to non-static. If BodyInstanceRequiresSimulation is non-null we check that it is simulated. Triggers a PIE warning if conditions fails */
	void WarnInvalidPhysicsOperations_Internal(const FText& ActionText, const FBodyInstance* BodyInstanceRequiresSimulation = nullptr) const;

public:

	/**
	 * Applies RigidBodyState only if it needs to be updated
	 * NeedsUpdate flag will be removed from UpdatedState after all velocity corrections are finished
	 */
	bool ConditionalApplyRigidBodyState(FRigidBodyState& UpdatedState, const FRigidBodyErrorCorrection& ErrorCorrection, FVector& OutDeltaPos, FName BoneName = NAME_None);

	/** 
	 *	Get the state of the rigid body responsible for this Actor's physics, and fill in the supplied FRigidBodyState struct based on it.
	 *
	 *	@return	true if we successfully found a physics-engine body and update the state structure from it.
	 */
	bool GetRigidBodyState(FRigidBodyState& OutState, FName BoneName = NAME_None);

	/** 
	 *	Changes the current PhysMaterialOverride for this component. 
	 *	Note that if physics is already running on this component, this will _not_ alter its mass/inertia etc,  
	 *	it will only change its surface properties like friction.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics", meta=(DisplayName="Set PhysicalMaterial Override"))
	virtual void SetPhysMaterialOverride(class UPhysicalMaterial* NewPhysMaterial);

	/** 
	 *  Looking at various values of the component, determines if this
	 *  component should be added to the scene
	 * @return true if the component is visible and should be added to the scene, false otherwise
	 */
	bool ShouldComponentAddToScene() const;
	
	/**
	 * Changes the value of CullDistance.
	 * @param NewCullDistance - The value to assign to CullDistance.
	 */
	UFUNCTION(BlueprintCallable, Category="LOD", meta=(DisplayName="Set Max Draw Distance"))
	void SetCullDistance(float NewCullDistance);
	
	/**
	 * Utility to cache the max draw distance based on cull distance volumes or the desired max draw distance
	 */
	void SetCachedMaxDrawDistance(const float NewCachedMaxDrawDistance);

	/**
	 * Changes the value of DepthPriorityGroup.
	 * @param NewDepthPriorityGroup - The value to assign to DepthPriorityGroup.
	 */
	void SetDepthPriorityGroup(ESceneDepthPriorityGroup NewDepthPriorityGroup);
	
	/**
	 * Changes the value of bUseViewOwnerDepthPriorityGroup and ViewOwnerDepthPriorityGroup.
	 * @param bNewUseViewOwnerDepthPriorityGroup - The value to assign to bUseViewOwnerDepthPriorityGroup.
	 * @param NewViewOwnerDepthPriorityGroup - The value to assign to ViewOwnerDepthPriorityGroup.
	 */
	void SetViewOwnerDepthPriorityGroup(
		bool bNewUseViewOwnerDepthPriorityGroup,
		ESceneDepthPriorityGroup NewViewOwnerDepthPriorityGroup
		);
	
	/** 
	 *  Trace a ray against just this component.
	 *  @param  OutHit          Information about hit against this component, if true is returned
	 *  @param  Start           Start location of the ray
	 *  @param  End             End location of the ray
	 *  @param  Params          Additional parameters used for the trace
	 *  @return true if a hit is found
	 */
	virtual bool LineTraceComponent( FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params );
	
	/** 
	 *  Trace a box against just this component.
	 *  @param  OutHit          Information about hit against this component, if true is returned
	 *  @param  Start           Start location of the box
	 *  @param  End             End location of the box
	 *  @param  BoxHalfExtent 	Half Extent of the box
	 *	@param	bTraceComplex	Whether or not to trace complex
	 *  @return true if a hit is found
	 */
	virtual bool SweepComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionShape &CollisionShape, bool bTraceComplex=false);
	
	/** 
	 *  Test the collision of the supplied component at the supplied location/rotation, and determine if it overlaps this component
	 *  @note This overload taking rotation as a FQuat is slightly faster than the version using FRotator.
	 *  @note This simply calls the virtual ComponentOverlapComponentImpl() which can be overridden to implement custom behavior.
	 *  @param  PrimComp        Component to use geometry from to test against this component. Transform of this component is ignored.
	 *  @param  Pos             Location to place PrimComp geometry at 
	 *  @param  Rot             Rotation to place PrimComp geometry at 
	 *  @param	Params			Parameter for trace. TraceTag is only used.
	 *  @return true if PrimComp overlaps this component at the specified location/rotation
	 */
	bool ComponentOverlapComponent(class UPrimitiveComponent* PrimComp, const FVector Pos, const FQuat& Rot, const FCollisionQueryParams& Params);
	bool ComponentOverlapComponent(class UPrimitiveComponent* PrimComp, const FVector Pos, const FRotator Rot, const FCollisionQueryParams& Params);

protected:

	// Override this method for custom behavior.
	virtual bool ComponentOverlapComponentImpl(class UPrimitiveComponent* PrimComp, const FVector Pos, const FQuat& Rot, const FCollisionQueryParams& Params);

public:
	
	/** 
	 *  Test the collision of the supplied Sphere at the supplied location, and determine if it overlaps this component
	 *
	 *  @param  Pos             Location to place PrimComp geometry at 
	 *	@param	Rot				Rotation of PrimComp geometry
	 *  @param  CollisionShape 	Shape of collision of PrimComp geometry
	 *  @return true if PrimComp overlaps this component at the specified location/rotation
	 */
	virtual bool OverlapComponent(const FVector& Pos, const FQuat& Rot, const FCollisionShape& CollisionShape);

	/**
	 * Computes the minimum translation direction (MTD) when an overlap exists between the component and the given shape.
	 * @param OutMTD			Outputs the MTD to move CollisionShape out of penetration
	 * @param CollisionShape	Shape information for the geometry testing against
	 * @param Pos				Location of collision shape
	 * @param Rot				Rotation of collision shape
	 * @return true if the computation succeeded - assumes that there is an overlap at the specified position/rotation
	 */

	virtual bool ComputePenetration(FMTDResult & OutMTD, const FCollisionShape& CollisionShape, const FVector& Pos, const FQuat& Rot);
	
	/**
	 * Return true if the given Pawn can step up onto this component.
	 * @param APawn is the pawn that wants to step onto this component.
	 */
	DEPRECATED(4.3, "UPrimitiveComponent::CanBeBaseForCharacter() is deprecated, use CanCharacterStepUp() instead.")
	virtual bool CanBeBaseForCharacter(class APawn* Pawn) const
	{
		return CanCharacterStepUp(Pawn);
	}

	/**
	 * Return true if the given Pawn can step up onto this component.
	 * @param Pawn is the Pawn that wants to step onto this component.
	 */
	virtual bool CanCharacterStepUp(class APawn* Pawn) const;

	/** Can this component potentially influence navigation */
	bool CanEverAffectNavigation() const
	{
		return bCanEverAffectNavigation;
	}

	/** set value of bCanEverAffectNavigation flag and update navigation octree if needed */
	void SetCanEverAffectNavigation(bool bRelevant);
	
protected:
	/** Makes sure navigation system has up to date information regarding component's navigation relevancy and if it can affect navigation at all */
	void HandleCanEverAffectNavigationChange();

public:
	DEPRECATED(4.5, "UPrimitiveComponent::DisableNavigationRelevance() is deprecated, use SetCanEverAffectNavigation() instead.")
	void DisableNavigationRelevance()
	{
		SetCanEverAffectNavigation(false);
	}

	// Begin INavRelevantInterface Interface
	virtual FBox GetNavigationBounds() const override;
	virtual bool IsNavigationRelevant() const override;
	// End INavRelevantInterface Interface

	FORCEINLINE EHasCustomNavigableGeometry::Type HasCustomNavigableGeometry() const { return bHasCustomNavigableGeometry; }

	void SetCustomNavigableGeometry(const EHasCustomNavigableGeometry::Type InType);

	/** Collects custom navigable geometry of component.
	 *	@return true if regular navigable geometry exporting should be run as well */
	DEPRECATED(4.8, "UPrimitiveComponent::DoCustomNavigableGeometryExport(FNavigableGeometryExport* GeomExport) is deprecated, use UPrimitiveComponent::DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) instead (takes ref instead of a pointer)")
	virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport* GeomExport) const { return DoCustomNavigableGeometryExport(*GeomExport); }
	/** Collects custom navigable geometry of component.
	*	@return true if regular navigable geometry exporting should be run as well */
	virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const { return true; }

	static void DispatchMouseOverEvents(UPrimitiveComponent* CurrentComponent, UPrimitiveComponent* NewComponent);
	static void DispatchTouchOverEvents(ETouchIndex::Type FingerIndex, UPrimitiveComponent* CurrentComponent, UPrimitiveComponent* NewComponent);
	void DispatchOnClicked();
	void DispatchOnReleased();
	void DispatchOnInputTouchBegin(const ETouchIndex::Type Key);
	void DispatchOnInputTouchEnd(const ETouchIndex::Type Key);
};

/** 
 *  Component instance cached data base class for primitive components. 
 *  Stores a list of instance components attached to the 
 */
class ENGINE_API FPrimitiveComponentInstanceData : public FSceneComponentInstanceData
{
public:
	FPrimitiveComponentInstanceData(const UPrimitiveComponent* SourceComponent);
			
	virtual ~FPrimitiveComponentInstanceData()
	{}

	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override;

	bool ContainsData() const;
};


//////////////////////////////////////////////////////////////////////////
// PrimitiveComponent inlines

FORCEINLINE_DEBUGGABLE bool UPrimitiveComponent::ComponentOverlapMulti(TArray<struct FOverlapResult>& OutOverlaps, const class UWorld* World, const FVector& Pos, const FQuat& Rot, ECollisionChannel TestChannel, const struct FComponentQueryParams& Params, const struct FCollisionObjectQueryParams& ObjectQueryParams) const
{
	return ComponentOverlapMultiImpl(OutOverlaps, World, Pos, Rot, TestChannel, Params, ObjectQueryParams);
}

FORCEINLINE_DEBUGGABLE bool UPrimitiveComponent::ComponentOverlapMulti(TArray<struct FOverlapResult>& OutOverlaps, const class UWorld* World, const FVector& Pos, const FRotator& Rot, ECollisionChannel TestChannel, const struct FComponentQueryParams& Params, const struct FCollisionObjectQueryParams& ObjectQueryParams) const
{
	return ComponentOverlapMultiImpl(OutOverlaps, World, Pos, Rot.Quaternion(), TestChannel, Params, ObjectQueryParams);
}

FORCEINLINE_DEBUGGABLE bool UPrimitiveComponent::ComponentOverlapComponent(class UPrimitiveComponent* PrimComp, const FVector Pos, const FQuat& Rot, const FCollisionQueryParams& Params)
{
	return ComponentOverlapComponentImpl(PrimComp, Pos, Rot, Params);
}

FORCEINLINE_DEBUGGABLE bool UPrimitiveComponent::ComponentOverlapComponent(class UPrimitiveComponent* PrimComp, const FVector Pos, const FRotator Rot, const FCollisionQueryParams& Params)
{
	return ComponentOverlapComponentImpl(PrimComp, Pos, Rot.Quaternion(), Params);
}
