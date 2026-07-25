// Copyright Epic Games, Inc. All Rights Reserved.

#include "GlassDualPassFront.h"
#include "GlassDualPassLogs.h"

#include "Components/MeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

static bool MaterialIsFrontGlass(const UMaterialInterface* Material)
{
	if (!IsValid(Material))
	{
		return false;
	}
	// Keep in sync with r.GlassDualPass.MasterMaterial default (M_PhoneixGlass).
	static const FString MasterName(TEXT("M_PhoneixGlass"));
	const UMaterialInterface* Current = Material;
	while (IsValid(Current))
	{
		if (Current->GetName().Equals(MasterName, ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (const UMaterialInstance* MI = Cast<UMaterialInstance>(Current))
		{
			Current = MI->Parent;
			continue;
		}
		break;
	}
	return false;
}

namespace GlassDualPassFront
{
	static void ApplyToMeshComponent(UMeshComponent* MeshComp, UTextureRenderTarget2D* GlassBackRT, float BackfaceWeight)
	{
		if (!IsValid(MeshComp) || !IsValid(GlassBackRT))
		{
			return;
		}

		const int32 Num = MeshComp->GetNumMaterials();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			UMaterialInterface* Mat = MeshComp->GetMaterial(Index);
			if (!MaterialIsFrontGlass(Mat))
			{
				continue;
			}

			UMaterialInstanceDynamic* DMI = Cast<UMaterialInstanceDynamic>(Mat);
			if (!DMI)
			{
				DMI = MeshComp->CreateDynamicMaterialInstance(Index, Mat);
			}
			if (!DMI)
			{
				continue;
			}

			DMI->SetTextureParameterValue(GlassBackRTParam, GlassBackRT);
			DMI->SetScalarParameterValue(BackfaceWeightParam, BackfaceWeight);
		}
	}

	void ApplyBackfaceRTToGlassMaterials(UWorld* World, UTextureRenderTarget2D* GlassBackRT, float BackfaceWeight)
	{
		if (!IsValid(World) || !IsValid(GlassBackRT))
		{
			return;
		}

		for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
		{
			AActor* Actor = *ActorIt;
			if (!IsValid(Actor))
			{
				continue;
			}

			TInlineComponentArray<UMeshComponent*> Meshes;
			Actor->GetComponents(Meshes);
			for (UMeshComponent* Mesh : Meshes)
			{
				if (!IsValid(Mesh) || !Mesh->IsRegistered() || !Mesh->IsVisible())
				{
					continue;
				}
				ApplyToMeshComponent(Mesh, GlassBackRT, BackfaceWeight);
			}
		}

		static double LastLog = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastLog > 5.0)
		{
			LastLog = Now;
			UE_LOG(LogGlassDualPass, Log,
				TEXT("Front dual-pass: GlassBackRT + BackfaceWeight=%.2f applied (switch Custom to PhoneixGlassDualFront.usf if not yet)"),
				BackfaceWeight);
		}
	}
}
