// Pins (Custom HLSL): Input float2 UnitToUV, ClickUV, PrevVelocity; float Strength, Radius, InjectPulse, PrevDensity, PrevPressure, PrevDivergence
// Output: float2 OutVelocity; float OutDensity, OutPressure, OutDivergence
float2 d = UnitToUV - ClickUV;
float r = length(d);
float rad = max(Radius, 1e-4);
float pulse = max(InjectPulse, 0.0);
float w = Strength * pulse * saturate(1.0 - r / rad);
w = w * w;
float2 dir = (r > 1e-5) ? (d / r) : float2(0.0, 0.0);
float2 tang = float2(-dir.y, dir.x);
float2 force = dir * w + tang * (w * 0.45) + float2(w, -w) * 0.05;
float2 v = PrevVelocity + force;
float sp = length(v);
if (sp > 2.0) { v *= 2.0 / sp; }
OutVelocity = v;
OutDensity = saturate(PrevDensity * 0.995 + w);
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
