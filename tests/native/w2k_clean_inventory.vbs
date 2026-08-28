Option Explicit

Const HKEY_LOCAL_MACHINE = &H80000002

Dim wmi, registry, item, subkeys, key, value
Set wmi = GetObject("winmgmts:{impersonationLevel=impersonate}!\\.\root\cimv2")
Set registry = GetObject("winmgmts:{impersonationLevel=impersonate}!\\.\root\default:StdRegProv")

WScript.Echo "[OperatingSystem]"
For Each item In wmi.ExecQuery("SELECT Caption,Version,ServicePackMajorVersion,ServicePackMinorVersion,BuildNumber,OSLanguage,TotalVisibleMemorySize,FreePhysicalMemory FROM Win32_OperatingSystem")
    WScript.Echo "Caption=" & item.Caption
    WScript.Echo "Version=" & item.Version
    WScript.Echo "ServicePack=" & item.ServicePackMajorVersion & "." & item.ServicePackMinorVersion
    WScript.Echo "BuildNumber=" & item.BuildNumber
    WScript.Echo "OSLanguage=" & item.OSLanguage
    WScript.Echo "TotalVisibleMemoryKB=" & item.TotalVisibleMemorySize
    WScript.Echo "FreePhysicalMemoryKB=" & item.FreePhysicalMemory
Next

WScript.Echo "[CurrentVersionRegistry]"
For Each key In Array("CSDVersion", "CurrentBuildNumber", "ProductName", "RegisteredOwner")
    value = ""
    registry.GetStringValue HKEY_LOCAL_MACHINE, "SOFTWARE\Microsoft\Windows NT\CurrentVersion", key, value
    WScript.Echo key & "=" & value
Next

WScript.Echo "[HotFixRegistry]"
subkeys = Null
registry.EnumKey HKEY_LOCAL_MACHINE, "SOFTWARE\Microsoft\Windows NT\CurrentVersion\HotFix", subkeys
If IsNull(subkeys) Then
    WScript.Echo "Count=0"
Else
    WScript.Echo "Count=" & (UBound(subkeys) - LBound(subkeys) + 1)
    For Each key In subkeys
        WScript.Echo key
    Next
End If

WScript.Echo "[QuickFixEngineering]"
For Each item In wmi.ExecQuery("SELECT HotFixID,Description,InstalledOn FROM Win32_QuickFixEngineering")
    WScript.Echo item.HotFixID & "|" & item.Description & "|" & item.InstalledOn
Next

WScript.Echo "[SoundDevice]"
For Each item In wmi.ExecQuery("SELECT Name,Manufacturer,Status,PNPDeviceID FROM Win32_SoundDevice")
    WScript.Echo "Name=" & item.Name
    WScript.Echo "Manufacturer=" & item.Manufacturer
    WScript.Echo "Status=" & item.Status
    WScript.Echo "PNPDeviceID=" & item.PNPDeviceID
Next

WScript.Echo "[NetworkConfiguration]"
For Each item In wmi.ExecQuery("SELECT Description,IPAddress,DefaultIPGateway,DNSServerSearchOrder FROM Win32_NetworkAdapterConfiguration WHERE IPEnabled=True")
    WScript.Echo "Description=" & item.Description
    If Not IsNull(item.IPAddress) Then WScript.Echo "IPAddress=" & Join(item.IPAddress, ",")
    If Not IsNull(item.DefaultIPGateway) Then WScript.Echo "DefaultGateway=" & Join(item.DefaultIPGateway, ",")
    If Not IsNull(item.DNSServerSearchOrder) Then WScript.Echo "DNS=" & Join(item.DNSServerSearchOrder, ",")
Next
