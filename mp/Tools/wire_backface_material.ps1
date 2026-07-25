# Wire M_PhoneixGlass_Back Custom to full GlassDualPassBackfaceCustom.usf via Unreal MCP
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$McpUrl = 'http://127.0.0.1:8000/mcp'
$MatPath = '/Game/Phonix/Material/M_PhoneixGlass_Back.M_PhoneixGlass_Back'
$MatTs = 'editor_toolset.toolsets.material.MaterialTools'
$ObjTs = 'editor_toolset.toolsets.object.ObjectTools'
$AssetTs = 'editor_toolset.toolsets.asset.AssetTools'
$script:Id = 1

function New-Session {
	$h0 = @{ 'Content-Type' = 'application/json'; Accept = 'application/json, text/event-stream' }
	$ri = Invoke-WebRequest -Uri $McpUrl -Method POST -Body '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"wire","version":"1"}}}' -Headers $h0 -TimeoutSec 30 -UseBasicParsing
	$sid = $ri.Headers['Mcp-Session-Id']
	$h = @{ 'Content-Type' = 'application/json'; Accept = 'application/json, text/event-stream'; 'Mcp-Session-Id' = $sid }
	Invoke-WebRequest -Uri $McpUrl -Method POST -Body '{"jsonrpc":"2.0","method":"notifications/initialized"}' -Headers $h -TimeoutSec 30 -UseBasicParsing | Out-Null
	return $sid
}

function Call-Tool([string]$Sid, [string]$Toolset, [string]$Tool, $ToolArgs) {
	$script:Id++
	$h = @{ 'Content-Type' = 'application/json'; Accept = 'application/json, text/event-stream'; 'Mcp-Session-Id' = $Sid }
	$aj = ($ToolArgs | ConvertTo-Json -Depth 30 -Compress)
	$body = "{`"jsonrpc`":`"2.0`",`"id`":$($script:Id),`"method`":`"tools/call`",`"params`":{`"name`":`"call_tool`",`"arguments`":{`"toolset_name`":`"$Toolset`",`"tool_name`":`"$Tool`",`"arguments`":$aj}}}"
	$r = Invoke-WebRequest -Uri $McpUrl -Method POST -Body ([Text.Encoding]::UTF8.GetBytes($body)) -Headers $h -TimeoutSec 300 -UseBasicParsing
	$line = ($r.Content -split "`n" | Where-Object { $_.StartsWith('data:') } | Select-Object -Last 1)
	$json = $line.Substring(5).Trim()
	$j = $json | ConvertFrom-Json
	if ($j.error) { throw "RPC $($j.error | ConvertTo-Json -Compress)" }
	if ($j.result.isError) { throw "TOOL $($j.result.content[0].text)" }
	$t = @($j.result.content | Where-Object { $_.type -eq 'text' })[0]
	if ($t) { return $t.text }
	return $json
}

function Get-RefPath([string]$Text) {
	$o = $Text | ConvertFrom-Json
	if ($o.returnValue.refPath) { return $o.returnValue.refPath }
	if ($o.refPath) { return $o.refPath }
	throw "No refPath in: $Text"
}

function Add-Expr([string]$Sid, [string]$ClassPath, [int]$X, [int]$Y) {
	$t = Call-Tool $Sid $MatTs 'add_expression' @{
		material_or_function = @{ refPath = $MatPath }
		expression_class     = @{ refPath = $ClassPath }
		x                    = $X
		y                    = $Y
	}
	return (Get-RefPath $t)
}

function Set-Props([string]$Sid, [string]$Ref, [string]$ValuesJson) {
	return Call-Tool $Sid $ObjTs 'set_properties' @{
		instance = @{ refPath = $Ref }
		values   = $ValuesJson
	}
}

function Connect-ToCustom([string]$Sid, [string]$FromRef, [string]$ToCustom, [string]$InputName) {
	return Call-Tool $Sid $MatTs 'connect_expressions' @{
		from_expression  = @{ refPath = $FromRef }
		from_output_name = ''
		to_expression    = @{ refPath = $ToCustom }
		to_input_name    = $InputName
	}
}

$sid = New-Session
Write-Host "SID=$sid"

# Ensure Unlit / TwoSided
Set-Props $sid $MatPath '{"TwoSided":true,"blendMode":"BLEND_Opaque"}' | Out-Null

# Get existing Custom
$exprsText = Call-Tool $sid $MatTs 'get_expressions' @{ material_or_function = @{ refPath = $MatPath } }
$exprsObj = $exprsText | ConvertFrom-Json
$customRef = $null
foreach ($e in $exprsObj.returnValue) {
	if ($e.refPath -match 'MaterialExpressionCustom') { $customRef = $e.refPath; break }
}
if (-not $customRef) {
	$customRef = Add-Expr $sid '/Script/Engine.MaterialExpressionCustom' 200 0
}
Write-Host "CUSTOM=$customRef"

# --- Geometry ---
$worldPos = Add-Expr $sid '/Script/Engine.MaterialExpressionWorldPosition' -1400 0
$normal = Add-Expr $sid '/Script/Engine.MaterialExpressionVertexNormalWS' -1400 140
$cam = Add-Expr $sid '/Script/Engine.MaterialExpressionCameraPositionWS' -1400 280
$uv1 = Add-Expr $sid '/Script/Engine.MaterialExpressionTextureCoordinate' -1400 420
Set-Props $sid $uv1 '{"CoordinateIndex":1}' | Out-Null

# --- Scalars ---
function Add-Scalar([string]$Sid, [string]$Name, [float]$Def, [int]$Y) {
	$r = Add-Expr $Sid '/Script/Engine.MaterialExpressionScalarParameter' -1100 $Y
	$vj = (@{ ParameterName = $Name; DefaultValue = $Def } | ConvertTo-Json -Compress)
	Set-Props $Sid $r $vj | Out-Null
	return $r
}
function Add-Vector([string]$Sid, [string]$Name, [float[]]$RGBA, [int]$Y) {
	$r = Add-Expr $Sid '/Script/Engine.MaterialExpressionVectorParameter' -1100 $Y
	# DefaultValue as linear color object if needed - try simple
	$vj = (@{
			ParameterName = $Name
			DefaultValue  = @{ R = $RGBA[0]; G = $RGBA[1]; B = $RGBA[2]; A = $RGBA[3] }
		} | ConvertTo-Json -Compress -Depth 5)
	Set-Props $Sid $r $vj | Out-Null
	return $r
}
function Add-TexParam([string]$Sid, [string]$Name, [int]$Y) {
	$r = Add-Expr $Sid '/Script/Engine.MaterialExpressionTextureObjectParameter' -1100 $Y
	$vj = (@{ ParameterName = $Name } | ConvertTo-Json -Compress)
	Set-Props $Sid $r $vj | Out-Null
	return $r
}

$sIor = Add-Scalar $sid 'IorStart' 1.45 0
$sUseT = Add-Scalar $sid 'UseTransmittance' 1.0 70
$sEnvR = Add-Scalar $sid 'EnvRefraction' 0.35 140
$sFrC = Add-Scalar $sid 'FringeCurve' 2.0 210
$sFrM = Add-Scalar $sid 'FringeMix' 0.25 280
$sIrid = Add-Scalar $sid 'RefractionIridescence' 0.85 350
$sDist = Add-Scalar $sid 'DistScale' 1.0 420
$sEdge = Add-Scalar $sid 'SceneEdgeSoftness' 0.02 490

$vFringe = Add-Vector $sid 'FringeColor' @(0.55, 0.75, 1.0, 1.0) 560
$vRectMin = Add-Vector $sid 'SceneViewRectMin' @(0, 0, 0, 0) 630
$vViewSize = Add-Vector $sid 'SceneViewSize' @(1920, 1080, 0, 0) 700
$vBufInv = Add-Vector $sid 'SceneBufferInvSize' @(0.000520833, 0.000925926, 0, 0) 770
$vVP0 = Add-Vector $sid 'ViewProjection0' @(1, 0, 0, 0) 840
$vVP1 = Add-Vector $sid 'ViewProjection1' @(0, 1, 0, 0) 910
$vVP2 = Add-Vector $sid 'ViewProjection2' @(0, 0, 1, 0) 980
$vVP3 = Add-Vector $sid 'ViewProjection3' @(0, 0, 0, 1) 1050

$texScene = Add-TexParam $sid 'SceneColorTexture' 1120
$texDataA = Add-TexParam $sid 'DataATexture' 1190
$texDataB = Add-TexParam $sid 'DataBTexture' 1260
$texEnv = Add-TexParam $sid 'EnvMapTexture' 1330
$texColors = Add-TexParam $sid 'ColorsMapTexture' 1400

# Assign default textures where possible
function Try-SetTexture([string]$Sid, [string]$ExprRef, [string]$TexPath) {
	try {
		$vj = (@{ Texture = @{ refPath = $TexPath } } | ConvertTo-Json -Compress -Depth 5)
		Set-Props $Sid $ExprRef $vj | Out-Null
		Write-Host "  tex $TexPath -> ok"
	} catch {
		Write-Host "  tex $TexPath -> $($_.Exception.Message)"
	}
}
Try-SetTexture $sid $texDataA '/Game/Phonix/textures/T_Phoenix_DataA.T_Phoenix_DataA'
Try-SetTexture $sid $texDataB '/Game/Phonix/textures/T_Phoenix_DataB.T_Phoenix_DataB'
Try-SetTexture $sid $texColors '/Game/Phonix/textures/colorsMap.colorsMap'
Try-SetTexture $sid $texEnv '/Game/Phonix/textures/wooden_studio_19_1k.wooden_studio_19_1k'
Try-SetTexture $sid $texScene '/Engine/EngineResources/Black.Black'

# Custom code: multi-line include (NOT single-line — UE would wrap as return #include)
$code = @"
// Full dual-pass backface shading (shared with Global PS via Lib)
#include `"/Project/GlassDualPassBackfaceCustom.usf`"
"@
$codeValues = [ordered]@{
	Code        = $code
	Description = 'GlassDualPassBackface full'
	OutputType  = 'CMOT_Float3'
}
# Also declare Inputs names so pins exist
# UE Custom Inputs: TArray of {InputName}
$inputNames = @(
	'WorldPos', 'WorldNormal', 'UV1', 'CameraWorldPos',
	'ViewProjection0', 'ViewProjection1', 'ViewProjection2', 'ViewProjection3',
	'SceneViewRectMin', 'SceneViewSize', 'SceneBufferInvSize', 'SceneEdgeSoftness',
	'IorStart', 'UseTransmittance', 'EnvRefraction', 'FringeCurve', 'FringeMix',
	'FringeColor', 'RefractionIridescence', 'DistScale',
	'DataATexture', 'DataBTexture', 'EnvMapTexture', 'ColorsMapTexture', 'SceneColorTexture'
)
$inputsArr = @()
foreach ($n in $inputNames) { $inputsArr += @{ InputName = $n } }
$codeValues['Inputs'] = $inputsArr

$valuesJson = $codeValues | ConvertTo-Json -Depth 10 -Compress
Write-Host "Setting Custom Code+Inputs..."
Set-Props $sid $customRef $valuesJson | Out-Null

# Connect pins
Write-Host "Connecting..."
Connect-ToCustom $sid $worldPos $customRef 'WorldPos' | Out-Null
Connect-ToCustom $sid $normal $customRef 'WorldNormal' | Out-Null
Connect-ToCustom $sid $uv1 $customRef 'UV1' | Out-Null
Connect-ToCustom $sid $cam $customRef 'CameraWorldPos' | Out-Null
Connect-ToCustom $sid $vVP0 $customRef 'ViewProjection0' | Out-Null
Connect-ToCustom $sid $vVP1 $customRef 'ViewProjection1' | Out-Null
Connect-ToCustom $sid $vVP2 $customRef 'ViewProjection2' | Out-Null
Connect-ToCustom $sid $vVP3 $customRef 'ViewProjection3' | Out-Null
Connect-ToCustom $sid $vRectMin $customRef 'SceneViewRectMin' | Out-Null
Connect-ToCustom $sid $vViewSize $customRef 'SceneViewSize' | Out-Null
Connect-ToCustom $sid $vBufInv $customRef 'SceneBufferInvSize' | Out-Null
Connect-ToCustom $sid $sEdge $customRef 'SceneEdgeSoftness' | Out-Null
Connect-ToCustom $sid $sIor $customRef 'IorStart' | Out-Null
Connect-ToCustom $sid $sUseT $customRef 'UseTransmittance' | Out-Null
Connect-ToCustom $sid $sEnvR $customRef 'EnvRefraction' | Out-Null
Connect-ToCustom $sid $sFrC $customRef 'FringeCurve' | Out-Null
Connect-ToCustom $sid $sFrM $customRef 'FringeMix' | Out-Null
Connect-ToCustom $sid $vFringe $customRef 'FringeColor' | Out-Null
Connect-ToCustom $sid $sIrid $customRef 'RefractionIridescence' | Out-Null
Connect-ToCustom $sid $sDist $customRef 'DistScale' | Out-Null
Connect-ToCustom $sid $texDataA $customRef 'DataATexture' | Out-Null
Connect-ToCustom $sid $texDataB $customRef 'DataBTexture' | Out-Null
Connect-ToCustom $sid $texEnv $customRef 'EnvMapTexture' | Out-Null
Connect-ToCustom $sid $texColors $customRef 'ColorsMapTexture' | Out-Null
Connect-ToCustom $sid $texScene $customRef 'SceneColorTexture' | Out-Null

# Emissive
Call-Tool $sid $MatTs 'connect_to_output' @{
	expression        = @{ refPath = $customRef }
	output_name       = ''
	material_property = 'MP_EmissiveColor'
} | Out-Null

Call-Tool $sid $MatTs 'layout_expressions' @{ material_or_function = @{ refPath = $MatPath } } | Out-Null

Write-Host "Recompiling..."
try {
	$rc = Call-Tool $sid $MatTs 'recompile' @{ material_or_function = @{ refPath = $MatPath } }
	Write-Host "RECOMPILE=$rc"
} catch {
	Write-Host "RECOMPILE_ERR=$($_.Exception.Message)"
}

Call-Tool $sid $AssetTs 'save_assets' @{ asset_paths = @('/Game/Phonix/Material/M_PhoneixGlass_Back') } | Out-Null
Call-Tool $sid 'EditorToolset.EditorAppToolset' 'OpenEditorForAsset' @{ assetPath = '/Game/Phonix/Material/M_PhoneixGlass_Back' } | Out-Null
Write-Host 'DONE'
