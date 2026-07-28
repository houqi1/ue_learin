// Inputs: float2 SampleVel; float SampleDens, Damping, PrevPressure, PrevDivergence
// Outputs: float2 OutVelocity; float OutDensity, OutPressure, OutDivergence
// SampleVel/Dens: sample previous grid at (UnitToUV - PrevVelocity * AdvectScale)
OutVelocity = SampleVel * Damping;
OutDensity = SampleDens * Damping;
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
