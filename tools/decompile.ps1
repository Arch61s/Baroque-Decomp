param([string[]]$Addresses=@(),[switch]$All,[switch]$List)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$localToolRoot=$env:BAROQUE_DECOMPILER_ROOT   # folder with your Ghidra install (see the script below)
$ghidraRoot=Join-Path $localToolRoot 'ghidra_12.1.3_PUBLIC'
$taskClassPath=(Get-ChildItem -LiteralPath (Join-Path $ghidraRoot 'Ghidra') -Recurse -Filter '*.jar').FullName -join ';'
$classes=Join-Path $taskRoot 'build/ghidra_classes'
New-Item -ItemType Directory -Force -Path $classes | Out-Null
$compilerArgs=@('-proc:none','-nowarn','-Xlint:-removal','-cp',('"'+$taskClassPath.Replace('\','/')+'"'),'-d',('"'+$classes.Replace('\','/')+'"'),('"'+(Join-Path $PSScriptRoot 'BaroqueDecompile.java').Replace('\','/')+'"'))
$argFile=Join-Path $taskRoot 'build/javac_args.txt'
[IO.File]::WriteAllLines($argFile,$compilerArgs,(New-Object Text.UTF8Encoding($false)))
$ErrorActionPreference='Continue'
& (Join-Path $localToolRoot 'jdk-21.0.12.1+1/bin/javac.exe') ('@'+$argFile) 2>&1 | Out-String | Write-Host
$ErrorActionPreference='Stop'
if ($LASTEXITCODE -ne 0) { throw 'Exporter compilation failed' }
$javaArgs=@('-Xmx6G','-Djava.awt.headless=true',('-Dapplication.settingsdir='+ (Join-Path $taskRoot 'work/ghidra_settings')),('-Dapplication.cachedir='+ (Join-Path $taskRoot 'work/ghidra_cache')),'-cp',($classes+';'+$taskClassPath),'BaroqueDecompile',$taskRoot,$ghidraRoot)
$javaArgs+=@($Addresses | ForEach-Object { $_ -split '[,; ]+' } | Where-Object { $_ -ne '' })
if($All) { $javaArgs+='--all' }
if($List) { $javaArgs+='--list' }
$ErrorActionPreference='Continue'
& (Join-Path $localToolRoot 'jdk-21.0.12.1+1/bin/java.exe') @javaArgs 2>&1 | ForEach-Object { "$_" }
$code=$LASTEXITCODE
$ErrorActionPreference='Stop'
if ($code -ne 0) { throw 'Static analysis failed' }
