// Copyright Epic Games, Inc. All Rights Reserved.

#include "GlassDualPassBackfaceShaders.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "PipelineStateCache.h"

IMPLEMENT_GLOBAL_SHADER(FGlassBackfaceVS, "/Project/GlassDualPassBackface.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FGlassBackfaceSkinCacheVS, "/Project/GlassDualPassBackface.usf", "MainVS_SkinCache", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FGlassBackfacePS, "/Project/GlassDualPassBackface.usf", "MainPS", SF_Pixel);

FRHIVertexDeclaration* GetGlassBackfaceVertexDeclarationRHI()
{
	FVertexDeclarationElementList Elements;
	Elements.Add(FVertexElement(0, 0, VET_Float3, 0, sizeof(FVector3f)));
	return PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
}
