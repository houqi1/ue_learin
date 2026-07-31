// GameMode that spawns / defaults to the top-down Phoenix pawn.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PhoenixGameMode.generated.h"

UCLASS(Blueprintable, meta = (DisplayName = "Phoenix Game Mode"))
class APhoenixGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APhoenixGameMode();
};
