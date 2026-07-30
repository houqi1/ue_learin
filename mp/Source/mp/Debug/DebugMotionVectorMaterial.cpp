// Editor helper: create /Game/FX/Debug/M_PP_DebugMotionVector (Post Process Velocity visualize).
// Console: mp.CreateDebugMotionVectorMaterial
// Spawn host: mp.SpawnDebugMotionVectorView

#include "DebugMotionVectorView.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogDebugMVMat, Log, All);

namespace DebugMotionVectorMaterial
{
	static constexpr const TCHAR* PackagePath = TEXT("/Game/FX/Debug/M_PP_DebugMotionVector");
	static constexpr const TCHAR* AssetName = TEXT("M_PP_DebugMotionVector");
	static constexpr const TCHAR* ObjectPath =
		TEXT("/Game/FX/Debug/M_PP_DebugMotionVector.M_PP_DebugMotionVector");

#if WITH_EDITOR
	static UMaterial* CreateMotionVectorMaterialAsset(bool bForceRecreate)
	{
		if (!bForceRecreate)
		{
			if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, ObjectPath))
			{
				UE_LOG(LogDebugMVMat, Warning, TEXT("Material already exists: %s"), ObjectPath);
				return Existing;
			}
		}
		else
		{
			// Soft delete package if present so graph is rebuilt cleanly.
			if (UPackage* OldPkg = FindPackage(nullptr, PackagePath))
			{
				OldPkg->ClearFlags(RF_Standalone | RF_Public);
				OldPkg->SetFlags(RF_Transient);
			}
		}

		UPackage* Package = CreatePackage(PackagePath);
		if (!Package)
		{
			UE_LOG(LogDebugMVMat, Error, TEXT("CreatePackage failed for %s"), PackagePath);
			return nullptr;
		}
		Package->FullyLoad();

		UMaterial* Mat = NewObject<UMaterial>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!Mat)
		{
			return nullptr;
		}

		Mat->MaterialDomain = MD_PostProcess;
		Mat->BlendableLocation = BL_AfterTonemapping;
		Mat->SetShadingModel(MSM_Unlit);

		auto AddExpr = [Mat](UMaterialExpression* E, int32 X, int32 Y) -> UMaterialExpression*
		{
			if (!E)
			{
				return nullptr;
			}
			E->Material = Mat;
			E->MaterialExpressionEditorX = X;
			E->MaterialExpressionEditorY = Y;
			Mat->GetExpressionCollection().AddExpression(E);
			return E;
		};

		// SceneTexture (Velocity)
		UMaterialExpressionSceneTexture* SceneTex = NewObject<UMaterialExpressionSceneTexture>(Mat);
		if (SceneTex)
		{
			SceneTex->SceneTextureId = PPI_Velocity;
			AddExpr(SceneTex, -900, 0);
		}

		// Exposure scalar
		UMaterialExpressionScalarParameter* Exposure = NewObject<UMaterialExpressionScalarParameter>(Mat);
		if (Exposure)
		{
			Exposure->ParameterName = TEXT("Exposure");
			Exposure->DefaultValue = 30.f;
			AddExpr(Exposure, -900, 220);
		}

		// Mask R,G
		UMaterialExpressionComponentMask* Mask = NewObject<UMaterialExpressionComponentMask>(Mat);
		if (Mask)
		{
			Mask->R = true;
			Mask->G = true;
			Mask->B = false;
			Mask->A = false;
			AddExpr(Mask, -600, 0);
			if (SceneTex)
			{
				// SceneTexture Color is output index 0
				Mask->Input.Connect(0, SceneTex);
			}
		}

		// * Exposure
		UMaterialExpressionMultiply* Mul = NewObject<UMaterialExpressionMultiply>(Mat);
		if (Mul)
		{
			AddExpr(Mul, -350, 0);
			if (Mask)
			{
				Mul->A.Connect(0, Mask);
			}
			if (Exposure)
			{
				Mul->B.Connect(0, Exposure);
			}
		}

		// Append 0 -> float3
		UMaterialExpressionConstant* Zero = NewObject<UMaterialExpressionConstant>(Mat);
		if (Zero)
		{
			Zero->R = 0.f;
			AddExpr(Zero, -350, 180);
		}
		UMaterialExpressionAppendVector* Append = NewObject<UMaterialExpressionAppendVector>(Mat);
		if (Append)
		{
			AddExpr(Append, -150, 0);
			if (Mul)
			{
				Append->A.Connect(0, Mul);
			}
			if (Zero)
			{
				Append->B.Connect(0, Zero);
			}
		}

		// + (0.5, 0.5, 0)
		UMaterialExpressionConstant3Vector* Bias = NewObject<UMaterialExpressionConstant3Vector>(Mat);
		if (Bias)
		{
			Bias->Constant = FLinearColor(0.5f, 0.5f, 0.f, 0.f);
			AddExpr(Bias, -150, 200);
		}
		UMaterialExpressionAdd* Add = NewObject<UMaterialExpressionAdd>(Mat);
		if (Add)
		{
			AddExpr(Add, 80, 0);
			if (Append)
			{
				Add->A.Connect(0, Append);
			}
			if (Bias)
			{
				Add->B.Connect(0, Bias);
			}
		}

#if WITH_EDITORONLY_DATA
		if (Mat->GetEditorOnlyData() && Add)
		{
			Mat->GetEditorOnlyData()->EmissiveColor.Expression = Add;
			Mat->GetEditorOnlyData()->EmissiveColor.OutputIndex = 0;
		}
#endif

		Mat->PreEditChange(nullptr);
		Mat->PostEditChange();
		FAssetRegistryModule::AssetCreated(Mat);
		Package->MarkPackageDirty();

		// Save to disk so Content Browser sees it without manual save.
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			PackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GError;
		const bool bSaved = UPackage::SavePackage(Package, Mat, *PackageFilename, SaveArgs);
		UE_LOG(LogDebugMVMat, Warning,
			TEXT("Created M_PP_DebugMotionVector | saved=%d path=%s"),
			bSaved ? 1 : 0, *PackageFilename);
		return Mat;
	}
#endif // WITH_EDITOR
}

static FAutoConsoleCommand CCmdCreateDebugMVMat(
	TEXT("mp.CreateDebugMotionVectorMaterial"),
	TEXT("Editor: create/rebuild Post Process material /Game/FX/Debug/M_PP_DebugMotionVector (Velocity visualize)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
#if WITH_EDITOR
		const bool bForce = Args.Contains(TEXT("force")) || Args.Contains(TEXT("-force"));
		if (UMaterial* Mat = DebugMotionVectorMaterial::CreateMotionVectorMaterialAsset(bForce))
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(
					-1, 5.f, FColor::Green,
					FString::Printf(TEXT("MV material ready: %s"), *Mat->GetPathName()));
			}
		}
#else
		UE_LOG(LogDebugMVMat, Error, TEXT("mp.CreateDebugMotionVectorMaterial is editor-only"));
#endif
	}));

static FAutoConsoleCommand CCmdSpawnDebugMVView(
	TEXT("mp.SpawnDebugMotionVectorView"),
	TEXT("Spawn ADebugMotionVectorView in the current world (runtime MV overlay)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UWorld* World = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game)
				{
					World = Ctx.World();
					break;
				}
			}
			if (!World)
			{
				for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
				{
					if (Ctx.WorldType == EWorldType::Editor)
					{
						World = Ctx.World();
						break;
					}
				}
			}
		}
		if (!World)
		{
			UE_LOG(LogDebugMVMat, Error, TEXT("No world for SpawnDebugMotionVectorView"));
			return;
		}
#if WITH_EDITOR
		// Ensure material exists when spawning from editor.
		DebugMotionVectorMaterial::CreateMotionVectorMaterialAsset(/*bForceRecreate=*/false);
#endif
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ADebugMotionVectorView* A = World->SpawnActor<ADebugMotionVectorView>(
			ADebugMotionVectorView::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (A)
		{
			A->SetOverlayEnabled(true);
			UE_LOG(LogDebugMVMat, Warning, TEXT("Spawned ADebugMotionVectorView: %s"), *A->GetName());
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan, TEXT("Debug Motion Vector View spawned"));
			}
		}
	}));
