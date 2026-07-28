// Inputs: float2 PrevVelocity; float PrevDensity
// Output: float4 OutColor  -> write to RT (RG=vel, A=density)
OutColor = float4(PrevVelocity.x, PrevVelocity.y, 0.0, PrevDensity);
