# Wire M_PhoneixGlass_Back Custom to GlassDualPassBackfaceCustom.usf
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$McpUrl = 'http://127.0.0.1:8000/mcp'
$script:IdCounter = 1

$h0 = @{ 'Content-Type' = 'application/json'; Accept = 'application/json, text/event-stream' }
$ri = Invoke-WebRequest -Uri $McpUrl -Method POST -Body '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"w","version":"1"}}}' -Headers $h0 -TimeoutSec 30 -UseBasicParsing
$sid = $ri.Headers['Mcp-Session-Id']
$hBase = @{ 'Content-Type' = 'application/json'; Accept = 'application/json, text/event-stream'; 'Mcp-Session-Id' = $sid }
Invoke-WebRequest -Uri $McpUrl -Method POST -Body '{"jsonrpc":"2.0","method":"notifications/initialized"}' -Headers $hBase -TimeoutSec 30 -UseBasicParsing | Out-Null
Write-Host "SID=$sid"

function Call-Tool([string]$Toolset, [string]$Tool, [string]$ArgsJson) {
	$script:IdCounter++
	$body = '{"jsonrpc":"2.0","id":' + $script:IdCounter + ',"method":"tools/call","params":{"name":"call_tool","arguments":{"toolset_name":"' + $Toolset + '","tool_name":"' + $Tool + '","arguments":' + $ArgsJson + '}}}'
	$hh = @{ 'Content-Type' = 'application/json'; Accept = 'application/json, text/event-stream'; 'Mcp-Session-Id' = $sid }
	$r = Invoke-WebRequest -Uri $McpUrl -Method POST -Body ([Text.Encoding]::UTF8.GetBytes($body)) -Headers $hh -TimeoutSec 300 -UseBasicParsing
	$line = ($r.Content -split "`n" | Where-Object { $_.StartsWith('data:') } | Select-Object -Last 1)
	$json = $line.Substring(5).Trim()
	$j = $json | ConvertFrom-Json
	if ($j.result.isError) { throw $j.result.content[0].text }
	if ($j.error) { throw ($j.error | ConvertTo-Json -Compress) }
	return (@($j.result.content | Where-Object { $_.type -eq 'text' })[0].text)
}

$mat = '/Game/Phonix/Material/M_PhoneixGlass_Back.M_PhoneixGlass_Back'
$custom = $mat + ':MaterialExpressionCustom_0'
$matTs = 'editor_toolset.toolsets.material.MaterialTools'
$objTs = 'editor_toolset.toolsets.object.ObjectTools'
$assetTs = 'editor_toolset.toolsets.asset.AssetTools'

# 1) Set full Custom code (multi-line include)
$setArgs = '{"instance":{"refPath":"' + $custom + '"},"values":"{\"Code\":\"// Full dual-pass backface\\n#include \\\"/Project/GlassDualPassBackfaceCustom.usf\\\"\",\"Description\":\"GlassDualPassBackface FULL\",\"OutputType\":\"CMOT_Float3\"}"}'
Write-Host "SET_CODE=$(Call-Tool $objTs 'set_properties' $setArgs)"

# 2) List expressions
$ex = Call-Tool $matTs 'get_expressions' ('{"material_or_function":{"refPath":"' + $mat + '"}}')
$paths = [regex]::Matches($ex, '"refPath":"([^"]+)"') | ForEach-Object { $_.Groups[1].Value }
Write-Host "NUM=$($paths.Count)"

function Conn([string]$From, [string]$ToName) {
	$aj = '{"from_expression":{"refPath":"' + $From + '"},"from_output_name":"","to_expression":{"refPath":"' + $custom + '"},"to_input_name":"' + $ToName + '"}'
	try {
		Call-Tool $matTs 'connect_expressions' $aj | Out-Null
		Write-Host "OK $ToName"
	} catch {
		Write-Host "FAIL $ToName : $($_.Exception.Message)"
	}
}

foreach ($p in $paths) {
	if ($p -match 'WorldPosition') { Conn $p 'WorldPos' }
	elseif ($p -match 'VertexNormalWS') { Conn $p 'WorldNormal' }
	elseif ($p -match 'CameraPositionWS') { Conn $p 'CameraWorldPos' }
	elseif ($p -match 'TextureCoordinate') { Conn $p 'UV1' }
}

$scalarNames = @('IorStart', 'UseTransmittance', 'EnvRefraction', 'FringeCurve', 'FringeMix', 'RefractionIridescence', 'DistScale', 'SceneEdgeSoftness')
$vectorNames = @('FringeColor', 'SceneViewRectMin', 'SceneViewSize', 'SceneBufferInvSize', 'ViewProjection0', 'ViewProjection1', 'ViewProjection2', 'ViewProjection3')
$texNames = @('SceneColorTexture', 'DataATexture', 'DataBTexture', 'EnvMapTexture', 'ColorsMapTexture')

foreach ($p in $paths) {
	if ($p -match 'ScalarParameter_(\d+)') {
		$idx = [int]$Matches[1]
		if ($idx -ge $scalarNames.Count) { continue }
		$nm = $scalarNames[$idx]
		$sa = '{"instance":{"refPath":"' + $p + '"},"values":"{\"ParameterName\":\"' + $nm + '\"}"}'
		Call-Tool $objTs 'set_properties' $sa | Out-Null
		Conn $p $nm
	}
	elseif ($p -match 'VectorParameter_(\d+)') {
		$idx = [int]$Matches[1]
		if ($idx -ge $vectorNames.Count) { continue }
		$nm = $vectorNames[$idx]
		$sa = '{"instance":{"refPath":"' + $p + '"},"values":"{\"ParameterName\":\"' + $nm + '\"}"}'
		Call-Tool $objTs 'set_properties' $sa | Out-Null
		Conn $p $nm
	}
	elseif ($p -match 'TextureObjectParameter_(\d+)') {
		$idx = [int]$Matches[1]
		if ($idx -ge $texNames.Count) { continue }
		$nm = $texNames[$idx]
		$sa = '{"instance":{"refPath":"' + $p + '"},"values":"{\"ParameterName\":\"' + $nm + '\"}"}'
		Call-Tool $objTs 'set_properties' $sa | Out-Null
		Conn $p $nm
	}
}

$texCount = @($paths | Where-Object { $_ -match 'TextureObject' }).Count
Write-Host "TEX_COUNT=$texCount"
for ($t = $texCount; $t -lt 5; $t++) {
	$nm = $texNames[$t]
	$addArgs = '{"material_or_function":{"refPath":"' + $mat + '"},"expression_class":{"refPath":"/Script/Engine.MaterialExpressionTextureObjectParameter"},"x":-900,"y":' + (1600 + $t * 80) + '}'
	$add = Call-Tool $matTs 'add_expression' $addArgs
	$pref = ([regex]::Match($add, '"refPath":"([^"]+)"')).Groups[1].Value
	$sa = '{"instance":{"refPath":"' + $pref + '"},"values":"{\"ParameterName\":\"' + $nm + '\"}"}'
	Call-Tool $objTs 'set_properties' $sa | Out-Null
	Conn $pref $nm
	Write-Host "ADD $nm"
}

# Assign default textures by index
$texPaths = @(
	'/Engine/EngineResources/Black.Black',
	'/Game/Phonix/textures/T_Phoenix_DataA.T_Phoenix_DataA',
	'/Game/Phonix/textures/T_Phoenix_DataB.T_Phoenix_DataB',
	'/Game/Phonix/textures/wooden_studio_19_1k.wooden_studio_19_1k',
	'/Game/Phonix/textures/colorsMap.colorsMap'
)
$ex2 = Call-Tool $matTs 'get_expressions' ('{"material_or_function":{"refPath":"' + $mat + '"}}')
$paths2 = [regex]::Matches($ex2, '"refPath":"([^"]+)"') | ForEach-Object { $_.Groups[1].Value }
foreach ($p in $paths2) {
	if ($p -match 'TextureObjectParameter_(\d+)') {
		$idx = [int]$Matches[1]
		if ($idx -ge $texPaths.Count) { continue }
		$tp = $texPaths[$idx]
		$sa = '{"instance":{"refPath":"' + $p + '"},"values":"{\"Texture\":{\"refPath\":\"' + $tp + '\"}}"}'
		try {
			Call-Tool $objTs 'set_properties' $sa | Out-Null
			Write-Host "TEX $idx ok"
		} catch {
			Write-Host "TEX $idx fail"
		}
	}
}

Call-Tool $matTs 'connect_to_output' ('{"expression":{"refPath":"' + $custom + '"},"output_name":"","material_property":"MP_EmissiveColor"}') | Out-Null

try {
	$rc = Call-Tool $matTs 'recompile' ('{"material_or_function":{"refPath":"' + $mat + '"}}')
	Write-Host "RECOMPILE=$rc"
} catch {
	Write-Host "RECOMPILE_ERR=$($_.Exception.Message)"
}

Write-Host "SAVE=$(Call-Tool $assetTs 'save_assets' '{"asset_paths":["/Game/Phonix/Material/M_PhoneixGlass_Back"]}')"
Write-Host "OPEN=$(Call-Tool 'EditorToolset.EditorAppToolset' 'OpenEditorForAsset' '{"assetPath":"/Game/Phonix/Material/M_PhoneixGlass_Back"}')"
Write-Host 'DONE'
