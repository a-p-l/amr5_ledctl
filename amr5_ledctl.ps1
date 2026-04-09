param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("off","button","static","default","rainbow","breath","cycle")]
    [string]$mode,

    [string]$dll = "",

    [int]$delay = 10
)

# --- Resolve the DLL path robustly ---
if (-not $dll) {
    $scriptDir = if ($PSScriptRoot) {
        $PSScriptRoot
    } elseif ($MyInvocation.MyCommand.Path) {
        Split-Path $MyInvocation.MyCommand.Path
    } else {
        Get-Location
    }

    $dll = Join-Path $scriptDir "inpoutx64.dll"
}

#Write-Host "Using DLL: $dll"

if (-not (Test-Path -LiteralPath $dll)) {
    throw "DLL not found: $dll"
}

# --- Bitness check ---
if ([IntPtr]::Size -ne 8) {
    Write-Warning "You are NOT running 64-bit PowerShell!"
    Write-Warning "Use: C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe"
    exit 1
}

# --- Native API + delegates ---
$source = @"
using System;
using System.Runtime.InteropServices;

public static class Native {
    [DllImport("kernel32", SetLastError=true, CharSet=CharSet.Ansi)]
    public static extern IntPtr LoadLibrary(string lpFileName);

    [DllImport("kernel32", SetLastError=true, CharSet=CharSet.Ansi)]
    public static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);

    [DllImport("kernel32", SetLastError=true)]
    public static extern bool FreeLibrary(IntPtr hModule);
}

[UnmanagedFunctionPointer(CallingConvention.StdCall)]
public delegate void Out32Delegate(short port, short data);

[UnmanagedFunctionPointer(CallingConvention.StdCall)]
public delegate short Inp32Delegate(short port);

[UnmanagedFunctionPointer(CallingConvention.StdCall)]
public delegate bool IsInpOutDriverOpenDelegate();
"@

Add-Type -TypeDefinition $source

# --- Load the DLL ---
$absDllPath = (Resolve-Path -LiteralPath $dll).Path
$dllHandle = [Native]::LoadLibrary($absDllPath)

if ($dllHandle -eq [IntPtr]::Zero) {
    throw "LoadLibrary failed: $absDllPath"
}

try {
    # --- Resolve function pointers ---
    $out32Ptr = [Native]::GetProcAddress($dllHandle, "Out32")
    $inp32Ptr = [Native]::GetProcAddress($dllHandle, "Inp32")
    $isOpenPtr = [Native]::GetProcAddress($dllHandle, "IsInpOutDriverOpen")

    if ($out32Ptr -eq [IntPtr]::Zero) { throw "Out32 not found" }
    if ($inp32Ptr -eq [IntPtr]::Zero) { throw "Inp32 not found" }
    if ($isOpenPtr -eq [IntPtr]::Zero) { throw "IsInpOutDriverOpen not found" }

    # --- Create delegates ---
    $Out32 = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer(
        $out32Ptr,
        [Out32Delegate]
    )
    $IsOpen = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer(
        $isOpenPtr,
        [IsInpOutDriverOpenDelegate]
    )

    if (-not $IsOpen.Invoke()) {
        throw "InpOut driver not open. Run as Administrator!"
    }

    # --- Ports ---
    $INDEX_PORT = 0x4E
    $DATA_PORT  = 0x4F

    # --- Registers ---
    $REG_MODE   = 0x4BE
    $REG_PARAM1 = 0x45B
    $REG_PARAM2 = 0x45C

    function Write8([int]$Port, [int]$Value) {
        $Out32.Invoke([int16]$Port, [int16]($Value -band 0xFF))
    }

    function Write-Reg([int]$Reg, [int]$Value) {
        $high = ($Reg -shr 8) -band 0xFF
        $low  = $Reg -band 0xFF

        Write8 $INDEX_PORT 0x2E
        Write8 $DATA_PORT  0x11
        Write8 $INDEX_PORT 0x2F
        Write8 $DATA_PORT  $high

        Write8 $INDEX_PORT 0x2E
        Write8 $DATA_PORT  0x10
        Write8 $INDEX_PORT 0x2F
        Write8 $DATA_PORT  $low

        Write8 $INDEX_PORT 0x2E
        Write8 $DATA_PORT  0x12
        Write8 $INDEX_PORT 0x2F
        Write8 $DATA_PORT  ($Value -band 0xFF)
    }

    # --- Reset ---
    Write-Reg $REG_PARAM2 0

    # --- Apply mode ---
    switch ($mode) {
        "static" {
            Write-Reg $REG_MODE 2
            Start-Sleep -Milliseconds $delay
            Write-Reg $REG_MODE 3
        }
        "button" {
            Write-Reg $REG_MODE 7
            Start-Sleep -Milliseconds $delay
            Write-Reg $REG_MODE 3
        }
        "rainbow" {
            Write-Reg $REG_PARAM1 0x32
            Write-Reg $REG_PARAM2 0x07
            Write-Reg $REG_MODE 5
        }
        "breath" {
            Write-Reg $REG_MODE 2
        }
        "cycle" {
            Write-Reg $REG_MODE 6
        }
        "default" {
            Write-Reg $REG_MODE 1
        }
        "off" {
            Write-Reg $REG_MODE 7
        }
    }

    #Write-Host "Mode applied: $mode"
}
finally {
    if ($dllHandle -ne [IntPtr]::Zero) {
        [void][Native]::FreeLibrary($dllHandle)
    }
}
