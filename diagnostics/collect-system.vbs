Option Explicit

Dim outputPath, fileSystem, output, wmi

If WScript.Arguments.Count <> 1 Then WScript.Quit 2
outputPath = WScript.Arguments(0)
Set fileSystem = CreateObject("Scripting.FileSystemObject")
Set output = fileSystem.CreateTextFile(outputPath, True, False)

On Error Resume Next
Set wmi = GetObject("winmgmts:{impersonationLevel=impersonate}!\\.\root\cimv2")
If Err.Number <> 0 Then
    output.WriteLine "inventory_error=WMI unavailable"
    output.Close
    WScript.Quit 3
End If
On Error GoTo 0

Sub WriteValue(name, value)
    If IsNull(value) Then
        output.WriteLine name & "=[unavailable]"
    Else
        output.WriteLine name & "=" & CStr(value)
    End If
End Sub

Sub QueryOperatingSystem
    Dim item
    output.WriteLine "[OperatingSystem]"
    On Error Resume Next
    For Each item In wmi.ExecQuery("SELECT Caption,Version,BuildNumber,ServicePackMajorVersion,ServicePackMinorVersion,OSLanguage,TotalVisibleMemorySize FROM Win32_OperatingSystem")
        WriteValue "Name", item.Caption
        WriteValue "Version", item.Version
        WriteValue "BuildNumber", item.BuildNumber
        WriteValue "ServicePackMajor", item.ServicePackMajorVersion
        WriteValue "ServicePackMinor", item.ServicePackMinorVersion
        WriteValue "LanguageCode", item.OSLanguage
        WriteValue "VisibleMemoryKB", item.TotalVisibleMemorySize
    Next
    If Err.Number <> 0 Then output.WriteLine "Status=[query unavailable]"
    Err.Clear
    On Error GoTo 0
End Sub

Sub QueryComputerSystem
    Dim item
    output.WriteLine "[ComputerSystem]"
    On Error Resume Next
    For Each item In wmi.ExecQuery("SELECT Manufacturer,Model,TotalPhysicalMemory,NumberOfProcessors,SystemType FROM Win32_ComputerSystem")
        WriteValue "Manufacturer", item.Manufacturer
        WriteValue "Model", item.Model
        WriteValue "TotalPhysicalMemoryBytes", item.TotalPhysicalMemory
        WriteValue "PhysicalProcessorCount", item.NumberOfProcessors
        WriteValue "SystemType", item.SystemType
    Next
    If Err.Number <> 0 Then output.WriteLine "Status=[query unavailable]"
    Err.Clear
    On Error GoTo 0
End Sub

Sub QueryProcessors
    Dim item, index
    index = 0
    output.WriteLine "[Processors]"
    On Error Resume Next
    For Each item In wmi.ExecQuery("SELECT Name,Manufacturer,MaxClockSpeed,L2CacheSize,Architecture,Status FROM Win32_Processor")
        output.WriteLine "Processor=" & index
        WriteValue "Name", item.Name
        WriteValue "Manufacturer", item.Manufacturer
        WriteValue "MaxClockMHz", item.MaxClockSpeed
        WriteValue "L2CacheKB", item.L2CacheSize
        WriteValue "Architecture", item.Architecture
        WriteValue "Status", item.Status
        index = index + 1
    Next
    WriteValue "Count", index
    If Err.Number <> 0 Then output.WriteLine "Status=[query unavailable]"
    Err.Clear
    On Error GoTo 0
End Sub

Sub QueryVideo
    Dim item, index
    index = 0
    output.WriteLine "[VideoControllers]"
    On Error Resume Next
    For Each item In wmi.ExecQuery("SELECT Name,AdapterRAM,DriverVersion,VideoModeDescription,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentBitsPerPixel,CurrentRefreshRate,Status FROM Win32_VideoController")
        output.WriteLine "VideoController=" & index
        WriteValue "Name", item.Name
        WriteValue "AdapterRAMBytes", item.AdapterRAM
        WriteValue "DriverVersion", item.DriverVersion
        WriteValue "VideoMode", item.VideoModeDescription
        WriteValue "Width", item.CurrentHorizontalResolution
        WriteValue "Height", item.CurrentVerticalResolution
        WriteValue "ColorBits", item.CurrentBitsPerPixel
        WriteValue "RefreshHz", item.CurrentRefreshRate
        WriteValue "Status", item.Status
        index = index + 1
    Next
    WriteValue "Count", index
    If Err.Number <> 0 Then output.WriteLine "Status=[query unavailable]"
    Err.Clear
    On Error GoTo 0
End Sub

Sub QuerySound
    Dim item, index
    index = 0
    output.WriteLine "[SoundDevices]"
    On Error Resume Next
    For Each item In wmi.ExecQuery("SELECT Name,Manufacturer,Status FROM Win32_SoundDevice")
        output.WriteLine "SoundDevice=" & index
        WriteValue "Name", item.Name
        WriteValue "Manufacturer", item.Manufacturer
        WriteValue "Status", item.Status
        index = index + 1
    Next
    WriteValue "Count", index
    If Err.Number <> 0 Then output.WriteLine "Status=[query unavailable]"
    Err.Clear
    On Error GoTo 0
End Sub

Sub QueryHotFixes
    Dim item, count
    count = 0
    output.WriteLine "[HotFixes]"
    On Error Resume Next
    For Each item In wmi.ExecQuery("SELECT HotFixID FROM Win32_QuickFixEngineering")
        If Not IsNull(item.HotFixID) Then
            output.WriteLine "HotFix=" & CStr(item.HotFixID)
            count = count + 1
        End If
    Next
    WriteValue "Count", count
    If Err.Number <> 0 Then output.WriteLine "Status=[query unavailable]"
    Err.Clear
    On Error GoTo 0
End Sub

output.WriteLine "Supermium Windows 2000 Release Candidate 1 - Privacy-Preserving System Inventory"
output.WriteLine "Schema=1"
output.WriteLine "Excluded=Username,ComputerName,SerialNumbers,ProductKeys,IPAddresses,MACAddresses,NetworkConfiguration,UserFiles"
QueryOperatingSystem
QueryComputerSystem
QueryProcessors
QueryVideo
QuerySound
QueryHotFixes
output.Close
