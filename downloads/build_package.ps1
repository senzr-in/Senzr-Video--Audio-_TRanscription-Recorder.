$ErrorActionPreference = "Stop"

$stage = "f:\PI5MAX\downloads\stage_v1_1_0"
if (Test-Path $stage) { 
    Remove-Item -Recurse -Force $stage 
}

New-Item -ItemType Directory -Path "$stage\bin" -Force | Out-Null
New-Item -ItemType Directory -Path "$stage\frontend" -Force | Out-Null
New-Item -ItemType Directory -Path "$stage\database" -Force | Out-Null

# Copy binaries
Copy-Item "f:\PI5MAX\cpp\build\edge-gateway-backend" "$stage\bin\edge-gateway-backend"
Copy-Item "f:\PI5MAX\cpp\build\edge-gateway-updater" "$stage\bin\edge-gateway-updater"
Copy-Item "f:\PI5MAX\cpp\build\senzr-ctl" "$stage\bin\senzr-ctl"

# Copy top-level binaries for root-level direct invocation compatibility
Copy-Item "f:\PI5MAX\cpp\build\edge-gateway-backend" "$stage\edge-gateway-backend"
Copy-Item "f:\PI5MAX\cpp\build\edge-gateway-updater" "$stage\edge-gateway-updater"
Copy-Item "f:\PI5MAX\cpp\build\senzr-ctl" "$stage\senzr-ctl"

# Copy frontend
Copy-Item "f:\PI5MAX\cpp\frontend\*" "$stage\frontend\" -Recurse

# Copy database configuration
Copy-Item "f:\PI5MAX\database\config.json" "$stage\database\config.json"

# Copy updater config
Copy-Item "f:\PI5MAX\deploy\updater_config.env" "$stage\updater_config.env"

# Write version metadata
[System.IO.File]::WriteAllText("$stage\.version", "1.1.0`n")
[System.IO.File]::WriteAllText("$stage\version.txt", "1.1.0`n")

# Create Zip Archive using tar.exe
$zipPath = "f:\PI5MAX\downloads\v1.1.0.zip"
if (Test-Path $zipPath) { 
    Remove-Item -Force $zipPath 
}

Push-Location $stage
try {
    tar.exe -a -cf "$zipPath" * .version
} finally {
    Pop-Location
}

# Clean up staging directory
Start-Sleep -Milliseconds 200
Remove-Item -Recurse -Force $stage

Write-Host "Created $zipPath successfully"
Get-FileHash -Algorithm SHA256 $zipPath | Format-List
Get-Item $zipPath | Select-Object Name, Length, LastWriteTime | Format-List
