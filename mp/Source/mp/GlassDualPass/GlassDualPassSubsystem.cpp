// Copyright Epic Games, Inc. All Rights Reserved.

#include "GlassDualPassSubsystem.h"

#include "GlassDualPassFront.h"
#include "GlassDualPassLogs.h"
#include "GlassDualPassMaterial.h"
#include "GlassDualPassViewExtension.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CoreDelegates.h"
#include "SceneViewExtension.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#endif

namespace GlassDualPassPrivate
{
	static const TCHAR* PreviewPackagePath = TEXT("/Game/Phonix/RT_GlassBack");
	static const TCHAR* PreviewAssetName = TEXT("RT_GlassBack");

	static const TCHAR* DataAPath = TEXT("/Game/Phonix/textures/T_Phoenix_DataA.T_Phoenix_DataA");
	static const TCHAR* DataBPath = TEXT("/Game/Phonix/textures/T_Phoenix_DataB.T_Phoenix_DataB");
	static const TCHAR* EnvMapPath = TEXT("/Game/Phonix/textures/wooden_studio_19_1k.wooden_studio_19_1k");
	static const TCHAR* ColorsMapPath = TEXT("/Game/Phonix/textures/colorsMap.colorsMap");
}

void UGlassDualPassSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ViewExtension = FSceneViewExtensions::NewExtension<FGlassDualPassViewExtension>();
	// Defer material load until engine is fully up (avoid any editor subsystem order issues).
	PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddUObject(this, &UGlassDualPassSubsystem::OnPostEngineInit);
	UE_LOG(LogGlassDualPass, Log,
		TEXT("UGlassDualPassSubsystem initialized. Backface: Material Mesh Shader (MI M_PhoneixGlass_Back_Inst) → RT_GlassBack. Front GlassBackRT bind."));
}

void UGlassDualPassSubsystem::OnPostEngineInit()
{
	EnsureBackfaceMaterial();
}

void UGlassDualPassSubsystem::Deinitialize()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
	ViewExtension.Reset();
	PreviewRT = nullptr;
	SceneColorCopyRT = nullptr;
	DataA = DataB = ColorsMap = nullptr;
	EnvMap = nullptr;
	BackfaceMaterial = nullptr;
	BackfaceMID = nullptr;
	UE_LOG(LogGlassDualPass, Log, TEXT("UGlassDualPassSubsystem deinitialized"));
	Super::Deinitialize();
}

void UGlassDualPassSubsystem::ApplyFrontGlassBindings(UWorld* World, float BackfaceWeight)
{
	UTextureRenderTarget2D* RT = GetOrCreatePreviewRT();
	GlassDualPassFront::ApplyBackfaceRTToGlassMaterials(World, RT, BackfaceWeight);
}

UTextureRenderTarget2D* UGlassDualPassSubsystem::GetOrCreateSceneColorCopyRT(int32 SizeX, int32 SizeY)
{
	SizeX = FMath::Clamp(SizeX, 8, 1280);
	SizeY = FMath::Clamp(SizeY, 8, 1280);
	if (!IsValid(SceneColorCopyRT))
	{
		SceneColorCopyRT = NewObject<UTextureRenderTarget2D>(this, TEXT("GlassSceneColorCopy"), RF_Transient);
		SceneColorCopyRT->RenderTargetFormat = RTF_RGBA16f;
		SceneColorCopyRT->bAutoGenerateMips = false;
		SceneColorCopyRT->ClearColor = FLinearColor::Black;
		SceneColorCopyRT->InitAutoFormat(SizeX, SizeY);
		SceneColorCopyRT->UpdateResourceImmediate(true);
	}
	else if (SceneColorCopyRT->SizeX != SizeX || SceneColorCopyRT->SizeY != SizeY)
	{
		SceneColorCopyRT->InitAutoFormat(SizeX, SizeY);
		SceneColorCopyRT->UpdateResourceImmediate(true);
	}
	return SceneColorCopyRT;
}

void UGlassDualPassSubsystem::ResetBackfaceMaterialLoadAttempt()
{
	bTriedBackfaceMaterial = false;
	BackfaceMaterial = nullptr;
	BackfaceMID = nullptr;
}

void UGlassDualPassSubsystem::EnsureBackfaceMaterial()
{
	if (IsValid(BackfaceMaterial) && IsValid(BackfaceMID))
	{
		return;
	}
	// One attempt per session: LoadObject only (no MaterialEditingLibrary — crashes at init).
	if (bTriedBackfaceMaterial)
	{
		return;
	}
	bTriedBackfaceMaterial = true;

	BackfaceMaterial = GlassDualPassMaterial::LoadOrCreateBackfaceMaterial();
	if (IsValid(BackfaceMaterial))
	{
		// DMI parent = MIC (M_PhoneixGlass_Back_Inst) or master — inherits instance params.
		// Runtime only injects SceneColor + view metrics; effect knobs stay on the MI.
		BackfaceMID = UMaterialInstanceDynamic::Create(BackfaceMaterial, this);
		UE_LOG(LogGlassDualPass, Log,
			TEXT("Backface render material ready: %s (DMI=%s) — edit MI in editor for effect params"),
			*BackfaceMaterial->GetPathName(),
			IsValid(BackfaceMID) ? TEXT("yes") : TEXT("no"));
	}
}

GlassDualPassMaterial::FShadingParams UGlassDualPassSubsystem::GatherShadingParamsFromMaterial()
{
	EnsureBackfaceMaterial();
	EnsureShadingTextures();

	GlassDualPassMaterial::FShadingParams Params;
	if (IsValid(BackfaceMaterial) && GlassDualPassMaterial::ReadShadingParamsFromMaterial(BackfaceMaterial, Params))
	{
		// Empty texture pins on the material → subsystem fallbacks (content defaults).
		if (!IsValid(Params.DataA))
		{
			Params.DataA = DataA;
		}
		if (!IsValid(Params.DataB))
		{
			Params.DataB = DataB;
		}
		if (!IsValid(Params.EnvMap))
		{
			Params.EnvMap = EnvMap;
		}
		if (!IsValid(Params.ColorsMap))
		{
			Params.ColorsMap = ColorsMap;
		}
		return Params;
	}

	// No material asset: CVars + subsystem textures (legacy fallback).
	GlassDualPassMaterial::FillShadingParamsFromCVars(Params);
	Params.DataA = DataA;
	Params.DataB = DataB;
	Params.EnvMap = EnvMap;
	Params.ColorsMap = ColorsMap;
	return Params;
}

void UGlassDualPassSubsystem::InjectRuntimeIntoBackfaceMID(
	UTexture* SceneColor,
	const FVector2f& SceneViewRectMin,
	const FVector2f& SceneViewSize,
	const FVector2f& SceneBufferInvSize,
	float SceneEdgeSoftness,
	const FMatrix44f& ViewProjection,
	const FVector3f& CameraWorldPos)
{
	EnsureBackfaceMaterial();
	if (!IsValid(BackfaceMID))
	{
		return;
	}
	GlassDualPassMaterial::InjectRuntimeViewIntoMID(
		BackfaceMID,
		SceneColor,
		SceneEdgeSoftness,
		SceneViewRectMin,
		SceneViewSize,
		SceneBufferInvSize,
		ViewProjection,
		CameraWorldPos);
}

void UGlassDualPassSubsystem::EnsureShadingTextures()
{
	if (!IsValid(DataA) && !bTriedLoadDataA)
	{
		bTriedLoadDataA = true;
		DataA = LoadObject<UTexture2D>(nullptr, GlassDualPassPrivate::DataAPath);
		if (DataA)
		{
			// Soft-request mips; do NOT WaitForStreaming (blocks GT / spikes memory).
			DataA->SetForceMipLevelsToBeResident(10.f);
		}
		else
		{
			UE_LOG(LogGlassDualPass, Warning, TEXT("Failed to load DataA (once): %s"), GlassDualPassPrivate::DataAPath);
		}
	}

	if (!IsValid(DataB) && !bTriedLoadDataB)
	{
		bTriedLoadDataB = true;
		DataB = LoadObject<UTexture2D>(nullptr, GlassDualPassPrivate::DataBPath);
		if (DataB)
		{
			DataB->SetForceMipLevelsToBeResident(10.f);
		}
		else
		{
			UE_LOG(LogGlassDualPass, Warning, TEXT("Failed to load DataB (once): %s"), GlassDualPassPrivate::DataBPath);
		}
	}

	if (!IsValid(EnvMap) && !bTriedLoadEnvMap)
	{
		bTriedLoadEnvMap = true;
		// Asset is TextureCube (HDR cubemap), not Texture2D.
		EnvMap = LoadObject<UTextureCube>(nullptr, GlassDualPassPrivate::EnvMapPath);
		if (EnvMap)
		{
			EnvMap->SetForceMipLevelsToBeResident(10.f);
			UE_LOG(LogGlassDualPass, Log, TEXT("Loaded env cubemap: %s"), GlassDualPassPrivate::EnvMapPath);
		}
		else
		{
			UE_LOG(LogGlassDualPass, Warning,
				TEXT("Failed to load EnvMap as TextureCube (once): %s"), GlassDualPassPrivate::EnvMapPath);
		}
	}

	if (!IsValid(ColorsMap) && !bTriedLoadColorsMap)
	{
		bTriedLoadColorsMap = true;
		ColorsMap = LoadObject<UTexture2D>(nullptr, GlassDualPassPrivate::ColorsMapPath);
		if (ColorsMap)
		{
			ColorsMap->SetForceMipLevelsToBeResident(10.f);
		}
		else
		{
			UE_LOG(LogGlassDualPass, Warning, TEXT("Failed to load ColorsMap (once): %s"), GlassDualPassPrivate::ColorsMapPath);
		}
	}
}

UTextureRenderTarget2D* UGlassDualPassSubsystem::GetOrCreatePreviewRT()
{
	if (IsValid(PreviewRT))
	{
		return PreviewRT;
	}

	const FString ObjectPath = FString::Printf(
		TEXT("%s.%s"),
		GlassDualPassPrivate::PreviewPackagePath,
		GlassDualPassPrivate::PreviewAssetName);

	if (UTextureRenderTarget2D* Existing = LoadObject<UTextureRenderTarget2D>(nullptr, *ObjectPath))
	{
		PreviewRT = Existing;
		// Drop legacy Step2 magenta clear so idle RT is black, not pink.
		PreviewRT->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
		if (PreviewRT->SizeX < 8 || PreviewRT->SizeY < 8)
		{
			PreviewRT->RenderTargetFormat = RTF_RGBA8;
			PreviewRT->bAutoGenerateMips = false;
			PreviewRT->InitAutoFormat(1280, 720);
		}
		PreviewRT->UpdateResourceImmediate(true);
		UE_LOG(LogGlassDualPass, Log, TEXT("Using existing preview RT: %s (%dx%d)"),
			*ObjectPath, PreviewRT->SizeX, PreviewRT->SizeY);
		return PreviewRT;
	}

#if WITH_EDITOR
	UPackage* Package = CreatePackage(GlassDualPassPrivate::PreviewPackagePath);
	Package->FullyLoad();
	PreviewRT = NewObject<UTextureRenderTarget2D>(
		Package, GlassDualPassPrivate::PreviewAssetName, RF_Public | RF_Standalone);
	PreviewRT->RenderTargetFormat = RTF_RGBA8;
	PreviewRT->bAutoGenerateMips = false;
	PreviewRT->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
	PreviewRT->InitAutoFormat(1280, 720);
	PreviewRT->UpdateResourceImmediate(true);
	FAssetRegistryModule::AssetCreated(PreviewRT);
	Package->MarkPackageDirty();
	UE_LOG(LogGlassDualPass, Log, TEXT("Created in-memory preview RT: %s"), *ObjectPath);
#else
	PreviewRT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), GlassDualPassPrivate::PreviewAssetName, RF_Transient);
	PreviewRT->RenderTargetFormat = RTF_RGBA8;
	PreviewRT->bAutoGenerateMips = false;
	PreviewRT->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
	PreviewRT->InitAutoFormat(1280, 720);
	PreviewRT->UpdateResourceImmediate(true);
	PreviewRT->AddToRoot();
#endif
	return PreviewRT;
}

void UGlassDualPassSubsystem::EnsurePreviewRTSize(int32 SizeX, int32 SizeY)
{
	UTextureRenderTarget2D* RT = GetOrCreatePreviewRT();
	if (!IsValid(RT))
	{
		return;
	}
	// Cap RT resolution to limit VRAM (full 4K dual-buffer is expensive at startup).
	static constexpr int32 MaxRTDim = 1280;
	SizeX = FMath::Clamp(SizeX, 8, MaxRTDim);
	SizeY = FMath::Clamp(SizeY, 8, MaxRTDim);
	const bool bSizeOk = (RT->SizeX == SizeX && RT->SizeY == SizeY);
	const bool bHasResource = (RT->GetResource() != nullptr);
	if (bSizeOk && bHasResource)
	{
		return;
	}
	if (!bSizeOk)
	{
		RT->RenderTargetFormat = RTF_RGBA8;
		RT->bAutoGenerateMips = false;
		RT->InitAutoFormat(SizeX, SizeY);
	}
	RT->UpdateResourceImmediate(true);
}

FTextureRenderTargetResource* UGlassDualPassSubsystem::GetPreviewRTResource()
{
	UTextureRenderTarget2D* RT = GetOrCreatePreviewRT();
	if (!IsValid(RT))
	{
		return nullptr;
	}
	FTextureRenderTargetResource* Resource = RT->GameThread_GetRenderTargetResource();
	if (!Resource || !Resource->GetRenderTargetTexture())
	{
		RT->UpdateResourceImmediate(true);
		Resource = RT->GameThread_GetRenderTargetResource();
	}
	if (!Resource)
	{
		Resource = static_cast<FTextureRenderTargetResource*>(RT->GetResource());
	}
	return Resource;
}
