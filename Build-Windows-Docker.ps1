param([string]$OutputDirectory = "dist")
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$root = $PSScriptRoot
Push-Location $root
try {
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        throw "Docker Desktop was not found. Install/start Docker Desktop and select Linux containers."
    }
    $os = (& docker info --format '{{.OSType}}' 2>$null | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Docker Desktop is not ready. Start it and retry." }
    if ($os -ne "linux") { throw "Select Linux containers in Docker Desktop." }

    if ([IO.Path]::IsPathRooted($OutputDirectory)) { $out = $OutputDirectory }
    else { $out = Join-Path $root $OutputDirectory }
    if ($out.Contains(',')) { throw "The output path cannot contain a comma." }
    if (Test-Path $out) {
        if (Get-ChildItem -LiteralPath $out -Force | Select-Object -First 1) {
            throw "Output directory is not empty: $out`nDelete/rename it, or choose a fresh -OutputDirectory."
        }
    }

    $logs = Join-Path $root "logs"
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $log = Join-Path $logs "Build-Windows-Docker-$stamp.log"

    Write-Host "Building Xemu HDD Tools Windows x64 with MinGW in Docker Desktop."
    Write-Host "Docker compiles/exports the Windows program and packages the pinned qemu-img helper; it does not execute QEMU/Wine."
    Write-Host "Build log: $log"
    Write-Host ""

    # Windows PowerShell 5.1 converts native stderr lines into ErrorRecord objects.
    # Docker Buildx writes normal progress to stderr, so with ErrorActionPreference=Stop
    # the old wrapper could abort on a harmless progress line before Docker finished.
    # Temporarily allow native stderr through the merged pipeline, stringify it, and
    # decide success strictly from Docker's process exit code.
    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & docker buildx build --platform linux/amd64 --target windows-artifacts --progress plain --output "type=local,dest=$out" . 2>&1 |
            ForEach-Object {
                $line = if ($_ -is [System.Management.Automation.ErrorRecord]) {
                    $_.Exception.Message
                } else {
                    $_.ToString()
                }
                Write-Host $line
                Add-Content -LiteralPath $log -Value $line -Encoding UTF8
            }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if ($code -ne 0) { throw "Windows build failed (Docker exit $code). See: $log" }

    $gui = Join-Path $out 'windows-x64\Xemu-HDD-Tools.exe'
    $cli = Join-Path $out 'windows-x64\xemu-hdd-convert.exe'
    if (-not (Test-Path -LiteralPath $gui)) { throw "Expected GUI executable was not exported: $gui" }
    if (-not (Test-Path -LiteralPath $cli)) { throw "Expected CLI executable was not exported: $cli" }

    Write-Host ""
    Write-Host "WINDOWS BUILD: PASS" -ForegroundColor Green
    Write-Host "GUI: $gui"
    Write-Host "CLI: $cli"
    Write-Host "Log: $log"
    Write-Host ""
    Write-Host "Bundled qemu-img helper: $out\windows-x64\tools\qemu-img.exe"
} finally {
    Pop-Location
}
