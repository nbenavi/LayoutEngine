// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DestructibleComponent.cpp: UDestructibleComponent methods.
=============================================================================*/


#include "EnginePrivate.h"
#include "PhysicsPublic.h"
#include "PhysicsEngine/DestructibleActor.h"
#include "PhysicsEngine/PhysXSupport.h"
#include "Collision/PhysXCollision.h"
#include "ParticleDefinitions.h"
#include "ObjectEditorUtils.h"
#include "Particles/ParticleSystemComponent.h"
#include "PhysicsEngine/DestructibleActor.h"
#include "Engine/DestructibleMesh.h"
#include "Components/DestructibleComponent.h"
#include "NavigationSystemHelpers.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"

UDestructibleComponent::UDestructibleComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_PHYSX
	, PhysxUserData(this)
#endif
{

	PrimaryComponentTick.bCanEverTick = false;
	PostPhysicsComponentTick.bCanEverTick = false;

	bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::EvenIfNotCollidable;

	BodyInstance.SetUseAsyncScene(true);
	static FName CollisionProfileName(TEXT("Destructible"));
	SetCollisionProfileName(CollisionProfileName);

	bAlwaysCreatePhysicsState = true;
	bIsActive = true;
	bMultiBodyOverlap = true;

	LargeChunkThreshold = 25.f;

	SetSpaceBaseDoubleBuffering(false);
}

void UDestructibleComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

#if WITH_EDITORONLY_DATA
	if(Ar.IsLoading())
	{
		// Copy our skeletal mesh value to our transient variable, so it appears in slate correctly.
		this->DestructibleMesh = GetDestructibleMesh();
	}
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
void UDestructibleComponent::PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent )
{
	static const FName NAME_DestructibleComponent = FName(TEXT("DestructibleComponent"));
	static const FName NAME_DestructibleMesh = FName(TEXT("DestructibleMesh"));

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != NULL)
	{
		if ((FObjectEditorUtils::GetCategoryFName(PropertyChangedEvent.Property) == NAME_DestructibleComponent)
		&&  (PropertyChangedEvent.Property->GetFName() == NAME_DestructibleMesh))
		{
			// If our transient mesh has changed, update our skeletal mesh.
			SetSkeletalMesh( this->DestructibleMesh );
		}
	}
}
#endif // WITH_EDITOR

FBoxSphereBounds UDestructibleComponent::CalcBounds(const FTransform& LocalToWorld) const
{
#if WITH_APEX
	if( ApexDestructibleActor == NULL )
	{
		// Fallback if we don't have physics
		return Super::CalcBounds(LocalToWorld);
	}

	const PxBounds3& PBounds = ApexDestructibleActor->getBounds();

	return FBoxSphereBounds( FBox( P2UVector(PBounds.minimum), P2UVector(PBounds.maximum) ) );
#else	// #if WITH_APEX
	return Super::CalcBounds(LocalToWorld);
#endif	// #if WITH_APEX
}

bool IsImpactDamageEnabled(const UDestructibleMesh* TheDestructibleMesh, int32 Level)
{
	if(TheDestructibleMesh->DefaultDestructibleParameters.DamageParameters.ImpactDamage == 0.f)
	{
		return false;
	}

	bool bEnableImpactDamage = false;
	const FDestructibleDepthParameters& DepthParams = TheDestructibleMesh->DefaultDestructibleParameters.DepthParameters[Level];
	const EImpactDamageOverride LevelOverride = DepthParams.ImpactDamageOverride; 

	switch(LevelOverride)
	{
		case EImpactDamageOverride::IDO_On:
		{
			return true;
		}

		case EImpactDamageOverride::IDO_Off:
		{
			return false;
		}

		default:
		{
			//return default if we're within the default level
		    return TheDestructibleMesh->DefaultDestructibleParameters.DamageParameters.DefaultImpactDamageDepth >= Level ? TheDestructibleMesh->DefaultDestructibleParameters.DamageParameters.bEnableImpactDamage : false;
		}
	}
}

void UDestructibleComponent::OnUpdateTransform(bool bSkipPhysicsMove)
{
	// We are handling the physics move below, so don't handle it at higher levels
	Super::OnUpdateTransform(true);

	if (SkeletalMesh == NULL)
	{
		return;
	}

	if (!bPhysicsStateCreated || bSkipPhysicsMove)
	{
		return;
	}

	const FTransform& CurrentLocalToWorld = ComponentToWorld;

	if(CurrentLocalToWorld.ContainsNaN())
	{
		return;
	}

	// warn if it has non-uniform scale
	const FVector& MeshScale3D = CurrentLocalToWorld.GetScale3D();
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if( !MeshScale3D.IsUniform() )
	{
		UE_LOG(LogPhysics, Log, TEXT("UDestructibleComponent::SendPhysicsTransform : Non-uniform scale factor (%s) can cause physics to mismatch for %s  SkelMesh: %s"), *MeshScale3D.ToString(), *GetFullName(), SkeletalMesh ? *SkeletalMesh->GetFullName() : TEXT("NULL"));
	}
#endif

#if WITH_APEX
	if (ApexDestructibleActor)
	{
		PxRigidDynamic* PRootActor = ApexDestructibleActor->getChunkPhysXActor(0);
		PxMat44 GlobalPose(PxMat33(U2PQuat(CurrentLocalToWorld.GetRotation())), U2PVector(CurrentLocalToWorld.GetTranslation()));
		if(!PRootActor || PRootActor->getScene())	//either root chunk is null meaning fractured (so there's a scene), or the root has a scene
		{
			ApexDestructibleActor->setGlobalPose(GlobalPose);
		}else
		{
			PRootActor->setGlobalPose(PxTransform(GlobalPose));	//we're not in a scene yet, so place the root actor in this new position
		}
	}
#endif // #if WITH_APEX
}

void UDestructibleComponent::CreatePhysicsState()
{
	// to avoid calling PrimitiveComponent, I'm just calling ActorComponent::CreatePhysicsState
	// @todo lh - fix me based on the discussion with Bryan G
	UActorComponent::CreatePhysicsState();
	bPhysicsStateCreated = true;

	// What we want to do with BodySetup is simply use it to store a PhysicalMaterial, and possibly some other relevant fields.  Set up pointers from the BodyInstance to the BodySetup and this component
	UBodySetup* BodySetup = GetBodySetup();
	BodyInstance.OwnerComponent	= this;
	BodyInstance.BodySetup = BodySetup;
	BodyInstance.InstanceBodyIndex = 0;

#if WITH_APEX
	if( SkeletalMesh == NULL )
	{
		return;
	}

	FPhysScene* PhysScene = World->GetPhysicsScene();
	check(PhysScene);

	if( GApexModuleDestructible == NULL )
	{
		UE_LOG(LogPhysics, Log, TEXT("UDestructibleComponent::CreatePhysicsState(): APEX must be enabled to init UDestructibleComponent physics.") );
		return;
	}

	if( ApexDestructibleActor != NULL )
	{
		UE_LOG(LogPhysics, Log, TEXT("UDestructibleComponent::CreatePhysicsState(): NxDestructibleActor already created.") );
		return;
	}

	UDestructibleMesh* TheDestructibleMesh = GetDestructibleMesh();
	if( TheDestructibleMesh == NULL || TheDestructibleMesh->ApexDestructibleAsset == NULL)
	{
		UE_LOG(LogPhysics, Log, TEXT("UDestructibleComponent::CreatePhysicsState(): No DestructibleMesh or missing ApexDestructibleAsset.") );
		return;
	}

	int32 ChunkCount = TheDestructibleMesh->ApexDestructibleAsset->getChunkCount();
	// Ensure the chunks start off invisible.  RefreshBoneTransforms should make them visible.
	for (int32 ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
	{
		SetChunkVisible(ChunkIndex, false);
	}

#if WITH_EDITOR
	if (GIsEditor && !World->IsGameWorld())
	{
		// In the editor, only set the 0 chunk to be visible.
		if (TheDestructibleMesh->ApexDestructibleAsset->getChunkCount() > 0)
		{
			SetChunkVisible(0, true);
		}
		return;
	}
#endif	// WITH_EDITOR

	// Only create physics in the game
	if( !World->IsGameWorld() )
	{
		return;
	}

	// Set template actor/body/shape properties

	// Find the PhysicalMaterial we need to apply to the physics bodies.
	UPhysicalMaterial* PhysMat = BodyInstance.GetSimplePhysicalMaterial();

	// Get the default actor descriptor NxParameterized data from the asset
	NxParameterized::Interface* ActorParams = TheDestructibleMesh->GetDestructibleActorDesc(PhysMat);

	// Create PhysX transforms from ComponentToWorld
	const PxMat44 GlobalPose(PxMat33(U2PQuat(ComponentToWorld.GetRotation())), U2PVector(ComponentToWorld.GetTranslation()));
	const PxVec3 Scale = U2PVector(ComponentToWorld.GetScale3D());

	// Set the transform in the actor descriptor
	verify( NxParameterized::setParamMat44(*ActorParams,"globalPose",GlobalPose) );
	verify( NxParameterized::setParamVec3(*ActorParams,"scale",Scale) );

	// Set the (initially) dynamic flag in the actor descriptor
	// See if we are 'static'
	verify( NxParameterized::setParamBool(*ActorParams,"dynamic", BodyInstance.bSimulatePhysics != false) );

	// Set the sleep velocity frame decay constant (was sleepVelocitySmoothingFactor) - a new feature that should help sleeping in large piles
	verify( NxParameterized::setParamF32(*ActorParams,"sleepVelocityFrameDecayConstant", 20.0f) );

	// Set up the shape desc template

	// Get collision channel and response
	PxFilterData PQueryFilterData, PSimFilterData;
	uint8 MoveChannel = GetCollisionObjectType();
	FCollisionResponseContainer CollResponse;
	if(IsCollisionEnabled())
	{
		// Only enable a collision response if collision is enabled
		CollResponse = GetCollisionResponseToChannels();

		LargeChunkCollisionResponse.SetCollisionResponseContainer(CollResponse);
		SmallChunkCollisionResponse.SetCollisionResponseContainer(CollResponse);
		SmallChunkCollisionResponse.SetResponse(ECC_Pawn, ECR_Overlap);
	}
	else
	{
		// now since by default it will all block, if collision is disabled, we need to set to ignore
		MoveChannel = ECC_WorldStatic;
		CollResponse.SetAllChannels(ECR_Ignore);
		LargeChunkCollisionResponse.SetAllChannels(ECR_Ignore);
		SmallChunkCollisionResponse.SetAllChannels(ECR_Ignore);
	}

	const bool bEnableImpactDamage = IsImpactDamageEnabled(TheDestructibleMesh, 0);
	const bool bEnableContactModification = TheDestructibleMesh->DefaultDestructibleParameters.DamageParameters.bCustomImpactResistance && TheDestructibleMesh->DefaultDestructibleParameters.DamageParameters.ImpactResistance > 0.f;

	// Passing AssetInstanceID = 0 so we'll have self-collision
	AActor* Owner = GetOwner();
	CreateShapeFilterData(MoveChannel, GetUniqueID(), CollResponse, 0, 0, PQueryFilterData, PSimFilterData, BodyInstance.bUseCCD, bEnableImpactDamage, false, bEnableContactModification);

	// Build filterData variations for complex and simple
	PSimFilterData.word3 |= EPDF_SimpleCollision | EPDF_ComplexCollision;
	PQueryFilterData.word3 |= EPDF_SimpleCollision | EPDF_ComplexCollision;

	// Set the filterData in the shape descriptor
	verify( NxParameterized::setParamU32(*ActorParams,"p3ShapeDescTemplate.simulationFilterData.word0", PSimFilterData.word0 ) );
	verify( NxParameterized::setParamU32(*ActorParams,"p3ShapeDescTemplate.simulationFilterData.word1", PSimFilterData.word1 ) );
	verify( NxParameterized::setParamU32(*ActorParams,"p3ShapeDescTemplate.simulationFilterData.word2", PSimFilterData.word2 ) );
	verify( NxParameterized::setParamU32(*ActorParams,"p3ShapeDescTemplate.simulationFilterData.word3", PSimFilterData.word3 ) );
	verify( NxParameterized::setParamU32(*ActorParams,"p3ShapeDescTemplate.queryFilterData.word0", PQueryFilterData.word0 ) );
	verify( NxParameterized::setParamU32(*ActorParams,"p3ShapeDescTemplate.queryFilterData.word1", PQueryFilterData.word1 ) );
	verify( NxParameterized::setParamU32(*ActorParams,"p3ShapeDescTemplate.queryFilterData.word2", PQueryFilterData.word2 ) );
	verify( NxParameterized::setParamU32(*ActorParams,"p3ShapeDescTemplate.queryFilterData.word3", PQueryFilterData.word3 ) );

	// Set the PhysX material in the shape descriptor
	PxMaterial* PMaterial = PhysMat->GetPhysXMaterial();
	verify( NxParameterized::setParamU64(*ActorParams,"p3ShapeDescTemplate.material", (physx::PxU64)PMaterial) );

	// Set the rest depth to match the skin width in the shape descriptor
	const physx::PxCookingParams& CookingParams = GApexSDK->getCookingInterface()->getParams();
	verify( NxParameterized::setParamF32(*ActorParams,"p3ShapeDescTemplate.restOffset", -CookingParams.skinWidth) );

	// Set the PhysX material in the actor descriptor
	verify( NxParameterized::setParamBool(*ActorParams,"p3ActorDescTemplate.flags.eDISABLE_GRAVITY",false) );
	verify( NxParameterized::setParamBool(*ActorParams,"p3ActorDescTemplate.flags.eVISUALIZATION",true) );

	// Set the PxActor's and PxShape's userData fields to this component's body instance
	verify( NxParameterized::setParamU64(*ActorParams,"p3ActorDescTemplate.userData", 0 ) );

	// All shapes created by this DestructibleActor will have the userdata of the owning component.
	// We need this, as in some cases APEX is moving shapes accross actors ( ex. FormExtended structures )
	verify( NxParameterized::setParamU64(*ActorParams,"p3ShapeDescTemplate.userData", (PxU64)&PhysxUserData ) );

	// Set up the body desc template in the actor descriptor
	verify( NxParameterized::setParamF32(*ActorParams,"p3BodyDescTemplate.angularDamping", BodyInstance.AngularDamping ) );
	verify( NxParameterized::setParamF32(*ActorParams,"p3BodyDescTemplate.linearDamping", BodyInstance.LinearDamping ) );
	const PxTolerancesScale& PScale = GPhysXSDK->getTolerancesScale();
	PxF32 SleepEnergyThreshold = 0.00005f*PScale.speed*PScale.speed;	// 1/1000 Default, since the speed scale is quite high
	if (BodyInstance.SleepFamily == ESleepFamily::Sensitive)
	{
		SleepEnergyThreshold /= 20.0f;
	}
	verify( NxParameterized::setParamF32(*ActorParams,"p3BodyDescTemplate.sleepThreshold", SleepEnergyThreshold) );
//	NxParameterized::setParamF32(*ActorParams,"bodyDescTemplate.sleepDamping", SleepDamping );
	verify( NxParameterized::setParamF32(*ActorParams,"p3BodyDescTemplate.density", 0.001f*PhysMat->Density) );	// Convert from g/cm^3 to kg/cm^3
	// Enable CCD if requested
	verify( NxParameterized::setParamBool(*ActorParams,"p3BodyDescTemplate.flags.eENABLE_CCD", BodyInstance.bUseCCD != 0) );
	// Ask the actor to create chunk events, for more efficient visibility updates
	verify( NxParameterized::setParamBool(*ActorParams,"createChunkEvents", true) );

	// Enable hard sleeping if requested
	verify( NxParameterized::setParamBool(*ActorParams,"useHardSleeping", bEnableHardSleeping) );

	

	// Destructibles are always dynamic or kinematic, and therefore only go into one of the scenes
	const uint32 SceneType = BodyInstance.UseAsyncScene(PhysScene) ? PST_Async : PST_Sync;
	NxApexScene* ApexScene = PhysScene->GetApexScene(SceneType);
	PxScene* PScene = PhysScene->GetPhysXScene(SceneType);

	BodyInstance.SceneIndexSync = SceneType == PST_Sync ? PhysScene->PhysXSceneIndex[PST_Sync] : 0;
	BodyInstance.SceneIndexAsync = SceneType == PST_Async ? PhysScene->PhysXSceneIndex[PST_Async] : 0;
	check(ApexScene);

	ChunkInfos.Reset(ChunkCount);
	ChunkInfos.AddZeroed(ChunkCount);
	PhysxChunkUserData.Reset(ChunkCount);
	PhysxChunkUserData.AddZeroed(ChunkCount);

	// Create an APEX NxDestructibleActor from the Destructible asset and actor descriptor
	ApexDestructibleActor = static_cast<NxDestructibleActor*>(TheDestructibleMesh->ApexDestructibleAsset->createApexActor(*ActorParams, *ApexScene));
	check(ApexDestructibleActor);

	// Make a backpointer to this component
	PhysxUserData = FPhysxUserData(this);
	ApexDestructibleActor->userData = &PhysxUserData;

	// Cache cooked collision data
	// BRGTODO : cook in asset
	ApexDestructibleActor->cacheModuleData();

	// BRGTODO : Per-actor LOD setting
//	ApexDestructibleActor->forcePhysicalLod( DestructibleActor->LOD );

	// Start asleep if requested
	PxRigidDynamic* PRootActor = ApexDestructibleActor->getChunkPhysXActor(0);


	//  Put to sleep or wake up only if the component is physics-simulated
	if (PRootActor != NULL && BodyInstance.bSimulatePhysics)
	{
		SCOPED_SCENE_WRITE_LOCK(PScene);	//Question, since apex is defer adding actors do we need to lock? Locking the async scene is expensive!

		PRootActor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !BodyInstance.bEnableGravity);

		// Sleep/wake up as appropriate
		if (!BodyInstance.bStartAwake)
		{
			ApexDestructibleActor->setChunkPhysXActorAwakeState(0, false);
		}
		}

	UpdateBounds();
#endif	// #if WITH_APEX
}

void UDestructibleComponent::DestroyPhysicsState()
{
#if WITH_APEX
	if(ApexDestructibleActor != NULL)
	{
		if(UWorld * World = GetWorld())
		{
			if (FPhysScene * PhysScene = World->GetPhysicsScene())
			{
				PhysScene->DeferredCommandHandler.DeferredRelease(ApexDestructibleActor);
			}
		}
		
		ApexDestructibleActor = NULL;
		
		//Destructible component uses the BodyInstance in PrimitiveComponent in a very dangerous way. It assigns PxRigidDynamic to it as it needs it.
		//Destructible PxRigidDynamic actors can be deleted from under us as PhysX sees fit.
		//Ideally we wouldn't ever have a dangling pointer, but in practice this is hard to avoid.
		//In theory anyone using BodyInstance on a PrimitiveComponent should be using functions like GetBodyInstance - in which case we properly fix up the dangling pointer
		BodyInstance.RigidActorSync = NULL;
		BodyInstance.RigidActorAsync = NULL;
	}
#endif	// #if WITH_APEX
	Super::DestroyPhysicsState();
}

UBodySetup* UDestructibleComponent::GetBodySetup()
{
	if (SkeletalMesh != NULL)
	{
		UDestructibleMesh* TheDestructibleMesh = GetDestructibleMesh();

		if (TheDestructibleMesh != NULL)
		{
			return TheDestructibleMesh->BodySetup;
		}
	}

	return NULL;
}

bool UDestructibleComponent::CanEditSimulatePhysics()
{
	// if destructiblemeshcomponent, we will allow it always
	return true;
}

void UDestructibleComponent::AddImpulse( FVector Impulse, FName BoneName /*= NAME_None*/, bool bVelChange /*= false*/ )
{
#if WITH_APEX
	ExecuteOnPhysicsReadWrite([&]
	{
		const int32 ChunkIdx = BoneIdxToChunkIdx(GetBoneIndex(BoneName));
		if(PxRigidDynamic* Actor = ApexDestructibleActor->getChunkPhysXActor(ChunkIdx))
		{
			Actor->addForce(U2PVector(Impulse), bVelChange ? PxForceMode::eVELOCITY_CHANGE : PxForceMode::eIMPULSE);
		}
	});
#endif
}

void UDestructibleComponent::AddImpulseAtLocation( FVector Impulse, FVector Position, FName BoneName /*= NAME_None*/ )
{
#if WITH_APEX
	ExecuteOnPhysicsReadWrite([&]
	{
		const int32 ChunkIdx = BoneIdxToChunkIdx(GetBoneIndex(BoneName));
		if (PxRigidDynamic* Actor = ApexDestructibleActor->getChunkPhysXActor(ChunkIdx))
		{
			PxRigidBodyExt::addForceAtPos(*Actor, U2PVector(Impulse), U2PVector(Position), PxForceMode::eIMPULSE);
		}
	});
#endif
}

void UDestructibleComponent::AddForce( FVector Force, FName BoneName /*= NAME_None*/, bool bAccelChange /* = false */ )
{
#if WITH_APEX
	ExecuteOnPhysicsReadWrite([&]
	{
		const int32 ChunkIdx = BoneIdxToChunkIdx(GetBoneIndex(BoneName));
		if (PxRigidDynamic* Actor = ApexDestructibleActor->getChunkPhysXActor(ChunkIdx))
		{
			Actor->addForce(U2PVector(Force), bAccelChange ? PxForceMode::eACCELERATION : PxForceMode::eFORCE);
		}
	});
#endif
}

void UDestructibleComponent::AddForceAtLocation( FVector Force, FVector Location, FName BoneName /*= NAME_None*/ )
{
#if WITH_APEX
	ExecuteOnPhysicsReadWrite([&]
	{
		int32 ChunkIdx = BoneIdxToChunkIdx(GetBoneIndex(BoneName));
		if (PxRigidDynamic* Actor = ApexDestructibleActor->getChunkPhysXActor(ChunkIdx))
		{
			PxRigidBodyExt::addForceAtPos(*Actor, U2PVector(Force), U2PVector(Location), PxForceMode::eFORCE);
		}
	});
#endif
}

void UDestructibleComponent::AddRadialImpulse(FVector Origin, float Radius, float Strength, ERadialImpulseFalloff Falloff, bool bVelChange)
{
#if WITH_APEX
	if (bIgnoreRadialImpulse)
	{
		return;
	}

	if (ApexDestructibleActor == NULL)
	{
		return;
	}

	PxRigidDynamic** PActorBuffer = NULL;
	PxU32 PActorCount = 0;
	if (ApexDestructibleActor->acquirePhysXActorBuffer(PActorBuffer, PActorCount, NxDestructiblePhysXActorQueryFlags::Dynamic))
	{
		PxScene* LockedScene = NULL;

		while (PActorCount--)
		{
			PxRigidDynamic* PActor = *PActorBuffer++;
			if (PActor != NULL)
			{
				if (!LockedScene)
				{
					LockedScene = PActor->getScene();
					LockedScene->lockWrite();
					LockedScene->lockRead();
				}
				AddRadialImpulseToPxRigidBody_AssumesLocked(*PActor, Origin, Radius, Strength, Falloff, bVelChange);
			}
		}

		if (LockedScene)
		{
			LockedScene->unlockRead();
			LockedScene->unlockWrite();
			LockedScene = NULL;
		}

		ApexDestructibleActor->releasePhysXActorBuffer();
	}
#endif	// #if WITH_APEX
}

void UDestructibleComponent::AddRadialForce(FVector Origin, float Radius, float Strength, ERadialImpulseFalloff Falloff, bool bAccelChange /* = false */)
{
#if WITH_APEX
	if(bIgnoreRadialForce)
	{
		return;
	}

	if (ApexDestructibleActor == NULL)
	{
		return;
	}

	PxRigidDynamic** PActorBuffer = NULL;
	PxU32 PActorCount = 0;
	if (ApexDestructibleActor->acquirePhysXActorBuffer(PActorBuffer, PActorCount, NxDestructiblePhysXActorQueryFlags::Dynamic))
	{
		PxScene* LockedScene = NULL;
		

		while (PActorCount--)
		{
			PxRigidDynamic* PActor = *PActorBuffer++;
			if (PActor != NULL)
			{
				if (!LockedScene)
				{
					LockedScene = PActor->getScene();
					LockedScene->lockWrite();
					LockedScene->lockRead();
				}

				AddRadialForceToPxRigidBody_AssumesLocked(*PActor, Origin, Radius, Strength, Falloff, bAccelChange);
			}

			if (LockedScene)
			{
				LockedScene->unlockRead();
				LockedScene->unlockWrite();
				LockedScene = NULL;
			}
		}
		ApexDestructibleActor->releasePhysXActorBuffer();
	}
#endif	// #if WITH_APEX
}

void UDestructibleComponent::ReceiveComponentDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	UDamageType const* const DamageTypeCDO = DamageEvent.DamageTypeClass ? DamageEvent.DamageTypeClass->GetDefaultObject<UDamageType>() : GetDefault<UDamageType>();
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		FPointDamageEvent const* const PointDamageEvent = (FPointDamageEvent*)(&DamageEvent);
		ApplyDamage(DamageAmount, PointDamageEvent->HitInfo.ImpactPoint, PointDamageEvent->ShotDirection, DamageTypeCDO->DestructibleImpulse);
	}
	else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
	{
		FRadialDamageEvent const* const RadialDamageEvent = (FRadialDamageEvent*)(&DamageEvent);
		ApplyRadiusDamage(DamageAmount, RadialDamageEvent->Origin, RadialDamageEvent->Params.OuterRadius, DamageTypeCDO->DestructibleImpulse, false);
	}
}

#if WITH_APEX
void UDestructibleComponent::SpawnFractureEffectsFromDamageEvent(const NxApexDamageEventReportData& InDamageEvent)
{
	// Use the component's fracture effects if the override is selected, otherwise use fracture effects from the asset
	TArray<FFractureEffect>& UseFractureEffects = (bFractureEffectOverride || !SkeletalMesh) ? FractureEffects : CastChecked<UDestructibleMesh>(SkeletalMesh)->FractureEffects;

	UDestructibleMesh* TheDestructibleMesh = GetDestructibleMesh();
	if (!TheDestructibleMesh)
	{
		return;
	}


	// We keep track of the handled parent chunks here
	TArray<int32> HandledParents;
	for(physx::PxU32 eventN = 0; eventN < InDamageEvent.fractureEventListSize; ++eventN)
	{
		const NxApexChunkData& chunkData = InDamageEvent.fractureEventList[eventN];
		if( chunkData.depth < (physx::PxU32)UseFractureEffects.Num() )
		{
			// We can get the root chunk here as well, so make sure that the parent index is 0, even for the root chunk
			int32 ParentIdx = FMath::Max(TheDestructibleMesh->ApexDestructibleAsset->getChunkParentIndex(chunkData.index), 0);
			
			// We can test a number of flags - we'll play an effect if the chunk was destroyed
			// As we only get the fractured event here for chunks that come free, we spawn fracture
			// effects only once per unique parent
			if((chunkData.flags & NxApexChunkFlag::FRACTURED) && !HandledParents.Contains(ParentIdx))
			{
				FVector Position = P2UVector(chunkData.worldBounds.getCenter());
				FFractureEffect& FractureEffect = UseFractureEffects[chunkData.depth];
				if( FractureEffect.Sound != NULL )
				{
					// Spawn sound
					UGameplayStatics::PlaySoundAtLocation( this, FractureEffect.Sound, Position );
				}
				if( FractureEffect.ParticleSystem != NULL )
				{
					// Spawn particle system
					UParticleSystemComponent* ParticleSystemComponent = UGameplayStatics::SpawnEmitterAtLocation( this, FractureEffect.ParticleSystem, Position );

					// Disable shadows, since destructibles tend to generate a lot of these
					if (ParticleSystemComponent != NULL)
					{
						ParticleSystemComponent->CastShadow = false;
					}
				}

				HandledParents.Add(ParentIdx);
			}			
		}
	}
}

void UDestructibleComponent::OnDamageEvent(const NxApexDamageEventReportData& InDamageEvent)
{
	FVector HitPosition = P2UVector(InDamageEvent.hitPosition);
	FVector HitDirection = P2UVector(InDamageEvent.hitDirection);

	OnComponentFracture.Broadcast(HitPosition, HitDirection);
	if (ADestructibleActor * DestructibleActor = Cast<ADestructibleActor>(GetOwner()))
	{
		DestructibleActor->OnActorFracture.Broadcast(HitPosition, HitDirection);
	}

	SpawnFractureEffectsFromDamageEvent(InDamageEvent);

	// After receiving damage, no longer receive decals.
	if (bReceivesDecals)
	{
		bReceivesDecals = false;
		MarkRenderStateDirty();
	}
}

void UDestructibleComponent::OnVisibilityEvent(const NxApexChunkStateEventData & InVisibilityEvent)
{
	for (uint32 EventIndex = 0; EventIndex < InVisibilityEvent.stateEventListSize; ++EventIndex)
	{
		const NxDestructibleChunkEvent &  Event = InVisibilityEvent.stateEventList[EventIndex];
		// Right now the only events are visibility changes.  So as an optimization we won't check for the event type.
		//				if (Event.event & physx::NxDestructibleChunkEvent::VisibilityChanged)
		const bool bVisible = (Event.event & physx::NxDestructibleChunkEvent::ChunkVisible) != 0;
		SetChunkVisible(Event.chunkIndex, bVisible);
	}
}
#endif // WITH_APEX

bool UDestructibleComponent::IsFracturedOrInitiallyStatic() const
{
	bool bInitiallyStatic = false;
	bool bFractured = false;
#if WITH_APEX
	if (ApexDestructibleActor == nullptr)
	{
		return false;
	}

	bInitiallyStatic = !ApexDestructibleActor->isInitiallyDynamic();

	if (bInitiallyStatic == false)
	{
		//If we have only one chunk and its index is 0 we are NOT fractured. Otherwise we must have fractured
		const physx::PxU32 VisibleChunkCount = ApexDestructibleActor->getNumVisibleChunks();
		if (VisibleChunkCount == 1)
		{
			const physx::PxU16* VisibleChunks = ApexDestructibleActor->getVisibleChunks();
			bFractured = *VisibleChunks != 0;
		}
		else
		{
			bFractured = true;
		}
	}
#endif
	return bFractured || bInitiallyStatic;
}

bool UDestructibleComponent::ExecuteOnPhysicsReadOnly(TFunctionRef<void()> Func) const
{
#if WITH_APEX
	if (ApexDestructibleActor)
	{
		FPhysScene* PhysScene = GetWorld()->GetPhysicsScene();
		// Destructibles are always dynamic or kinematic, and therefore only go into one of the scenes
		const uint32 SceneType = BodyInstance.UseAsyncScene(PhysScene) ? PST_Async : PST_Sync;
		PxScene* PScene = PhysScene->GetPhysXScene(SceneType);

		SCOPED_SCENE_READ_LOCK(PScene);
		Func();

		return true;
	}
#endif

	return false;
}

bool UDestructibleComponent::ExecuteOnPhysicsReadWrite(TFunctionRef<void()> Func) const
{
#if WITH_APEX
	if (ApexDestructibleActor)
	{
		FPhysScene* PhysScene = GetWorld()->GetPhysicsScene();
		// Destructibles are always dynamic or kinematic, and therefore only go into one of the scenes
		const uint32 SceneType = BodyInstance.UseAsyncScene(PhysScene) ? PST_Async : PST_Sync;
		PxScene* PScene = PhysScene->GetPhysXScene(SceneType);

		SCOPED_SCENE_WRITE_LOCK(PScene);
		Func();

		return true;
	}
#endif

	return false;
}

void UDestructibleComponent::RefreshBoneTransforms(FActorComponentTickFunction* TickFunction)
{
}

void UDestructibleComponent::SetDestructibleMesh(class UDestructibleMesh* NewMesh)
{
#if WITH_APEX
	uint32 ChunkCount = NewMesh ? NewMesh->ApexDestructibleAsset->getChunkCount() : 0;
	ChunkInfos.Reset(ChunkCount);
	ChunkInfos.AddZeroed(ChunkCount);
	PhysxChunkUserData.Reset(ChunkCount);
	PhysxChunkUserData.AddZeroed(ChunkCount);
#endif // WITH_APEX

	Super::SetSkeletalMesh( NewMesh );

#if WITH_EDITORONLY_DATA
	// If the SkeletalMesh has changed, update our transient value too.
	DestructibleMesh = GetDestructibleMesh();
#endif // WITH_EDITORONLY_DATA
	
	RecreatePhysicsState();
}

class UDestructibleMesh * UDestructibleComponent::GetDestructibleMesh()
{
	return Cast<UDestructibleMesh>(SkeletalMesh);
}

void UDestructibleComponent::SetSkeletalMesh( USkeletalMesh* InSkelMesh )
{
	if(InSkelMesh != NULL && !InSkelMesh->IsA(UDestructibleMesh::StaticClass()))
	{
		// Issue warning and do nothing if this is not actually a UDestructibleMesh
		UE_LOG(LogPhysics, Log, TEXT("UDestructibleComponent::SetSkeletalMesh(): Passed-in USkeletalMesh (%s) must be a UDestructibleMesh.  SkeletalMesh not set."), *InSkelMesh->GetPathName() );
		return;
	}

	UDestructibleMesh* TheDestructibleMesh = static_cast<UDestructibleMesh*>(InSkelMesh);
#if WITH_APEX
	if(TheDestructibleMesh != NULL && TheDestructibleMesh->ApexDestructibleAsset == NULL)
	{
		UE_LOG(LogPhysics, Log, TEXT("UDestructibleComponent::SetSkeletalMesh(): Missing ApexDestructibleAsset on '%s'."), *InSkelMesh->GetPathName() );
		return;
	}

	SetDestructibleMesh(TheDestructibleMesh);

	if(TheDestructibleMesh != NULL)
	{
		// Resize the fracture effects array to the appropriate size
		FractureEffects.AddZeroed(TheDestructibleMesh->ApexDestructibleAsset->getDepthCount());
	}
#else
	SetDestructibleMesh(TheDestructibleMesh);
#endif // WITH_APEX
}

FTransform UDestructibleComponent::GetSocketTransform(FName InSocketName, ERelativeTransformSpace TransformSpace) const
{
	FTransform ST = Super::GetSocketTransform(InSocketName, TransformSpace);

	int32 BoneIdx = GetBoneIndex(InSocketName);

	// As bones in a destructible might be scaled to 0 when hidden, we force a scale of 1 if we want the socket transform
	if (BoneIdx > 0 && IsBoneHidden(BoneIdx))
	{
		ST.SetScale3D(FVector(1.0f, 1.0f, 1.0f));
	}

	return ST;
}

#if WITH_APEX
void UDestructibleComponent::Pair( int32 ChunkIndex, PxShape* PShape)
{
	FDestructibleChunkInfo* CI;
	FPhysxUserData* UserData;

	check(ChunkIndex < ChunkInfos.Num());

	CI = &ChunkInfos[ChunkIndex];

					CI->ChunkIndex = ChunkIndex;
				CI->OwningComponent = this;

	UserData = &PhysxChunkUserData[ChunkIndex];
	UserData->Set(UserData, CI);

	PShape->userData = UserData;

	PShape->getActor()->userData = UserData;

	PShape->getActor()->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !BodyInstance.bEnableGravity);

	// Set collision response to non-root chunks
	if (GetDestructibleMesh()->ApexDestructibleAsset->getChunkParentIndex(ChunkIndex) >= 0)
	{
		SetCollisionResponseForShape(PShape, ChunkIndex);
	}
}
#endif

void UDestructibleComponent::SetChunkVisible( int32 ChunkIndex, bool bVisible )
{
#if WITH_APEX
	// Bone 0 is a dummy root bone
	const int32 BoneIndex = ChunkIdxToBoneIdx(ChunkIndex);

	if( bVisible )
	{
		UnHideBone(BoneIndex);

		if (NULL != ApexDestructibleActor)
		{
			physx::PxShape** PShapes;
			const physx::PxU32 PShapeCount = ApexDestructibleActor->getChunkPhysXShapes(PShapes, ChunkIndex);
			if (PShapeCount > 0)
			{
				const physx::PxMat44 ChunkPoseRT = ApexDestructibleActor->getChunkPose(ChunkIndex);	// Unscaled
				const physx::PxTransform Transform(ChunkPoseRT);
				SetChunkWorldRT(ChunkIndex, P2UQuat(Transform.q), P2UVector(Transform.p));
			}
		}
	}
	else
	{
		HideBone(BoneIndex, PBO_None);
	}

	// Mark the transform as dirty, so the bounds are updated and sent to the render thread
	MarkRenderTransformDirty();

	// New bone positions need to be sent to render thread
	MarkRenderDynamicDataDirty();
#endif
}

#if WITH_APEX
void UDestructibleComponent::UpdateDestructibleChunkTM(const TArray<const PxRigidActor*>& ActiveActors)
{
	//We want to consolidate the transforms so that we update each destructible component once by passing it an array of chunks to update.
	//This helps avoid a lot of duplicated work like marking render dirty, computing inverse world component, etc...

	TMap<UDestructibleComponent*, TArray<FUpdateChunksInfo> > ComponentUpdateMapping;
	
	//prepare map to update destructible components
	TArray<PxShape*> Shapes;
	for (const PxRigidActor* RigidActor : ActiveActors)
	{
		if (const FDestructibleChunkInfo* DestructibleChunkInfo = FPhysxUserData::Get<FDestructibleChunkInfo>(RigidActor->userData))
		{
			if (GApexModuleDestructible->owns(RigidActor) && DestructibleChunkInfo->OwningComponent.IsValid())
			{
				Shapes.AddUninitialized(RigidActor->getNbShapes());
				int32 NumShapes = RigidActor->getShapes(Shapes.GetData(), Shapes.Num());
				for (int32 ShapeIdx = 0; ShapeIdx < Shapes.Num(); ++ShapeIdx)
				{
					PxShape* Shape = Shapes[ShapeIdx];
					int32 ChunkIndex;
					if (NxDestructibleActor* DestructibleActor = GApexModuleDestructible->getDestructibleAndChunk(Shape, &ChunkIndex))
					{
						const physx::PxMat44 ChunkPoseRT = DestructibleActor->getChunkPose(ChunkIndex);
						const physx::PxTransform Transform(ChunkPoseRT);
						if (UDestructibleComponent* DestructibleComponent = Cast<UDestructibleComponent>(FPhysxUserData::Get<UPrimitiveComponent>(DestructibleActor->userData)))
						{
							if (DestructibleComponent->IsRegistered())
							{
								TArray<FUpdateChunksInfo>& UpdateInfos = ComponentUpdateMapping.FindOrAdd(DestructibleComponent);
								FUpdateChunksInfo* UpdateInfo = new (UpdateInfos)FUpdateChunksInfo(ChunkIndex, P2UTransform(Transform));
							}
						}
					}
				}

				Shapes.Empty(Shapes.Num());	//we want to keep largest capacity array to avoid reallocs
			}
		}
	}
	
	//update each component
	for (auto It = ComponentUpdateMapping.CreateIterator(); It; ++It)
	{
		UDestructibleComponent* DestructibleComponent = It.Key();
		TArray<FUpdateChunksInfo>& UpdateInfos = It.Value();
		if (DestructibleComponent->IsFracturedOrInitiallyStatic())
		{
			DestructibleComponent->SetChunksWorldTM(UpdateInfos);
		}
		else
		{
			//if we haven't fractured it must mean that we're simulating a destructible and so we should update our ComponentToWorld based on the single rigid body
			DestructibleComponent->SyncComponentToRBPhysics();
		}
	}

}

#endif

void UDestructibleComponent::SetChunksWorldTM(const TArray<FUpdateChunksInfo>& UpdateInfos)
{
	const FQuat InvRotation = ComponentToWorld.GetRotation().Inverse();

	for (const FUpdateChunksInfo& UpdateInfo : UpdateInfos)
	{
		// Bone 0 is a dummy root bone
		const int32 BoneIndex = ChunkIdxToBoneIdx(UpdateInfo.ChunkIndex);
		const FVector WorldTranslation	= UpdateInfo.WorldTM.GetLocation();
		const FQuat WorldRotation		= UpdateInfo.WorldTM.GetRotation();

		const FQuat BoneRotation = InvRotation*WorldRotation;
		const FVector BoneTranslation = InvRotation.RotateVector(WorldTranslation - ComponentToWorld.GetTranslation()) / ComponentToWorld.GetScale3D();

		GetEditableSpaceBases()[BoneIndex] = FTransform(BoneRotation, BoneTranslation);
	}

	// Mark the transform as dirty, so the bounds are updated and sent to the render thread
	MarkRenderTransformDirty();

	// New bone positions need to be sent to render thread
	MarkRenderDynamicDataDirty();

	//Update bone visibilty and flip the editable space base buffer
	FlipEditableSpaceBases();
}

void UDestructibleComponent::SetChunkWorldRT( int32 ChunkIndex, const FQuat& WorldRotation, const FVector& WorldTranslation )
{
	// Bone 0 is a dummy root bone
	const int32 BoneIndex = ChunkIdxToBoneIdx(ChunkIndex);

	// Mark the transform as dirty, so the bounds are updated and sent to the render thread
	MarkRenderTransformDirty();

	// New bone positions need to be sent to render thread
	MarkRenderDynamicDataDirty();

#if 0
	// Scale is already applied to the ComponentToWorld transform, and is carried into the bones _locally_.
	// So there is no need to set scale in the bone local transforms
	const FTransform WorldRT(WorldRotation, WorldTranslation, ComponentToWorld.GetScale3D());
	SpaceBases(BoneIndex) = WorldRT*ComponentToWorld.Inverse();
#elif 1
	// More optimal form of the above
	const FQuat BoneRotation = ComponentToWorld.GetRotation().Inverse()*WorldRotation;
	const FVector BoneTranslation = ComponentToWorld.GetRotation().Inverse().RotateVector(WorldTranslation - ComponentToWorld.GetTranslation())/ComponentToWorld.GetScale3D();
	GetEditableSpaceBases()[BoneIndex] = FTransform(BoneRotation, BoneTranslation);
#endif
}

void UDestructibleComponent::ApplyDamage(float DamageAmount, const FVector& HitLocation, const FVector& ImpulseDir, float ImpulseStrength)
{
#if WITH_APEX
	if (ApexDestructibleActor != NULL)
	{
		const FVector& NormalizedImpactDir = ImpulseDir.GetSafeNormal();

		// Transfer damage information to the APEX NxDestructibleActor interface
		ApexDestructibleActor->applyDamage(DamageAmount, ImpulseStrength, U2PVector( HitLocation ), U2PVector( ImpulseDir ));
	}
#endif
}

void UDestructibleComponent::ApplyRadiusDamage(float BaseDamage, const FVector& HurtOrigin, float DamageRadius, float ImpulseStrength, bool bFullDamage)
{
#if WITH_APEX
	if (ApexDestructibleActor != NULL)
	{
		// Transfer damage information to the APEX NxDestructibleActor interface
		ApexDestructibleActor->applyRadiusDamage(BaseDamage, ImpulseStrength, U2PVector( HurtOrigin ), DamageRadius, bFullDamage ? false : true);
	}
#endif
}

bool UDestructibleComponent::DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const
{
#if WITH_APEX
	if (ApexDestructibleActor == NULL)
	{
		return false;
	}

	NxDestructibleActor* DestrActor = const_cast<NxDestructibleActor*>(ApexDestructibleActor);

	const FTransform ComponentToWorldNoScale(ComponentToWorld.GetRotation(), ComponentToWorld.GetTranslation(), FVector(1.f));
	TArray<PxShape*> Shapes;
	Shapes.AddUninitialized(8);
	PxRigidDynamic** PActorBuffer = NULL;
	PxU32 PActorCount = 0;
	if (DestrActor->acquirePhysXActorBuffer(PActorBuffer, PActorCount
		, NxDestructiblePhysXActorQueryFlags::Static 
		| NxDestructiblePhysXActorQueryFlags::Dormant
		| NxDestructiblePhysXActorQueryFlags::Dynamic))
	{
		uint32 ShapesExportedCount = 0;

		while (PActorCount--)
		{
			const PxRigidDynamic* PActor = *PActorBuffer++;
			if (PActor != NULL)
			{
				const FTransform PActorGlobalPose = P2UTransform(PActor->getGlobalPose());

				const PxU32 ShapesCount = PActor->getNbShapes();
				if (ShapesCount > PxU32(Shapes.Num()))
				{
					Shapes.AddUninitialized(ShapesCount - Shapes.Num());
				}
				const PxU32 RetrievedShapesCount = PActor->getShapes(Shapes.GetData(), Shapes.Num());
				PxShape* const* ShapePtr = Shapes.GetData();
				for (PxU32 ShapeIndex = 0; ShapeIndex < RetrievedShapesCount; ++ShapeIndex, ++ShapePtr)
				{
					if (*ShapePtr != NULL)
					{
						const PxTransform LocalPose = (*ShapePtr)->getLocalPose();
						FTransform LocalToWorld = P2UTransform(LocalPose);
						LocalToWorld.Accumulate(PActorGlobalPose);

						switch((*ShapePtr)->getGeometryType())
						{
						case PxGeometryType::eCONVEXMESH:
							{
								PxConvexMeshGeometry Geometry;
								if ((*ShapePtr)->getConvexMeshGeometry(Geometry))
								{
									++ShapesExportedCount;

									// @todo address Geometry.scale not being used here
									GeomExport.ExportPxConvexMesh(Geometry.convexMesh, LocalToWorld);
								}
							}
							break;
						case PxGeometryType::eTRIANGLEMESH:
							{
								// @todo address Geometry.scale not being used here
								PxTriangleMeshGeometry Geometry;
								if ((*ShapePtr)->getTriangleMeshGeometry(Geometry))
								{
									++ShapesExportedCount;

									if ((Geometry.triangleMesh->getTriangleMeshFlags()) & PxTriangleMeshFlag::eHAS_16BIT_TRIANGLE_INDICES)
									{
										GeomExport.ExportPxTriMesh16Bit(Geometry.triangleMesh, LocalToWorld);
									}
									else
									{
										GeomExport.ExportPxTriMesh32Bit(Geometry.triangleMesh, LocalToWorld);
									}
								}
							}
						default:
							{
								UE_LOG(LogPhysics, Log, TEXT("UDestructibleComponent::DoCustomNavigableGeometryExport(): unhandled PxGeometryType, %d.")
									, int32((*ShapePtr)->getGeometryType()));
							}
							break;
						}
					}
				}
			}
		}
		ApexDestructibleActor->releasePhysXActorBuffer();

		INC_DWORD_STAT_BY(STAT_Navigation_DestructiblesShapesExported, ShapesExportedCount);
	}
#endif // WITH_APEX

	// we don't want a regular geometry export
	return false;
}

void UDestructibleComponent::Activate( bool bReset/*=false*/ )
{
	if (bReset || ShouldActivate()==true)
	{
		bIsActive = true;
	}
}

void UDestructibleComponent::Deactivate()
{
	if (ShouldActivate()==false)
	{
		bIsActive = false;
	}
}

bool UDestructibleComponent::ShouldUpdateTransform(bool bLODHasChanged) const
{
	// We do not want to update bone transforms before physics has finished
	return false;
}

bool UDestructibleComponent::LineTraceComponent( FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params )
{
	bool bHaveHit = false;
#if WITH_APEX
	if (ApexDestructibleActor != NULL)
	{
		PxF32 HitTime = 0.0f;
		PxVec3 HitNormal;
		
		int32 ChunkIdx = ApexDestructibleActor->rayCast(HitTime, HitNormal, U2PVector(Start), U2PVector(End-Start), NxDestructibleActorRaycastFlags::AllChunks);

		if (ChunkIdx != NxModuleDestructibleConst::INVALID_CHUNK_INDEX && HitTime <= 1.0f)
		{
			PxRigidDynamic* PActor = ApexDestructibleActor->getChunkPhysXActor(ChunkIdx);

			if (PActor != NULL)
			{
				// Store body instance state
				FFakeBodyInstanceState PrevState;
				SetupFakeBodyInstance(PActor, ChunkIdx, &PrevState);
				
				bHaveHit = Super::LineTraceComponent(OutHit, Start, End, Params);

				// Reset original body instance
				ResetFakeBodyInstance(PrevState);
			}
		}
	}
#endif
	return bHaveHit;
}

bool UDestructibleComponent::SweepComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionShape &CollisionShape, bool bTraceComplex/*=false*/)
{
	bool bHaveHit = false;
#if WITH_APEX
	if (ApexDestructibleActor != NULL)
	{
		PxF32 HitTime = 0.0f;
		PxVec3 HitNormal;

		int32 ChunkIdx = ApexDestructibleActor->obbSweep(HitTime, HitNormal, U2PVector(Start), U2PVector(CollisionShape.GetExtent()), PxMat33::createIdentity(), U2PVector(End - Start), NxDestructibleActorRaycastFlags::AllChunks);

		if (ChunkIdx != NxModuleDestructibleConst::INVALID_CHUNK_INDEX && HitTime <= 1.0f)
		{
			PxRigidDynamic* PActor = ApexDestructibleActor->getChunkPhysXActor(ChunkIdx);

			if (PActor != NULL)
			{
				// Store body instance state
				FFakeBodyInstanceState PrevState;
				SetupFakeBodyInstance(PActor, ChunkIdx, &PrevState);

				bHaveHit = Super::SweepComponent(OutHit, Start, End, CollisionShape, bTraceComplex);

				// Reset original body instance
				ResetFakeBodyInstance(PrevState);
			}
		}
	}
#endif
	return bHaveHit;
}

#if WITH_APEX
void UDestructibleComponent::SetupFakeBodyInstance( physx::PxRigidActor* NewRigidActor, int32 InstanceIdx, FFakeBodyInstanceState* PrevState )
{
	//This code is very dangerous, but at the moment I have no better solution:
	//Destructible component assigns PxRigidDynamic to the BodyInstance as it needs it.
	//Destructible PxRigidDynamic actors can be deleted from under us as PhysX sees fit.
	//Ideally we wouldn't ever have a dangling pointer, but in practice this is hard to avoid.
	//In theory anyone using BodyInstance on a PrimitiveComponent should be using functions like GetBodyInstance - in which case we properly fix up the dangling pointer

	if (PrevState != NULL)
	{
		PrevState->ActorSync = BodyInstance.RigidActorSync;
		PrevState->ActorAsync = BodyInstance.RigidActorAsync;
		PrevState->InstanceIndex = BodyInstance.InstanceBodyIndex;
	}

	const UWorld* World = GetWorld();
	const FPhysScene* PhysScene = World ? World->GetPhysicsScene() : nullptr;

	BodyInstance.RigidActorSync = BodyInstance.UseAsyncScene(PhysScene) ? NULL : NewRigidActor;
	BodyInstance.RigidActorAsync = BodyInstance.UseAsyncScene(PhysScene) ? NewRigidActor : NULL;
	BodyInstance.BodyAggregate = NULL;
	BodyInstance.InstanceBodyIndex = InstanceIdx;
}

void UDestructibleComponent::ResetFakeBodyInstance( FFakeBodyInstanceState& PrevState )
{
	BodyInstance.RigidActorSync = PrevState.ActorSync;
	BodyInstance.RigidActorAsync = PrevState.ActorAsync;
	BodyInstance.InstanceBodyIndex = PrevState.InstanceIndex;
}

#endif

void UDestructibleComponent::WakeRigidBody(FName BoneName /* = NAME_None */)
{
#if WITH_APEX
	ExecuteOnPhysicsReadWrite([&]
	{
		const int32 ChunkIdx = BoneIdxToChunkIdx(GetBoneIndex(BoneName));
		ApexDestructibleActor->setChunkPhysXActorAwakeState(ChunkIdx, true);
	});
#endif
}

void UDestructibleComponent::SetEnableGravity(bool bGravityEnabled)
{
	Super::SetEnableGravity(bGravityEnabled);
	
#if WITH_APEX
	uint32 ChunkCount = ApexDestructibleActor->getNumVisibleChunks();
	const uint16* ChunkIndices = ApexDestructibleActor->getVisibleChunks();
	for(uint32 c = 0; c < ChunkCount; c++)
		{
		PxActor* PActor = ApexDestructibleActor->getChunkPhysXActor(ChunkIndices[c]);
		check(PActor);
		PActor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !bGravityEnabled);
	}
#endif //WITH_APEX
}

FBodyInstance* UDestructibleComponent::GetBodyInstance( FName BoneName /*= NAME_None*/, bool) const
{
#if WITH_APEX
	if (ApexDestructibleActor != NULL)
	{
		int32 BoneIdx = GetBoneIndex(BoneName);
		PxRigidDynamic* PActor = ApexDestructibleActor->getChunkPhysXActor(BoneIdxToChunkIdx(BoneIdx));

		const_cast<UDestructibleComponent*>(this)->SetupFakeBodyInstance(PActor, BoneIdx);
	}
#endif // WITH_APEX

	return (FBodyInstance*)&BodyInstance;
}

bool UDestructibleComponent::IsAnySimulatingPhysics() const 
{
	return !!BodyInstance.bSimulatePhysics;
}

#if WITH_PHYSX

bool UDestructibleComponent::IsChunkLarge(int32 ChunkIdx) const
{
#if WITH_APEX
	check(ApexDestructibleActor);
	physx::PxBounds3 Bounds = ApexDestructibleActor->getChunkBounds(ChunkIdx);
	return Bounds.getExtents().maxElement() > LargeChunkThreshold;
#else
	return true;
#endif // WITH_APEX
}

void UDestructibleComponent::SetCollisionResponseForActor(PxRigidDynamic* Actor, int32 ChunkIdx, const FCollisionResponseContainer* ResponseOverride /*= NULL*/)
{
#if WITH_APEX
	if (ApexDestructibleActor == NULL)
	{
		return;
	}

	// Get collision channel and response
	PxFilterData PQueryFilterData, PSimFilterData;
	uint8 MoveChannel = GetCollisionObjectType();
	if(IsCollisionEnabled())
	{
		UDestructibleMesh* TheDestructibleMesh = GetDestructibleMesh();
		AActor* Owner = GetOwner();
		bool bLargeChunk = IsChunkLarge(ChunkIdx);
		const FCollisionResponseContainer& UseResponse = ResponseOverride == NULL ? (bLargeChunk ? LargeChunkCollisionResponse.GetResponseContainer() : SmallChunkCollisionResponse.GetResponseContainer()) : *ResponseOverride;

		physx::PxU32 SupportDepth = TheDestructibleMesh->ApexDestructibleAsset->getChunkDepth(ChunkIdx);

		const bool bEnableImpactDamage = IsImpactDamageEnabled(TheDestructibleMesh, SupportDepth);
		CreateShapeFilterData(MoveChannel, GetUniqueID(), UseResponse, 0, ChunkIdxToBoneIdx(ChunkIdx), PQueryFilterData, PSimFilterData, BodyInstance.bUseCCD, bEnableImpactDamage, false);
		
		PQueryFilterData.word3 |= EPDF_SimpleCollision | EPDF_ComplexCollision;

		SCOPED_SCENE_WRITE_LOCK(Actor->getScene());

		TArray<PxShape*> Shapes;
		Shapes.AddUninitialized(Actor->getNbShapes());

		int ShapeCount = Actor->getShapes(Shapes.GetData(), Shapes.Num());

		for (int32 i=0; i < ShapeCount; ++i)
		{
			PxShape* Shape = Shapes[i];

			Shape->setQueryFilterData(PQueryFilterData);
			Shape->setSimulationFilterData(PSimFilterData);
			Shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true); 
			Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true); 
			Shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
		}
	}
#endif
}

void UDestructibleComponent::SetCollisionResponseForAllActors(const FCollisionResponseContainer& ResponseOverride)
{
#if WITH_APEX
	if (ApexDestructibleActor == NULL)
	{
		return;
	}

	PxRigidDynamic** PActorBuffer = NULL;
	PxU32 PActorCount = 0;
	if (ApexDestructibleActor->acquirePhysXActorBuffer(PActorBuffer, PActorCount))
	{
		PxScene* LockedScene = NULL;

		while (PActorCount--)
		{
			PxRigidDynamic* PActor = *PActorBuffer++;
			if (PActor != NULL)
			{
				FDestructibleChunkInfo* ChunkInfo = FPhysxUserData::Get<FDestructibleChunkInfo>(PActor->userData);
				if (ChunkInfo != NULL)
				{
					if (!LockedScene)
					{
						LockedScene = PActor->getScene();
						LockedScene->lockWrite();
						LockedScene->lockRead();
					}
					SetCollisionResponseForActor(PActor, ChunkInfo->ChunkIndex, &ResponseOverride);	// ChunkIndex is the last chunk made visible.  But SetCollisionResponseForActor already doesn't respect per-chunk collision properties.
				}
			}
		}

		if (LockedScene)
		{
			LockedScene->unlockRead();
			LockedScene->unlockWrite();
			LockedScene = NULL;
		}

		ApexDestructibleActor->releasePhysXActorBuffer();
	}
#endif
}

void UDestructibleComponent::SetCollisionResponseForShape(PxShape* Shape, int32 ChunkIdx)
{
	// Get collision channel and response
	PxFilterData PQueryFilterData, PSimFilterData;
	uint8 MoveChannel = GetCollisionObjectType();
	if (IsCollisionEnabled())
	{
		AActor* Owner = GetOwner();
		bool bLargeChunk = IsChunkLarge(ChunkIdx);
		const FCollisionResponse& ColResponse = bLargeChunk ? LargeChunkCollisionResponse : SmallChunkCollisionResponse;
		//TODO: we currently assume chunks will not have impact damage as it's very expensive. Should look into exposing this a bit more
		CreateShapeFilterData(MoveChannel, (Owner ? Owner->GetUniqueID() : 0), ColResponse.GetResponseContainer(), 0, ChunkIdxToBoneIdx(ChunkIdx), PQueryFilterData, PSimFilterData, BodyInstance.bUseCCD, false, false);

		PQueryFilterData.word3 |= EPDF_SimpleCollision | EPDF_ComplexCollision;

		SCOPED_SCENE_WRITE_LOCK(Shape->getActor()->getScene());

		Shape->setQueryFilterData(PQueryFilterData);
		Shape->setSimulationFilterData(PSimFilterData);
		Shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
		Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
		Shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
	}
}

void UDestructibleComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* Material)
{
	// Mesh component handles render side materials
	Super::SetMaterial(ElementIndex, Material);

	// Update physical properties of the chunks in the mesh
	GetBodyInstance()->UpdatePhysicalMaterials();
	int32 NumBones = SkeletalMesh->RefSkeleton.GetNum();
	for(int32 BoneIdx = 0 ; BoneIdx < NumBones ; ++BoneIdx)
	{
		FName BoneName = SkeletalMesh->RefSkeleton.GetBoneName(BoneIdx);
		if(FBodyInstance* Instance = GetBodyInstance(BoneName))
		{
			Instance->UpdatePhysicalMaterials();
		}
	}

#if WITH_APEX
	// Set new template parameters for the apex actor, so they take effect before fracturing too.
	if(ApexDestructibleActor)
	{
		physx::apex::NxPhysX3DescTemplate* Template = ApexDestructibleActor->createPhysX3DescTemplate();
		if(ApexDestructibleActor->getPhysX3Template(*Template))
		{
			UPhysicalMaterial* SimpleMaterial = GetBodyInstance()->GetSimplePhysicalMaterial();
			check(SimpleMaterial);
			PxMaterial* PhysxMat = SimpleMaterial->GetPhysXMaterial();

			Template->setMaterials(&PhysxMat, 1);

			ApexDestructibleActor->setPhysX3Template(Template);
		}
		Template->release();
	}
#endif
}

#endif // WITH_PHYSX
