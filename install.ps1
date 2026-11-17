# Installs labitop for the current user, so that "labitop" works in any new terminal.
#
#   1. Builds build\labitop.exe (when g++ is available; otherwise uses the existing file).
#   2. Copies it to %LOCALAPPDATA%\Programs\labitop.
#   3. Adds that folder to the user PATH (no administrator rights needed).
#
# -Uninstall removes the PATH entry and the installed file again.
# Normally run through install.bat; directly:
#   powershell -NoProfile -ExecutionPolicy Bypass -File install.ps1 [-Uninstall]
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'

$InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\labitop'

# ---- PATH helpers (plain string functions, so they can be tested on their own)

function Test-SamePath([string]$a, [string]$b) {
    $x = [Environment]::ExpandEnvironmentVariables($a).Trim().TrimEnd('\')
    $y = [Environment]::ExpandEnvironmentVariables($b).Trim().TrimEnd('\')
    return $x -ieq $y
}

# PATH is a ';'-separated list. Entries are compared case-insensitively, with variables
# such as %USERPROFILE% expanded and a trailing '\' ignored.
function Test-PathContains([string]$path, [string]$dir) {
    foreach ($p in ($path -split ';')) {
        if ($p -ne '' -and (Test-SamePath $p $dir)) { return $true }
    }
    return $false
}

function Add-PathEntry([string]$path, [string]$dir) {
    $parts = @($path -split ';' | Where-Object { $_ -ne '' })
    return (@($parts) + $dir) -join ';'
}

function Remove-PathEntry([string]$path, [string]$dir) {
    $parts = @($path -split ';' | Where-Object { $_ -ne '' -and -not (Test-SamePath $_ $dir) })
    return $parts -join ';'
}

# ---- the user PATH in the registry (HKCU\Environment)
# Read and written raw, so entries like %USERPROFILE%\bin keep their variables.

function Get-UserPath {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment')
    try {
        return [string]$key.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    } finally {
        $key.Close()
    }
}

function Set-UserPath([string]$value) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $true)
    try {
        $key.SetValue('Path', $value, [Microsoft.Win32.RegistryValueKind]::ExpandString)
    } finally {
        $key.Close()
    }
    # Tell Explorer and other running programs that the environment changed, so terminals
    # started from now on see the new PATH without signing out.
    Add-Type -Namespace Labitop -Name Native -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode)]
public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint msg, UIntPtr wParam, string lParam,
                                               uint flags, uint timeout, out UIntPtr result);
'@
    $result = [UIntPtr]::Zero
    [void][Labitop.Native]::SendMessageTimeout([IntPtr]0xffff, 0x1A, [UIntPtr]::Zero, 'Environment', 2, 5000, [ref]$result)
}

# Dot-sourcing (". .\install.ps1") only loads the functions above.
if ($MyInvocation.InvocationName -eq '.') { return }

# ---- uninstall

if ($Uninstall) {
    $path = Get-UserPath
    if (Test-PathContains $path $InstallDir) {
        Set-UserPath (Remove-PathEntry $path $InstallDir)
        Write-Host "Removed from PATH: $InstallDir"
    } else {
        Write-Host "Not in PATH: $InstallDir"
    }
    $target = Join-Path $InstallDir 'labitop.exe'
    if (Test-Path $target) {
        try {
            Remove-Item $target -Force
        } catch {
            throw "Could not delete $target. If labitop is running, close it and try again."
        }
        Write-Host "Deleted: $target"
    }
    if ((Test-Path $InstallDir) -and -not (Get-ChildItem $InstallDir -Force)) { Remove-Item $InstallDir }
    Write-Host 'labitop is uninstalled. Terminals opened from now on no longer find it.'
    exit 0
}

# ---- install

$exe = Join-Path $PSScriptRoot 'build\labitop.exe'
if (Get-Command g++ -ErrorAction SilentlyContinue) {
    $env:LABITOP_NOPAUSE = 1  # install.bat pauses once at the end; build.bat must not
    & cmd.exe /c "`"$PSScriptRoot\build.bat`""
    if ($LASTEXITCODE -ne 0) { throw 'Build failed; nothing was installed.' }
} elseif (Test-Path $exe) {
    Write-Host 'g++ not found: installing the existing build\labitop.exe as it is.'
} else {
    throw 'g++ not found and build\labitop.exe does not exist. Install MinGW-w64 GCC (scoop install gcc) first.'
}

New-Item -ItemType Directory -Force $InstallDir | Out-Null
$target = Join-Path $InstallDir 'labitop.exe'
try {
    Copy-Item $exe $target -Force
} catch {
    throw "Could not replace $target. If labitop is running, close it and try again."
}
Write-Host "Installed: $target"

$path = Get-UserPath
if (Test-PathContains $path $InstallDir) {
    Write-Host "Already in PATH: $InstallDir"
} else {
    Set-UserPath (Add-PathEntry $path $InstallDir)
    Write-Host "Added to the user PATH: $InstallDir"
}

Write-Host ''
Write-Host 'Open a new terminal and run:  labitop'
Write-Host '(Terminals that are already open keep their old PATH.)'
