# Check all DLL dependencies in the package
param(
    [string]$TestDir = "C:\Users\DANIEL~1.LAM\AppData\Local\Temp\IoStoreDependencyViewer_Test"
)

cd $TestDir

$exe = 'IoStoreDependencyViewer.exe'
$dlls = Get-ChildItem *.dll | Select-Object -ExpandProperty Name

Write-Host 'Checking each packaged DLL for additional dependencies...' -ForegroundColor Cyan
Write-Host ''

$allMissing = @{}

# Check EXE
Write-Host "Checking $exe..." -ForegroundColor White
try {
    $bytes = [System.IO.File]::ReadAllBytes($exe)
    $content = [System.Text.Encoding]::ASCII.GetString($bytes)

    # Look for UnrealEditor DLL references
    $matches = [regex]::Matches($content, '(UnrealEditor-[A-Za-z0-9]+\.dll)', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)

    foreach ($match in $matches) {
        $refDll = $match.Groups[1].Value
        if ($refDll -notin $dlls) {
            if (-not $allMissing.ContainsKey($refDll)) {
                $allMissing[$refDll] = @()
            }
            $allMissing[$refDll] += $exe
        }
    }
} catch {
    Write-Host "  Error checking $exe" -ForegroundColor Red
}

# Check each DLL
foreach ($dll in $dlls) {
    try {
        $bytes = [System.IO.File]::ReadAllBytes($dll)
        $content = [System.Text.Encoding]::ASCII.GetString($bytes)

        # Look for UnrealEditor DLL references
        $matches = [regex]::Matches($content, '(UnrealEditor-[A-Za-z0-9]+\.dll)', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)

        foreach ($match in $matches) {
            $refDll = $match.Groups[1].Value
            if ($refDll -notin $dlls -and $refDll -ne $dll) {
                if (-not $allMissing.ContainsKey($refDll)) {
                    $allMissing[$refDll] = @()
                }
                if ($dll -notin $allMissing[$refDll]) {
                    $allMissing[$refDll] += $dll
                }
            }
        }
    } catch {
        # Skip
    }
}

if ($allMissing.Count -gt 0) {
    Write-Host ''
    Write-Host '====================================' -ForegroundColor Red
    Write-Host 'MISSING DLL DEPENDENCIES DETECTED!' -ForegroundColor Red
    Write-Host '====================================' -ForegroundColor Red
    Write-Host ''

    $sortedMissing = $allMissing.GetEnumerator() | Sort-Object Name
    foreach ($entry in $sortedMissing) {
        Write-Host "  [MISSING] $($entry.Key)" -ForegroundColor Red
        Write-Host "      Referenced by: $($entry.Value -join ', ')" -ForegroundColor Yellow
    }

    Write-Host ''
    Write-Host "Total missing DLLs: $($allMissing.Count)" -ForegroundColor Red
    Write-Host ''

    # Check if they exist in Engine binaries
    $engineBin = "D:\Fortnite4\Engine\Binaries\Win64"
    Write-Host "Checking Engine/Binaries/Win64 for missing DLLs..." -ForegroundColor Cyan
    Write-Host ''

    foreach ($entry in $sortedMissing) {
        $dllName = $entry.Key
        $sourcePath = Join-Path $engineBin $dllName
        if (Test-Path $sourcePath) {
            $size = (Get-Item $sourcePath).Length / 1MB
            Write-Host "  [FOUND] $dllName ($("{0:N2}" -f $size) MB)" -ForegroundColor Green
        } else {
            Write-Host "  [NOT IN ENGINE] $dllName" -ForegroundColor Yellow
        }
    }

    exit 1
} else {
    Write-Host ''
    Write-Host 'No missing DLL dependencies detected' -ForegroundColor Green
    exit 0
}
