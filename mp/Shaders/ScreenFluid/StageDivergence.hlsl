// Inputs: float2 PrevVelocity, VelL, VelR, VelD, VelU; float PrevDensity, PrevPressure
// Outputs: float2 OutVelocity; float OutDensity, OutPressure, OutDivergence
OutDivergence = 0.5 * ((VelR.x - VelL.x) + (VelU.y - VelD.y));
OutVelocity = PrevVelocity;
OutDensity = PrevDensity;
OutPressure = PrevPressure;
