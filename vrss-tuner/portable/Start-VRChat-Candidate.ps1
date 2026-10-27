param([string]$VRChatDirectory='C:\Program Files (x86)\Steam\steamapps\common\VRChat',
      [int]$Profile=0,[switch]$ValidateOnly)
& (Join-Path $PSScriptRoot 'Start-VRChat-HMD-Key.ps1') `
    -VRChatDirectory $VRChatDirectory -Profile $Profile -ValidateOnly:$ValidateOnly
