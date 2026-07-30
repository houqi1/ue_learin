// Default Custom HLSL body = ORIGINAL call only (logic unchanged).
// When enabling Phoenix, switch the call to SF_InjectWithGBuffer and add Mode1 pins.

#include "/Project/ScreenFluid/StageCommon.ush"
#include "/Project/ScreenFluid/StageInject.ush"

// --- original (keep using this until Mode1 pins are wired) ---
SF_Inject(
	UnitToUV,
	ClickUV,
	InjectDir,
	Strength,
	Radius,
	InjectPulse,
	Dt,
	PrevVelocity,
	PrevDensity,
	PrevPressure,
	PrevDivergence,
	OutVelocity,
	OutDensity,
	OutPressure,
	OutDivergence);

// --- Phoenix (uncomment & comment out SF_Inject above when ready) ---
// SF_InjectWithGBuffer(
//	UnitToUV, ClickUV, InjectDir, Strength, Radius, InjectPulse, Dt,
//	PrevVelocity, PrevDensity, PrevPressure, PrevDivergence,
//	InjectMode, SceneVel, Stencil, PhoenixStencil,
//	InjectStrength, VelocityToFluid, MinSpeedUV, VelocityYFlip,
//	OutVelocity, OutDensity, OutPressure, OutDivergence);
