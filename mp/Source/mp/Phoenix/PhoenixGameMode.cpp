#include "PhoenixGameMode.h"
#include "PhoenixPawn.h"

APhoenixGameMode::APhoenixGameMode()
{
	DefaultPawnClass = APhoenixPawn::StaticClass();
	// Keep engine default PlayerController / HUD.
}
