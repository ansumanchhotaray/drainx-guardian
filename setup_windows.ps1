# Run as Administrator!
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Warning "CRITICAL: You must run this script as Administrator!"
    Write-Warning "Right-click the file and select 'Run with PowerShell', or open an Admin PowerShell window and run it."
    Start-Sleep -Seconds 10
    Exit
}

Write-Host "=== DrainX Guardian: Windows Mosquitto Setup ===" -ForegroundColor Cyan

Write-Host "`n1. Checking for Mosquitto..."
if (Get-Command "mosquitto" -ErrorAction SilentlyContinue) {
    Write-Host "Mosquitto is already installed." -ForegroundColor Green
} else {
    Write-Host "Installing Mosquitto via winget..." -ForegroundColor Yellow
    winget install -e --id EclipseFoundation.Mosquitto --accept-package-agreements --accept-source-agreements
    
    # Add to PATH for this session so we can run it immediately
    $env:Path += ";C:\Program Files\mosquitto"
}

Write-Host "`n2. Opening Windows Firewall for Port 1883..."
# The ESP32 cannot connect to the Windows Hotspot unless the firewall allows it!
Remove-NetFirewallRule -DisplayName "Allow MQTT (Mosquitto) Port 1883" -ErrorAction SilentlyContinue
New-NetFirewallRule -DisplayName "Allow MQTT (Mosquitto) Port 1883" -Direction Inbound -LocalPort 1883 -Protocol TCP -Action Allow | Out-Null
Write-Host "Firewall rule added successfully." -ForegroundColor Green

Write-Host "`n3. Creating Custom Mosquitto Configuration..."
# Mosquitto 2.0+ blocks external connections by default. We MUST explicitly allow the ESP32 to connect.
$confPath = "$env:TEMP\mosquitto_custom.conf"
@"
listener 1883 0.0.0.0
allow_anonymous true
"@ | Out-File -FilePath $confPath -Encoding ASCII
Write-Host "Config created to allow external ESP32 connections." -ForegroundColor Green

Write-Host "`n4. Stopping default background service..."
# Stop the default service so we can run it manually and see the logs
Stop-Service -Name "mosquitto" -ErrorAction SilentlyContinue

Write-Host "`n5. Starting Mosquitto Broker..." -ForegroundColor Cyan
Write-Host "=======================================================" -ForegroundColor Yellow
Write-Host "Mosquitto is now running! LEAVE THIS WINDOW OPEN." -ForegroundColor Yellow
Write-Host "You will see connection logs here when the ESP32 connects." -ForegroundColor Yellow
Write-Host "=======================================================" -ForegroundColor Yellow

# Run mosquitto visibly with verbose logging
mosquitto.exe -c $confPath -v
