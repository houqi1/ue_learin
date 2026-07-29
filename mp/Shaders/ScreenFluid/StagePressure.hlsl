// Inputs: float PressL, PressR, PressD, PressU, PrevDivergence; float2 PrevVelocity; float PrevDensity
// Outputs: float OutPressure, OutDensity, OutDivergence; float2 OutVelocity
// Stage NumIterations should be 20~40
// Stam: div = -0.5/N*((uR-uL)+(vU-vD)); p = (Σp + div)/4
OutPressure = (PressL + PressR + PressD + PressU + PrevDivergence) * 0.25;
OutVelocity = PrevVelocity;
OutDensity = PrevDensity;
OutDivergence = PrevDivergence;
