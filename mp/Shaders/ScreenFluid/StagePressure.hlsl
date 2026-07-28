// Inputs: float PressL, PressR, PressD, PressU, PrevDivergence; float2 PrevVelocity; float PrevDensity
// Outputs: float OutPressure, OutDensity, OutDivergence; float2 OutVelocity
// Stage NumIterations should be 20~40
OutPressure = (PressL + PressR + PressD + PressU - PrevDivergence) * 0.25;
OutVelocity = PrevVelocity;
OutDensity = PrevDensity;
OutDivergence = PrevDivergence;
