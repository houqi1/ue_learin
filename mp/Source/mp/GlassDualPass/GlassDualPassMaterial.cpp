// Copyright Epic Games, Inc. All Rights Reserved.

#include "GlassDualPassMaterial.h"
#include "GlassDualPassLogs.h"
#include "GlassDualPassSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#endif

namespace GlassDualPassMaterial
{
#if WITH_EDITOR
	/**
	 * Create material with NewObject only — never UMaterialEditingLibrary
	 * (that path crashes if AssetEditorSubsystem is not ready).
	 * Call only after engine/editor is up (console cmd or PostEngineInit).
	 */
	static UMaterial* CreateBackfaceMaterialNewObject()
	{
		const FString PackagePath = TEXT("/Game/Phonix/Material/M_PhoneixGlass_Back");
		const FString AssetName = TEXT("M_PhoneixGlass_Back");

		if (UMaterialInterface* Existing = LoadObject<UMaterialInterface>(nullptr, BackfaceMaterialPath))
		{
			return Cast<UMaterial>(Existing);
		}

		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			return nullptr;
		}
		Package->FullyLoad();

		UMaterial* Mat = NewObject<UMaterial>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!Mat)
		{
			return nullptr;
		}

		Mat->MaterialDomain = MD_Surface;
		Mat->BlendMode = BLEND_Opaque;
		Mat->SetShadingModel(MSM_Unlit);
		Mat->TwoSided = true;
		Mat->bTangentSpaceNormal = false;

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

		UMaterialExpressionWorldPosition* WorldPos = NewObject<UMaterialExpressionWorldPosition>(Mat);
		AddExpr(WorldPos, -1200, 0);
		UMaterialExpressionVertexNormalWS* Nrm = NewObject<UMaterialExpressionVertexNormalWS>(Mat);
		AddExpr(Nrm, -1200, 120);
		UMaterialExpressionCameraPositionWS* Cam = NewObject<UMaterialExpressionCameraPositionWS>(Mat);
		AddExpr(Cam, -1200, 240);
		UMaterialExpressionTextureCoordinate* UV1 = NewObject<UMaterialExpressionTextureCoordinate>(Mat);
		if (UV1)
		{
			UV1->CoordinateIndex = 1;
			AddExpr(UV1, -1200, 360);
		}

		auto AddScalar = [&](const TCHAR* Name, float Def, int32 Y) -> UMaterialExpressionScalarParameter*
		{
			auto* E = NewObject<UMaterialExpressionScalarParameter>(Mat);
			if (E)
			{
				E->ParameterName = Name;
				E->DefaultValue = Def;
				AddExpr(E, -900, Y);
			}
			return E;
		};
		auto AddVector = [&](const TCHAR* Name, FLinearColor Def, int32 Y) -> UMaterialExpressionVectorParameter*
		{
			auto* E = NewObject<UMaterialExpressionVectorParameter>(Mat);
			if (E)
			{
				E->ParameterName = Name;
				E->DefaultValue = Def;
				AddExpr(E, -900, Y);
			}
			return E;
		};
		auto AddTex = [&](const TCHAR* Name, int32 Y) -> UMaterialExpressionTextureObjectParameter*
		{
			auto* E = NewObject<UMaterialExpressionTextureObjectParameter>(Mat);
			if (E)
			{
				E->ParameterName = Name;
				AddExpr(E, -900, Y);
			}
			return E;
		};

		auto* Ior = AddScalar(TEXT("IorStart"), 1.45f, 0);
		auto* UseTrans = AddScalar(TEXT("UseTransmittance"), 1.f, 80);
		auto* EnvRefr = AddScalar(TEXT("EnvRefraction"), 0.35f, 160);
		auto* FringeCurve = AddScalar(TEXT("FringeCurve"), 2.f, 240);
		auto* FringeMix = AddScalar(TEXT("FringeMix"), 0.25f, 320);
		auto* Irid = AddScalar(TEXT("RefractionIridescence"), 0.85f, 400);
		auto* DistScale = AddScalar(TEXT("DistScale"), 1.f, 480);
		auto* EdgeSoft = AddScalar(TEXT("SceneEdgeSoftness"), 0.02f, 560);
		auto* FringeCol = AddVector(TEXT("FringeColor"), FLinearColor(0.55f, 0.75f, 1.f), 640);
		auto* ViewRectMin = AddVector(TEXT("SceneViewRectMin"), FLinearColor(0, 0, 0, 0), 720);
		auto* ViewSize = AddVector(TEXT("SceneViewSize"), FLinearColor(1920, 1080, 0, 0), 800);
		auto* BufInv = AddVector(TEXT("SceneBufferInvSize"), FLinearColor(1.f / 1920.f, 1.f / 1080.f, 0, 0), 880);
		auto* VP0 = AddVector(TEXT("ViewProjection0"), FLinearColor(1, 0, 0, 0), 960);
		auto* VP1 = AddVector(TEXT("ViewProjection1"), FLinearColor(0, 1, 0, 0), 1040);
		auto* VP2 = AddVector(TEXT("ViewProjection2"), FLinearColor(0, 0, 1, 0), 1120);
		auto* VP3 = AddVector(TEXT("ViewProjection3"), FLinearColor(0, 0, 0, 1), 1200);
		auto* TexScene = AddTex(TEXT("SceneColorTexture"), 1320);
		auto* TexDataA = AddTex(TEXT("DataATexture"), 1400);
		auto* TexDataB = AddTex(TEXT("DataBTexture"), 1480);
		auto* TexEnv = AddTex(TEXT("EnvMapTexture"), 1560);
		auto* TexColors = AddTex(TEXT("ColorsMapTexture"), 1640);

		UMaterialExpressionCustom* Custom = NewObject<UMaterialExpressionCustom>(Mat);
		if (!Custom)
		{
			return nullptr;
		}
		Custom->Description = TEXT("GlassDualPassBackface");
		Custom->OutputType = CMOT_Float3;
		// Multi-line + "return" word so UE does not wrap as return #include "...";
		Custom->Code = TEXT("// return is provided by included usf\n#include \"/Project/GlassDualPassBackfaceCustom.usf\"");
		AddExpr(Custom, -200, 400);

		auto ConnectIn = [Custom](const FName Name, UMaterialExpression* Expr)
		{
			if (!Expr)
			{
				return;
			}
			FCustomInput In;
			In.InputName = Name;
			In.Input.Connect(0, Expr);
			Custom->Inputs.Add(In);
		};

		ConnectIn(TEXT("WorldPos"), WorldPos);
		ConnectIn(TEXT("WorldNormal"), Nrm);
		ConnectIn(TEXT("UV1"), UV1);
		ConnectIn(TEXT("CameraWorldPos"), Cam);
		ConnectIn(TEXT("ViewProjection0"), VP0);
		ConnectIn(TEXT("ViewProjection1"), VP1);
		ConnectIn(TEXT("ViewProjection2"), VP2);
		ConnectIn(TEXT("ViewProjection3"), VP3);
		ConnectIn(TEXT("SceneViewRectMin"), ViewRectMin);
		ConnectIn(TEXT("SceneViewSize"), ViewSize);
		ConnectIn(TEXT("SceneBufferInvSize"), BufInv);
		ConnectIn(TEXT("SceneEdgeSoftness"), EdgeSoft);
		ConnectIn(TEXT("IorStart"), Ior);
		ConnectIn(TEXT("UseTransmittance"), UseTrans);
		ConnectIn(TEXT("EnvRefraction"), EnvRefr);
		ConnectIn(TEXT("FringeCurve"), FringeCurve);
		ConnectIn(TEXT("FringeMix"), FringeMix);
		ConnectIn(TEXT("FringeColor"), FringeCol);
		ConnectIn(TEXT("RefractionIridescence"), Irid);
		ConnectIn(TEXT("DistScale"), DistScale);
		ConnectIn(TEXT("DataATexture"), TexDataA);
		ConnectIn(TEXT("DataBTexture"), TexDataB);
		ConnectIn(TEXT("EnvMapTexture"), TexEnv);
		ConnectIn(TEXT("ColorsMapTexture"), TexColors);
		ConnectIn(TEXT("SceneColorTexture"), TexScene);

#if WITH_EDITORONLY_DATA
		if (Mat->GetEditorOnlyData())
		{
			Mat->GetEditorOnlyData()->EmissiveColor.Expression = Custom;
			Mat->GetEditorOnlyData()->EmissiveColor.OutputIndex = 0;
		}
#endif

		Mat->PreEditChange(nullptr);
		Mat->PostEditChange();
		FAssetRegistryModule::AssetCreated(Mat);
		Package->MarkPackageDirty();

		UE_LOG(LogGlassDualPass, Log,
			TEXT("Created M_PhoneixGlass_Back via NewObject (Custom → GlassDualPassBackfaceCustom.usf). Save the package!"));
		return Mat;
	}
#endif // WITH_EDITOR

	FString GetConfiguredBackfaceMaterialPath()
	{
		static const IConsoleVariable* CVarPath =
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.GlassDualPass.BackfaceMaterial"));
		FString Path = CVarPath ? CVarPath->GetString().TrimStartAndEnd() : FString();
		if (Path.IsEmpty())
		{
			// Prefer content MI for artist control; fall back handled at load time.
			Path = BackfaceMaterialInstancePath;
		}
		return Path;
	}

	UMaterialInterface* LoadOrCreateBackfaceMaterial()
	{
		const FString Preferred = GetConfiguredBackfaceMaterialPath();
		if (UMaterialInterface* Existing = LoadObject<UMaterialInterface>(nullptr, *Preferred))
		{
			UE_LOG(LogGlassDualPass, Log, TEXT("Loaded backface material (render): %s"), *Existing->GetPathName());
			return Existing;
		}

		// Fallback: master material if MI path failed.
		if (!Preferred.Equals(BackfaceMaterialPath, ESearchCase::IgnoreCase))
		{
			if (UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, BackfaceMaterialPath))
			{
				UE_LOG(LogGlassDualPass, Warning,
					TEXT("Backface MI not found (%s) — using master: %s"),
					*Preferred, BackfaceMaterialPath);
				return Master;
			}
		}

		UE_LOG(LogGlassDualPass, Warning,
			TEXT("Backface material not found: %s (also tried master). Create MI/master or set r.GlassDualPass.BackfaceMaterial."),
			*Preferred);
		return nullptr;
	}

	UMaterialInterface* CreateBackfaceMaterialIfMissing()
	{
#if WITH_EDITOR
		if (UMaterialInterface* Existing = LoadObject<UMaterialInterface>(nullptr, BackfaceMaterialPath))
		{
			return Existing;
		}
		return CreateBackfaceMaterialNewObject();
#else
		return LoadOrCreateBackfaceMaterial();
#endif
	}

	UMaterialInstanceDynamic* CreateBackfaceMID(UObject* Outer)
	{
		UMaterialInterface* Parent = LoadOrCreateBackfaceMaterial();
		if (!Parent)
		{
			return nullptr;
		}
		// DMI parent = MIC (or master): inherits MI overrides; runtime inject SceneColor/view only.
		return UMaterialInstanceDynamic::Create(Parent, Outer ? Outer : GetTransientPackage());
	}

	static bool GetScalar(UMaterialInterface* Mat, const TCHAR* Name, float& Out)
	{
		return Mat->GetScalarParameterValue(FHashedMaterialParameterInfo(Name), Out);
	}

	static bool GetVector(UMaterialInterface* Mat, const TCHAR* Name, FLinearColor& Out)
	{
		return Mat->GetVectorParameterValue(FHashedMaterialParameterInfo(Name), Out);
	}

	static UTexture* GetTexture(UMaterialInterface* Mat, const TCHAR* Name)
	{
		UTexture* Tex = nullptr;
		Mat->GetTextureParameterValue(FHashedMaterialParameterInfo(Name), Tex);
		return Tex;
	}

	bool ReadShadingParamsFromMaterial(UMaterialInterface* Mat, FShadingParams& Out)
	{
		if (!IsValid(Mat))
		{
			Out.bFromMaterial = false;
			return false;
		}

		// Use the interface as given (MIC / MID / master) so material-instance overrides apply.
		UMaterialInterface* Source = Mat;

		GetScalar(Source, TEXT("IorStart"), Out.IorStart);
		GetScalar(Source, TEXT("UseTransmittance"), Out.UseTransmittance);
		GetScalar(Source, TEXT("EnvRefraction"), Out.EnvRefraction);
		GetScalar(Source, TEXT("FringeCurve"), Out.FringeCurve);
		GetScalar(Source, TEXT("FringeMix"), Out.FringeMix);
		GetScalar(Source, TEXT("RefractionIridescence"), Out.RefractionIridescence);
		GetScalar(Source, TEXT("DistScale"), Out.DistScale);
		GetScalar(Source, TEXT("SceneEdgeSoftness"), Out.SceneEdgeSoftness);
		GetVector(Source, TEXT("FringeColor"), Out.FringeColor);

		if (UTexture* T = GetTexture(Source, TEXT("DataATexture")))
		{
			Out.DataA = Cast<UTexture2D>(T);
		}
		if (UTexture* T = GetTexture(Source, TEXT("DataBTexture")))
		{
			Out.DataB = Cast<UTexture2D>(T);
		}
		if (UTexture* T = GetTexture(Source, TEXT("EnvMapTexture")))
		{
			Out.EnvMap = Cast<UTextureCube>(T);
			if (!Out.EnvMap)
			{
				// Cubemap sometimes typed as UTexture; try cube first, leave null if 2D.
			}
		}
		if (UTexture* T = GetTexture(Source, TEXT("ColorsMapTexture")))
		{
			Out.ColorsMap = Cast<UTexture2D>(T);
		}

		Out.bFromMaterial = true;
		return true;
	}

	void FillShadingParamsFromCVars(FShadingParams& Out)
	{
		static const IConsoleVariable* CVarIor = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GlassDualPass.IOR"));
		static const IConsoleVariable* CVarUseT = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GlassDualPass.UseTransmittance"));
		static const IConsoleVariable* CVarEnvR = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GlassDualPass.EnvRefraction"));
		static const IConsoleVariable* CVarFrC = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GlassDualPass.FringeCurve"));
		static const IConsoleVariable* CVarFrM = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GlassDualPass.FringeMix"));
		static const IConsoleVariable* CVarIrid = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GlassDualPass.RefractionIridescence"));
		static const IConsoleVariable* CVarDist = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GlassDualPass.DistScale"));

		Out.IorStart = CVarIor ? CVarIor->GetFloat() : 1.45f;
		Out.UseTransmittance = CVarUseT ? CVarUseT->GetFloat() : 1.f;
		Out.EnvRefraction = CVarEnvR ? CVarEnvR->GetFloat() : 0.35f;
		Out.FringeCurve = CVarFrC ? CVarFrC->GetFloat() : 2.f;
		Out.FringeMix = CVarFrM ? CVarFrM->GetFloat() : 0.25f;
		Out.RefractionIridescence = CVarIrid ? CVarIrid->GetFloat() : 0.85f;
		Out.DistScale = CVarDist ? CVarDist->GetFloat() : 1.f;
		Out.SceneEdgeSoftness = 0.02f;
		Out.FringeColor = FLinearColor(0.55f, 0.75f, 1.f, 1.f);
		Out.bFromMaterial = false;
	}

	void InjectRuntimeViewIntoMID(
		UMaterialInstanceDynamic* MID,
		UTexture* SceneColor,
		float SceneEdgeSoftness,
		const FVector2f& SceneViewRectMin,
		const FVector2f& SceneViewSize,
		const FVector2f& SceneBufferInvSize,
		const FMatrix44f& ViewProjection,
		const FVector3f& CameraWorldPos)
	{
		if (!MID)
		{
			return;
		}
		// Runtime-only: never touch IOR / fringe / bake textures (material editor owns those).
		// MUST run after any CopyParameterOverrides from MIC — that would wipe this bind.
		if (SceneColor)
		{
			MID->SetTextureParameterValue(TEXT("SceneColorTexture"), SceneColor);
			// Also try common alias if graph used a different Texture param name.
			MID->SetTextureParameterValue(TEXT("SceneColor"), SceneColor);
		}
		MID->SetScalarParameterValue(TEXT("SceneEdgeSoftness"), SceneEdgeSoftness);
		MID->SetVectorParameterValue(TEXT("SceneViewRectMin"),
			FLinearColor(SceneViewRectMin.X, SceneViewRectMin.Y, 0.f, 0.f));
		MID->SetVectorParameterValue(TEXT("SceneViewSize"),
			FLinearColor(SceneViewSize.X, SceneViewSize.Y, 0.f, 0.f));
		MID->SetVectorParameterValue(TEXT("SceneBufferInvSize"),
			FLinearColor(SceneBufferInvSize.X, SceneBufferInvSize.Y, 0.f, 0.f));
		const FMatrix44f& M = ViewProjection;
		MID->SetVectorParameterValue(TEXT("ViewProjection0"),
			FLinearColor(M.M[0][0], M.M[0][1], M.M[0][2], M.M[0][3]));
		MID->SetVectorParameterValue(TEXT("ViewProjection1"),
			FLinearColor(M.M[1][0], M.M[1][1], M.M[1][2], M.M[1][3]));
		MID->SetVectorParameterValue(TEXT("ViewProjection2"),
			FLinearColor(M.M[2][0], M.M[2][1], M.M[2][2], M.M[2][3]));
		MID->SetVectorParameterValue(TEXT("ViewProjection3"),
			FLinearColor(M.M[3][0], M.M[3][1], M.M[3][2], M.M[3][3]));
		MID->SetVectorParameterValue(TEXT("CameraWorldPos"),
			FLinearColor(CameraWorldPos.X, CameraWorldPos.Y, CameraWorldPos.Z, 1.f));
	}
}

// Deferred create — only when editor is fully up (safe NewObject, no MaterialEditingLibrary).
static FAutoConsoleCommand CCmdCreateBackfaceMaterial(
	TEXT("r.GlassDualPass.CreateBackfaceMaterial"),
	TEXT("Create /Game/Phonix/Material/M_PhoneixGlass_Back if missing (editor). Then save the asset."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
#if WITH_EDITOR
		if (UMaterialInterface* Mat = GlassDualPassMaterial::CreateBackfaceMaterialIfMissing())
		{
			UE_LOG(LogGlassDualPass, Log, TEXT("Backface material OK: %s"), *Mat->GetPathName());
		}
		else
		{
			UE_LOG(LogGlassDualPass, Error, TEXT("CreateBackfaceMaterial failed"));
		}
#else
		UE_LOG(LogGlassDualPass, Warning, TEXT("CreateBackfaceMaterial is editor-only"));
#endif
	}));

// Soft path to MIC / master used for mesh material override (DMI is created from this).
static TAutoConsoleVariable<FString> CVarGlassBackfaceMaterial(
	TEXT("r.GlassDualPass.BackfaceMaterial"),
	TEXT("/Game/Phonix/Material/M_PhoneixGlass_Back_Inst.M_PhoneixGlass_Back_Inst"),
	TEXT("Backface material or material instance path used for RT_GlassBack mesh pass.\n")
	TEXT("Default: M_PhoneixGlass_Back_Inst. Falls back to M_PhoneixGlass_Back if missing.\n")
	TEXT("After changing, run: r.GlassDualPass.ReloadBackfaceMaterial"),
	ECVF_Default);

static FAutoConsoleCommand CCmdReloadBackfaceMaterial(
	TEXT("r.GlassDualPass.ReloadBackfaceMaterial"),
	TEXT("Drop and re-load backface MI/master + recreate DMI (after authoring/saving)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (!GEngine)
		{
			return;
		}
		if (UGlassDualPassSubsystem* Sys = GEngine->GetEngineSubsystem<UGlassDualPassSubsystem>())
		{
			Sys->ResetBackfaceMaterialLoadAttempt();
			Sys->EnsureBackfaceMaterial();
			UE_LOG(LogGlassDualPass, Log,
				TEXT("ReloadBackfaceMaterial: Path=%s Mat=%s MID=%s"),
				*GlassDualPassMaterial::GetConfiguredBackfaceMaterialPath(),
				*GetNameSafe(Sys->GetBackfaceMaterial()),
				*GetNameSafe(Sys->GetBackfaceMaterialMID()));
		}
	}));
