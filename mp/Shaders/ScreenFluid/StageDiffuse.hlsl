// Inputs: PrevVelocity, VelL/R/D/U (float2); PrevDensity, DensL/R/D/U, Viscosity, PrevPressure, PrevDivergence
// Outputs: OutVelocity, OutDensity, OutPressure, OutDivergence
float visc = saturate(Viscosity);
OutVelocity = lerp(PrevVelocity, (PrevVelocity + VelL + VelR + VelD + VelU) * 0.2, visc);
OutDensity = lerp(PrevDensity, (PrevDensity + DensL + DensR + DensD + DensU) * 0.2, visc * 0.5);
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
