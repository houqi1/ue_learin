// Copyright Epic Games, Inc. All Rights Reserved.

#include "GlassDualPassSubsystem.h"

#include "GlassDualPassLogs.h"
#include "GlassDualPassViewExtension.h"
#include "Engine/TextureRenderTarget2D.h"
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
}

void UGlassDualPassSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Only register the view extension here.
	// Do NOT create/save assets during Engine init — SavePackage can fire editor
	// delegates (e.g. SemanticSearch) before TimerManager is valid and crash.
	ViewExtension = FSceneViewExtensions::NewExtension<FGlassDualPassViewExtension>();

	UE_LOG(LogGlassDualPass, Log,
		TEXT("UGlassDualPassSubsystem initialized. Glass=M_PhoneixGlass(+MI). Skeletal=GPU SkinCache positions."));
}

void UGlassDualPassSubsystem::Deinitialize()
{
	ViewExtension.Reset();
	PreviewRT = nullptr;
	UE_LOG(LogGlassDualPass, Log, TEXT("UGlassDualPassSubsystem deinitialized"));
	Super::Deinitialize();
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

	// Prefer already-saved content asset.
	if (UTextureRenderTarget2D* Existing = LoadObject<UTextureRenderTarget2D>(nullptr, *ObjectPath))
	{
		PreviewRT = Existing;
		// Content RT often has no GPU resource until first UpdateResource.
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
	// Create an in-editor package object WITHOUT SavePackage during early startup.
	// User can Save in Content Browser later if desired. Avoids OnPackageSaved crashes.
	UPackage* Package = CreatePackage(GlassDualPassPrivate::PreviewPackagePath);
	Package->FullyLoad();

	PreviewRT = NewObject<UTextureRenderTarget2D>(
		Package,
		GlassDualPassPrivate::PreviewAssetName,
		RF_Public | RF_Standalone);

	PreviewRT->RenderTargetFormat = RTF_RGBA8;
	PreviewRT->bAutoGenerateMips = false;
	PreviewRT->ClearColor = FLinearColor(1.f, 0.f, 1.f, 1.f);
	PreviewRT->InitAutoFormat(1280, 720);
	PreviewRT->UpdateResourceImmediate(true);

	FAssetRegistryModule::AssetCreated(PreviewRT);
	Package->MarkPackageDirty();

	UE_LOG(LogGlassDualPass, Log,
		TEXT("Created in-memory preview RT: %s (not auto-saved; assign to Unlit plane). "
			 "Save via Content Browser if you want it on disk."),
		*ObjectPath);
#else
	PreviewRT = NewObject<UTextureRenderTarget2D>(
		GetTransientPackage(),
		GlassDualPassPrivate::PreviewAssetName,
		RF_Transient);
	PreviewRT->RenderTargetFormat = RTF_RGBA8;
	PreviewRT->bAutoGenerateMips = false;
	PreviewRT->ClearColor = FLinearColor(1.f, 0.f, 1.f, 1.f);
	PreviewRT->InitAutoFormat(1280, 720);
	PreviewRT->UpdateResourceImmediate(true);
	PreviewRT->AddToRoot();
	UE_LOG(LogGlassDualPass, Log, TEXT("Created transient preview RT (non-editor build)"));
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

	SizeX = FMath::Clamp(SizeX, 8, 8192);
	SizeY = FMath::Clamp(SizeY, 8, 8192);

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
		UE_LOG(LogGlassDualPass, Verbose, TEXT("Preview RT resized to %dx%d"), SizeX, SizeY);
	}

	// Always rebuild GPU resource when missing or after size change.
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
		// First frames / after load: force RHI creation on render thread then wait.
		RT->UpdateResourceImmediate(true);
		Resource = RT->GameThread_GetRenderTargetResource();
	}

	if (!Resource)
	{
		Resource = static_cast<FTextureRenderTargetResource*>(RT->GetResource());
	}

	return Resource;
}
