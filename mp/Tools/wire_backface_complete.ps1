# Rebuild M_PhoneixGlass_Back: grow Custom Inputs via full JSON round-trip, then wire all pins
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
	$ri = Invoke-WebRequest -Uri $McpUrl -Method POST -Body '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"wire-complete","version":"3"}}}' -Headers $h0 -TimeoutSec 30 -UseBasicParsing
	$sid = $ri.Headers['Mcp-Session-Id']
	$h = @{ 'Content-Type' = 'application/json'; Accept = 'application/json, text/event-stream'; 'Mcp-Session-Id' = $sid }
	Invoke-WebRequest -Uri $McpUrl -Method POST -Body '{"jsonrpc":"2.0","method":"notifications/initialized"}' -Headers $h -TimeoutSec 30 -UseBasicParsing | Out-Null
	return $sid
}

function Call-Tool([string]$Sid, [string]$Toolset, [string]$Tool, $ToolArgs) {
	$script:Id++
	$h = @{ 'Content-Type' = 'application/json'; Accept = 'application/json, text/event-stream'; 'Mcp-Session-Id' = $Sid }
	$aj = ($ToolArgs | ConvertTo-Json -Depth 40 -Compress)
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

function Get-PropsJsonInner([string]$Text) {
	$outer = $Text | ConvertFrom-Json
	return [string]$outer.returnValue
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

function Empty-InputJson([string]$Name) {
	return "{`"inputName`":`"$Name`",`"input`":{`"expression`":`"None`",`"outputIndex`":0,`"inputName`":`"None`",`"mask`":0,`"maskR`":0,`"maskG`":0,`"maskB`":0,`"maskA`":0}}"
}

function Get-InputsJsonArray([string]$Sid, [string]$CustomRef) {
	# Returns raw JSON array string of inputs, e.g. [{...},{...}]
	$cur = Call-Tool $Sid $ObjTs 'get_properties' @{
		instance   = @{ refPath = $CustomRef }
		properties = @('inputs')
	}
	$inner = Get-PropsJsonInner $cur
	# inner is {"inputs":[...]}
	if ($inner -match '"inputs"\s*:\s*(\[.*\])\s*}\s*$') {
		return $Matches[1]
	}
	# fallback parse
	$obj = $inner | ConvertFrom-Json
	if (-not $obj.inputs) { return '[]' }
	$parts = New-Object System.Collections.Generic.List[string]
	foreach ($it in @($obj.inputs)) {
		$n = [string]$it.inputName
		$parts.Add((Empty-InputJson $n))
	}
	return ('[' + ($parts -join ',') + ']')
}

function Get-InputNamesFromJson([string]$ArrJson) {
	$names = [regex]::Matches($ArrJson, '"inputName"\s*:\s*"([^"]*)"') | ForEach-Object {
		# first inputName in each object is the pin name; nested input also has inputName
		$_.Groups[1].Value
	}
	# Take every other? Actually structure is inputName then input.inputName="None"
	# Pattern matches both. Prefer: parse properly
	$obj = ("{`"inputs`":$ArrJson}") | ConvertFrom-Json
	return @(@($obj.inputs) | ForEach-Object { [string]$_.inputName })
}

function Set-InputsJson([string]$Sid, [string]$CustomRef, [string]$ArrJson) {
	$values = "{`"inputs`":$ArrJson}"
	return Set-Props $Sid $CustomRef $values
}

function Append-InputJson([string]$ArrJson, [string]$Name) {
	$item = Empty-InputJson $Name
	if ($ArrJson -eq '[]' -or $ArrJson.Trim() -eq '[]') {
		return "[$item]"
	}
	# insert before final ]
	return ($ArrJson.TrimEnd() -replace '\]\s*$', ",$item]")
}

function Try-Call([scriptblock]$Block, [string]$Label) {
	try {
		$r = & $Block
		Write-Host "OK  $Label"
		return $r
	} catch {
		Write-Host "FAIL $Label : $($_.Exception.Message)"
		return $null
	}
}

$sid = New-Session
Write-Host "SID=$sid"

Set-Props $sid $MatPath '{"TwoSided":true,"blendMode":"BLEND_Opaque","shadingModel":"MSM_Unlit"}' | Out-Null

# Find or create Custom; wipe other expressions
$exprsText = Call-Tool $sid $MatTs 'get_expressions' @{ material_or_function = @{ refPath = $MatPath } }
$exprsObj = $exprsText | ConvertFrom-Json
$customRef = $null
$toDelete = @()
foreach ($e in @($exprsObj.returnValue)) {
	if ($e.refPath -match 'MaterialExpressionCustom') {
		if (-not $customRef) { $customRef = $e.refPath }
		else { $toDelete += $e.refPath }
	} else {
		$toDelete += $e.refPath
	}
}
foreach ($p in $toDelete) {
	try {
		Call-Tool $sid $MatTs 'delete_expression' @{
			material_or_function = @{ refPath = $MatPath }
			expression           = @{ refPath = $p }
		} | Out-Null
	} catch { Write-Host "del fail $p" }
}
if (-not $customRef) {
	$customRef = Add-Expr $sid '/Script/Engine.MaterialExpressionCustom' 300 0
}
Write-Host "CUSTOM=$customRef"

$inputNames = @(
	'WorldPos', 'WorldNormal', 'UV1', 'CameraWorldPos',
	'ViewProjection0', 'ViewProjection1', 'ViewProjection2', 'ViewProjection3',
	'SceneViewRectMin', 'SceneViewSize', 'SceneBufferInvSize', 'SceneEdgeSoftness',
	'IorStart', 'UseTransmittance', 'EnvRefraction', 'FringeCurve', 'FringeMix',
	'FringeColor', 'RefractionIridescence', 'DistScale',
	'DataATexture', 'DataBTexture', 'EnvMapTexture', 'ColorsMapTexture', 'SceneColorTexture'
)

# Ensure we have at least one pin named WorldPos
$arrJson = Get-InputsJsonArray $sid $customRef
Write-Host "INITIAL_INPUTS=$arrJson"
$names = @(Get-InputNamesFromJson $arrJson)
if ($names.Count -eq 0) {
	$arrJson = "[$(Empty-InputJson 'WorldPos')]"
	Set-InputsJson $sid $customRef $arrJson | Out-Null
	Write-Host "SEED WorldPos"
} elseif ($names[0] -ne 'WorldPos') {
	# same-size rename: rebuild array JSON with first name fixed
	$arrJson = Get-InputsJsonArray $sid $customRef
	# replace first inputName occurrence
	$arrJson = [regex]::Replace($arrJson, '"inputName"\s*:\s*"[^"]*"', '"inputName":"WorldPos"', 1)
	Set-InputsJson $sid $customRef $arrJson | Out-Null
	Write-Host "RENAME pin0 -> WorldPos"
}

# Grow one pin at a time with full round-trip JSON
for ($i = 0; $i -lt $inputNames.Count; $i++) {
	$name = $inputNames[$i]
	$arrJson = Get-InputsJsonArray $sid $customRef
	$names = @(Get-InputNamesFromJson $arrJson)
	if ($i -lt $names.Count -and $names[$i] -eq $name) {
		Write-Host "PIN[$i] ok $name"
		continue
	}
	if ($i -lt $names.Count) {
		# Rebuild full array with corrected names up to current count
		$parts = New-Object System.Collections.Generic.List[string]
		for ($j = 0; $j -lt $names.Count; $j++) {
			$n = if ($j -eq $i) { $name } else { $names[$j] }
			$parts.Add((Empty-InputJson $n))
		}
		$arrJson = '[' + ($parts -join ',') + ']'
		Set-InputsJson $sid $customRef $arrJson | Out-Null
		Write-Host "PIN[$i] rename $name"
		continue
	}
	# Grow: append new slot
	$arrJson = Get-InputsJsonArray $sid $customRef
	$arrJson = Append-InputJson $arrJson $name
	$ok = Set-InputsJson $sid $customRef $arrJson
	Write-Host "PIN[$i] add $name => $ok"
}

$pinNames = Call-Tool $sid $MatTs 'get_expression_input_names' @{ expression = @{ refPath = $customRef } }
Write-Host "PINS=$pinNames"

$code = "// Full dual-pass backface`n#include `"/Project/GlassDualPassBackfaceCustom.usf`""
$codeJson = "{`"code`":$((ConvertTo-Json $code -Compress)),`"description`":`"GlassDualPassBackface FULL`",`"outputType`":`"CMOT_Float3`"}"
Write-Host "SET_CODE=$(Set-Props $sid $customRef $codeJson)"

# Geometry
$worldPos = Add-Expr $sid '/Script/Engine.MaterialExpressionWorldPosition' -1400 0
$normal = Add-Expr $sid '/Script/Engine.MaterialExpressionVertexNormalWS' -1400 120
$cam = Add-Expr $sid '/Script/Engine.MaterialExpressionCameraPositionWS' -1400 240
$uv1 = Add-Expr $sid '/Script/Engine.MaterialExpressionTextureCoordinate' -1400 360
Set-Props $sid $uv1 '{"coordinateIndex":1}' | Out-Null

function Add-Scalar([string]$Sid, [string]$Name, [double]$Def, [int]$Y) {
	$r = Add-Expr $Sid '/Script/Engine.MaterialExpressionScalarParameter' -1100 $Y
	$vj = "{`"parameterName`":`"$Name`",`"defaultValue`":$Def}"
	Set-Props $Sid $r $vj | Out-Null
	return $r
}
function Add-Vector([string]$Sid, [string]$Name, [double[]]$RGBA, [int]$Y) {
	$r = Add-Expr $Sid '/Script/Engine.MaterialExpressionVectorParameter' -1100 $Y
	$vj = "{`"parameterName`":`"$Name`",`"defaultValue`":{`"R`":$($RGBA[0]),`"G`":$($RGBA[1]),`"B`":$($RGBA[2]),`"A`":$($RGBA[3])}}"
	Set-Props $Sid $r $vj | Out-Null
	return $r
}
function Add-TexParam([string]$Sid, [string]$Name, [int]$Y) {
	$r = Add-Expr $Sid '/Script/Engine.MaterialExpressionTextureObjectParameter' -1100 $Y
	$vj = "{`"parameterName`":`"$Name`"}"
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

function Try-SetTexture([string]$Sid, [string]$ExprRef, [string]$TexPath) {
	foreach ($key in @('texture', 'Texture')) {
		try {
			$vj = "{`"$key`":{`"refPath`":`"$TexPath`"}}"
			Set-Props $Sid $ExprRef $vj | Out-Null
			Write-Host "  tex ok $TexPath"
			return
		} catch {}
	}
	Write-Host "  tex fail $TexPath"
}
Try-SetTexture $sid $texDataA '/Game/Phonix/textures/T_Phoenix_DataA.T_Phoenix_DataA'
Try-SetTexture $sid $texDataB '/Game/Phonix/textures/T_Phoenix_DataB.T_Phoenix_DataB'
Try-SetTexture $sid $texColors '/Game/Phonix/textures/colorsMap.colorsMap'
Try-SetTexture $sid $texEnv '/Game/Phonix/textures/wooden_studio_19_1k.wooden_studio_19_1k'
Try-SetTexture $sid $texScene '/Engine/EngineResources/Black.Black'

Write-Host "Connecting..."
$map = [ordered]@{
	WorldPos               = $worldPos
	WorldNormal            = $normal
	UV1                    = $uv1
	CameraWorldPos         = $cam
	ViewProjection0        = $vVP0
	ViewProjection1        = $vVP1
	ViewProjection2        = $vVP2
	ViewProjection3        = $vVP3
	SceneViewRectMin       = $vRectMin
	SceneViewSize          = $vViewSize
	SceneBufferInvSize     = $vBufInv
	SceneEdgeSoftness      = $sEdge
	IorStart               = $sIor
	UseTransmittance       = $sUseT
	EnvRefraction          = $sEnvR
	FringeCurve            = $sFrC
	FringeMix              = $sFrM
	FringeColor            = $vFringe
	RefractionIridescence  = $sIrid
	DistScale              = $sDist
	DataATexture           = $texDataA
	DataBTexture           = $texDataB
	EnvMapTexture          = $texEnv
	ColorsMapTexture       = $texColors
	SceneColorTexture      = $texScene
}

foreach ($k in $map.Keys) {
	Try-Call {
		Call-Tool $sid $MatTs 'connect_expressions' @{
			from_expression  = @{ refPath = $map[$k] }
			from_output_name = ''
			to_expression    = @{ refPath = $customRef }
			to_input_name    = $k
		}
	} "conn $k" | Out-Null
}

Try-Call {
	Call-Tool $sid $MatTs 'connect_to_output' @{
		expression        = @{ refPath = $customRef }
		output_name       = ''
		material_property = 'MP_EmissiveColor'
	}
} 'emissive' | Out-Null

$wiring = Call-Tool $sid $MatTs 'get_expression_inputs' @{
	material_or_function = @{ refPath = $MatPath }
	expression           = @{ refPath = $customRef }
}
Write-Host "WIRING=$wiring"

try {
	Call-Tool $sid $MatTs 'layout_expressions' @{ material_or_function = @{ refPath = $MatPath } } | Out-Null
} catch { Write-Host "layout skip" }

Write-Host "Recompiling..."
try {
	$rc = Call-Tool $sid $MatTs 'recompile' @{ material_or_function = @{ refPath = $MatPath } }
	Write-Host "RECOMPILE=$rc"
} catch {
	Write-Host "RECOMPILE_ERR=$($_.Exception.Message)"
}

Write-Host "SAVE=$(Call-Tool $sid $AssetTs 'save_assets' @{ asset_paths = @('/Game/Phonix/Material/M_PhoneixGlass_Back') })"
try {
	Call-Tool $sid 'EditorToolset.EditorAppToolset' 'OpenEditorForAsset' @{ assetPath = '/Game/Phonix/Material/M_PhoneixGlass_Back' } | Out-Null
	Write-Host 'OPEN=ok'
} catch {
	Write-Host "OPEN fail: $($_.Exception.Message)"
}
Write-Host 'DONE'
