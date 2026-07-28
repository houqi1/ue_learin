// Inputs: float2 PrevVelocity; float PrevDensity, PrevPressure, PrevDivergence, PressL, PressR, PressD, PressU
// Outputs: float2 OutVelocity; float OutDensity, OutPressure, OutDivergence
float2 grad = 0.5 * float2(PressR - PressL, PressU - PressD);
OutVelocity = PrevVelocity - grad;
OutDensity = PrevDensity;
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
