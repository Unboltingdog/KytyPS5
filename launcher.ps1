param([switch]$DryRun)
# A small settings window in front of run-windows.ps1 and precompile-windows.ps1 (launcher.cmd opens it). The
# settings and the console language names follow the KytyPS5 launcher (src/launcher, by the KytyPS5 developers);
# they are kept in launcher-settings.json next to this script. -DryRun prints the commands without a window.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
# cmd.exe by its full path: Start-Process with a bare "cmd" opens whatever "cmd" it finds first, and a file of that
# name without an extension (in the working folder or on the PATH) made Windows ask which app should open it (issue #47).
$cmd = if ($env:ComSpec -and (Test-Path -LiteralPath $env:ComSpec)) { $env:ComSpec } else { Join-Path ([Environment]::SystemDirectory) 'cmd.exe' }
# Windows PowerShell by its path too: on a PC whose PATH lacks its folder, cmd.exe did not know "powershell" (issue #55).
$powershellExe = Join-Path ([Environment]::SystemDirectory) 'WindowsPowerShell\v1.0\powershell.exe'
$powershellCommand = if (Test-Path -LiteralPath $powershellExe) { "`"$powershellExe`"" } else { 'powershell' }
$settingsPath = Join-Path $root 'launcher-settings.json'
$languages = 'Japanese', 'English (United States)', 'French (France)', 'Spanish (Spain)', 'German', 'Italian', 'Dutch',
	'Portuguese (Portugal)', 'Russian', 'Korean', 'Chinese (Traditional)', 'Chinese (Simplified)', 'Finnish', 'Swedish',
	'Danish', 'Norwegian', 'Polish', 'Portuguese (Brazil)', 'English (United Kingdom)', 'Turkish', 'Spanish (Latin America)',
	'Arabic', 'French (Canada)', 'Czech', 'Hungarian', 'Greek', 'Romanian', 'Thai', 'Vietnamese', 'Indonesian'
# The window's sizes for each aspect ratio of the 3D (below).
$resolutionsByRatio = @{
	'16:9' = '1280x720', '1920x1080', '2560x1440', '3840x2160'
	'21:9' = '2560x1080', '3440x1440', '3840x1600', '5120x2160'
	'32:9' = '3840x1080', '5120x1440', '7680x2160' }
# The height Demon's Souls renders its 3D at (run-windows.ps1 -RenderHeight); its output and UI stay 3840x2160.
$renderHeights = '2160 (native 4K)', '1800', '1620', '1512 (PS5 Pro)', '1440 (PS5)', '1296', '1152', '1080', '900'
# What makes the 3840x2160 image of the 3D (run-windows.ps1 -Upscaler): the game's TAA and stretch (the PS5's), or DLSS
# (NVIDIA's Streamline runtime, as frame generation) or AMD FSR 3.1 (any GPU) in place of Sony's PS5 Pro upscaler on the
# game's PS5 Pro path.
$upscalers = 'Game (PS5)', 'DLSS (PS5 Pro path)', 'FSR 3.1 (PS5 Pro path)'
$upscalerKeys = '', 'dlss', 'fsr'
# The render height as a ratio of the 2160 output (run-windows.ps1 -UpscaleQuality), FSR's quality modes; Custom keeps
# the Render height row's own height.
$presets = 'Custom (Render height below)', 'Native AA (1x)', 'Ultra Quality (1.3x)', 'Quality (1.5x)', 'Balanced (1.7x)',
	'Performance (2x)', 'Ultra Performance (3x)'
$presetKeys = '', 'native', 'ultraquality', 'quality', 'balanced', 'performance', 'ultraperformance'
$presetRatios = 0.0, 1.0, 1.3, 1.5, 1.7, 2.0, 3.0
# The aspect ratio of Demon's Souls' 3D (run-windows.ps1 -Aspect): wider sees more to the sides.
$ratios = '16:9 (PS5)', '21:9', '32:9'
# Demon's Souls' own settings for what a frame draws (run-windows.ps1 -Quality): shorter shadows, simpler distant detail;
# and smaller textures (run-windows.ps1 -TextureReduce: Low 1, Lowest 2; Standard leaves it to the emulator: 1 below
# 12 GB of video memory, else 0).
$qualities = 'Standard (PS5)', 'Low', 'Lowest'
# Frame generation (run-windows.ps1 -FrameGen: the generated frames per rendered one, the multiplier less one): NVIDIA
# DLSS-G (RTX 40 and newer, up to 6x) or AMD FSR 3 (any GPU, 2x; run-windows.ps1 -FrameGenBackend fsr).
$frameGens = 'Off', '2x', '3x', '4x', '5x', '6x'
$frameGenBackends = 'DLSS-G (NVIDIA RTX 40+)', 'FSR 3 (any GPU)'
$frameGenBackendKeys = 'dlss', 'fsr'
# The controller hotkey button (hold it + another button; src/local/controller-hotkeys.h), kept in hotkeys.ini next to the
# emulator, where the game changes it too (the button + R3, then the new one).
# The buttons by the names hotkeys.ini takes, and where each is on a DualSense.
$hotkeyButtonInfo = [ordered]@{
	Create = 'Create (Xbox: Back / View, small left button)'; PS = 'PS (Xbox: Guide; Steam or Game Bar may take it)'
	Options = 'Options (Xbox: Start / Menu, small right button)'; Touchpad = 'Touchpad (PlayStation pads only)'
	Mute = 'Mute (DualSense only)'; Cross = 'Cross (Xbox: A)'; Circle = 'Circle (Xbox: B)'; Square = 'Square (Xbox: X)'
	Triangle = 'Triangle (Xbox: Y)'; L1 = 'L1 (Xbox: LB)'; R1 = 'R1 (Xbox: RB)'; L3 = 'L3 (press the left stick)'
	R3 = 'R3 (press the right stick)'; Up = 'Up (D-pad)'; Down = 'Down (D-pad)'; Left = 'Left (D-pad)'; Right = 'Right (D-pad)'
	Paddle1 = 'Paddle1 (back paddle)'; Paddle2 = 'Paddle2 (back paddle)'; Paddle3 = 'Paddle3 (back paddle)'
	Paddle4 = 'Paddle4 (back paddle)' }
# A button's name (hotkeys.ini's) from its label in a list; '' for none.
function Get-HotkeyNameFromText([string]$text) {
	foreach ($name in $hotkeyButtonInfo.Keys) { if ($hotkeyButtonInfo[$name] -eq $text) { return $name } }
	''
}
# The buttons offered as the hold button (ones the game does not need are best).
$hotkeyButtons = 'Create', 'PS', 'L3', 'R3', 'L1', 'R1', 'Touchpad', 'Mute', 'Paddle1', 'Paddle2'
# What each hotkey does, by its hotkeys.ini key, and its default button.
$hotkeyActions = [ordered]@{
	Quit = 'Quit the game (hold 1 second)'; Fullscreen = 'Full screen on / off'; Pause = 'Pause / resume'
	Overlay = 'Performance overlay (off, frame rate, detailed)'; FrameGen = 'Frame generation on / off'
	SharpnessUp = 'FSR sharpness +'; SharpnessDown = 'FSR sharpness -'; Help = 'Show the hotkeys on screen'
	RebindModifier = 'Choose a new hold button in the game' }
$hotkeyDefaults = @{ Modifier = 'Create'; QuitChord = ''; Quit = 'Options'; Fullscreen = 'Triangle'; Pause = 'Square'; Overlay = 'Circle'
	FrameGen = 'Cross'; SharpnessUp = 'Up'; SharpnessDown = 'Down'; Help = 'L3'; RebindModifier = 'R3' }
# The emulator's own panel at the top right of the game image (KYTY_FPS_HUD 0, 1, 2).
$overlays = 'Off', 'Frame rate', 'Detailed'
# Safe GPU mode (KYTY_COMPUTE_CHAIN=0): Demon's Souls' compute work runs one dispatch after another instead of letting
# the GPU overlap them. Slower, but it stops the GPU hangs ("device lost", the driver resetting the display) that some
# PCs have: on an RTX 3080 Laptop with driver 566.07, 3 of 4 runs through 1-1 hung with overlap, none of 3 without it,
# at 25-48% less frame rate there (13.9-17.7 fps instead of 26.8 at 1-1, 21-22 instead of 28.8 at 2-2).
$safeGpuTip = "Prevents the crashes and blue screens that come from the graphics card hanging: 'device lost', the screen going`n" +
	"black while the graphics driver restarts (a TDR), or a VIDEO_TDR_FAILURE blue screen. The emulator then runs Demon's Souls'`n" +
	"compute work one step after another instead of letting the graphics card overlap the steps. The frame rate is about 37%`n" +
	"lower (on a laptop RTX 3080 at Boletaria: 17 fps instead of 27). It does not help MEMORY_MANAGEMENT blue screens, which`n" +
	"come from the RAM being full (see the README). Leave it off if the game runs without such crashes; a current graphics`n" +
	"driver can help too."

$settings = [ordered]@{ game = ''; resolution = '2560x1440'; fullscreen = $false; aspect = $true; language = 1; redzone = $true;
	ecores = $false; fps120 = $false; render = 1440; ratio = '16:9'; quality = 'standard'; vsync = $false; hdr = $false; framegen = 0
	hud = 0; streamline = ''; safegpu = $false; upscaler = ''; render_dlss = 1080; upscale_quality = 'quality'
	framegen_backend = ''; fsr_sharpness = 0.25; fg_hudless = $true; rumble = 2 }
if (Test-Path $settingsPath) {
	$saved = Get-Content $settingsPath -Raw | ConvertFrom-Json
	foreach ($property in $saved.PSObject.Properties) { if ($settings.Contains($property.Name)) { $settings[$property.Name] = $property.Value } }
	# (Settings from before the quality presets keep their own render height: Custom.)
	if (!$saved.PSObject.Properties['upscale_quality']) { $settings.upscale_quality = '' }
}
if (!$settings.game -and (Test-Path "$root\game-path.txt")) { $settings.game = (Get-Content "$root\game-path.txt" -TotalCount 1).Trim() }

# The efficiency cores (the lowest efficiency class of a hybrid CPU) as an affinity mask, or 0.
function Get-EfficiencyMask {
	Add-Type -Namespace KytyLauncher -Name CpuSets -MemberDefinition @'
[DllImport("kernel32.dll")]
static extern bool GetSystemCpuSetInformation(IntPtr information, uint length, out uint returned, IntPtr process, uint flags);
// Group 0's logical processors as (index, efficiency class) pairs.
public static int[] Query() {
	var result = new System.Collections.Generic.List<int>();
	uint length;
	GetSystemCpuSetInformation(IntPtr.Zero, 0, out length, IntPtr.Zero, 0);
	if (length == 0) return result.ToArray();
	IntPtr buffer = Marshal.AllocHGlobal((int)length);
	try {
		if (!GetSystemCpuSetInformation(buffer, length, out length, IntPtr.Zero, 0)) return result.ToArray();
		for (int offset = 0; offset < length; offset += Marshal.ReadInt32(buffer, offset)) {
			if (Marshal.ReadInt32(buffer, offset + 4) != 0 || Marshal.ReadInt16(buffer, offset + 12) != 0) continue;
			result.Add(Marshal.ReadByte(buffer, offset + 14));
			result.Add(Marshal.ReadByte(buffer, offset + 18));
		}
	} finally {
		Marshal.FreeHGlobal(buffer);
	}
	return result.ToArray();
}
'@
	$values = [KytyLauncher.CpuSets]::Query()
	$sets = for ($i = 0; $i + 1 -lt $values.Count; $i += 2) { [pscustomobject]@{ Cpu = $values[$i]; Class = $values[$i + 1] } }
	if (@($sets | ForEach-Object Class | Sort-Object -Unique).Count -lt 2) { return [int64]0 }
	$slowest = ($sets | Measure-Object Class -Minimum).Minimum
	$mask = [int64]0
	foreach ($set in $sets | Where-Object { $_.Class -eq $slowest -and $_.Cpu -lt 63 }) { $mask = $mask -bor ([int64]1 -shl $set.Cpu) }
	return $mask
}
$efficiencyMask = Get-EfficiencyMask
# Frame generation needs NVIDIA's Streamline runtime, which no release ships: the Streamline SDK
# (github.com/NVIDIA-RTX/Streamline/releases) chosen with "Streamline..." (settings: streamline), else a "streamline"
# folder next to the emulator, else the SDK beside a local build (_Build\deps\streamline\sdk). The emulator loads
# only NVIDIA's own sl.interposer.dll (run-windows -StreamlineDir).
$exe = if (Test-Path "$root\kyty_emulator.exe") { "$root\kyty_emulator.exe" } else { "$root\_Build\windows\kyty_emulator.exe" }
$streamlineSdkUrl = 'https://github.com/NVIDIA-RTX/Streamline/releases'
# The runtime's folder in $dir (the folder itself or its bin\x64) when its sl.interposer.dll is signed by NVIDIA;
# $null otherwise.
function Find-Streamline([string]$dir) {
	if (!$dir) { return $null }
	foreach ($folder in @($dir, (Join-Path $dir 'bin\x64'))) {
		$file = Join-Path $folder 'sl.interposer.dll'
		if (Test-Path -LiteralPath $file) {
			$signature = Get-AuthenticodeSignature -LiteralPath $file
			if ($signature.Status -eq 'Valid' -and $signature.SignerCertificate.Subject -match 'O=NVIDIA Corporation') { return $folder }
			return $null
		}
	}
	return $null
}
function Resolve-Streamline {
	if ($settings.streamline) { return Find-Streamline $settings.streamline }
	if ($env:KYTY_STREAMLINE_DIR) { return Find-Streamline $env:KYTY_STREAMLINE_DIR }
	foreach ($dir in @((Join-Path (Split-Path $exe) 'streamline'), (Join-Path (Split-Path (Split-Path $exe)) 'deps\streamline\sdk'))) {
		$found = Find-Streamline $dir
		if ($found) { return $found }
	}
	return $null
}
$streamline = Resolve-Streamline
# DLSS (the upscaler and frame generation) needs an NVIDIA RTX graphics card: with none (AMD, Intel, GeForce GTX) both
# rows stay off and the game's own TAA makes the picture, at the render height the game's TAA keeps.
$rtxGpu = try {
	[bool](Get-CimInstance Win32_VideoController | Where-Object { $_.Name -match 'NVIDIA' -and $_.Name -match '\bRTX\b' })
} catch { $false }
$frameGenAvailable = [bool]$streamline -and $rtxGpu
# AMD FSR 3.1 (upscaling and frame generation) runs on any Vulkan 1.3 GPU with AMD's runtime next to the emulator (the
# build copies 3rdparty\fidelityfx\bin\amd_fidelityfx_vk.dll there).
$fsrAvailable = Test-Path -LiteralPath (Join-Path (Split-Path $exe) 'amd_fidelityfx_vk.dll')
function Test-Upscaler([string]$key) {
	switch ($key) { 'dlss' { return $frameGenAvailable } 'fsr' { return $fsrAvailable } default { return $true } }
}
# The frame generation backend chosen, else the one this PC has: DLSS-G with an RTX card and Streamline, else FSR 3.
function Get-FrameGenBackend {
	if ($settings.framegen_backend -in 'dlss', 'fsr') { return $settings.framegen_backend }
	if ($frameGenAvailable) { 'dlss' } else { 'fsr' }
}
function Test-FrameGenBackend([string]$key) { if ($key -eq 'fsr') { $fsrAvailable } else { $frameGenAvailable } }
# hotkeys.ini next to the emulator (src/local/controller-hotkeys.h): the emulator writes it, the game changes its hold
# button (Modifier) too, the launcher shows and edits it.
$hotkeysPath = Join-Path (Split-Path $exe) 'hotkeys.ini'
# A button name as hotkeys.ini may hold it (also SDL's and other spellings) as the launcher's; '' for none.
function ConvertTo-HotkeyName([string]$value) {
	$key = ($value -replace '[\s_/-]', '').ToLowerInvariant()
	if (!$key) { return '' }
	$aliases = @{ a = 'Cross'; b = 'Circle'; x = 'Square'; y = 'Triangle'; back = 'Create'; share = 'Create'; select = 'Create'
		view = 'Create'; guide = 'PS'; home = 'PS'; start = 'Options'; menu = 'Options'; leftstick = 'L3'; rightstick = 'R3'
		leftshoulder = 'L1'; lb = 'L1'; rightshoulder = 'R1'; rb = 'R1'; dpup = 'Up'; dpdown = 'Down'; dpleft = 'Left'
		dpright = 'Right'; misc1 = 'Mute' }
	if ($aliases.ContainsKey($key)) { return $aliases[$key] }
	foreach ($name in $hotkeyButtonInfo.Keys) { if ($name.ToLowerInvariant() -eq $key) { return $name } }
	''
}
# The bindings: the file's, the defaults where it has none.
function Read-Hotkeys {
	$map = @{}
	foreach ($key in $hotkeyDefaults.Keys) { $map[$key] = $hotkeyDefaults[$key] }
	if (Test-Path -LiteralPath $hotkeysPath) {
		foreach ($line in Get-Content -LiteralPath $hotkeysPath) {
			if ($line -match '^\s*([A-Za-z]+)\s*=\s*([^#;]*)') {
				$known = @($map.Keys) | Where-Object { $_ -ieq $Matches[1] } | Select-Object -First 1
				if ($known -eq 'QuitChord') {
					$map.QuitChord = (@($Matches[2].Split('+') | ForEach-Object { ConvertTo-HotkeyName $_.Trim() } | Where-Object { $_ }) |
						Select-Object -First 2) -join '+'
				} elseif ($known) { $map[$known] = ConvertTo-HotkeyName $Matches[2].Trim() }
			}
		}
	}
	if (!$map.Modifier) { $map.Modifier = 'Create' }
	$map
}
function Get-HotkeyButton { (Read-Hotkeys).Modifier }
# Writes all of them (as the emulator does); a hotkey on the hold button itself is left unbound.
function Save-Hotkeys($map) {
	try {
		$lines = @('# KytyPS5 controller hotkeys: hold Modifier, then press a button. Empty: unbound.',
			'# In the game, Modifier + R3 then a button changes Modifier (saved here).',
			'# QuitChord: one button (held 2 s) or two (held together 1 s) that quit the game; empty: none.',
			"Modifier=$($map.Modifier)", "QuitChord=$($map.QuitChord)")
		foreach ($key in $hotkeyActions.Keys) {
			$button = if ($map[$key] -eq $map.Modifier) { '' } else { $map[$key] }
			$lines += "$key=$button"
		}
		Set-Content -LiteralPath $hotkeysPath -Value $lines -Encoding ASCII
	} catch {}
}
# A button by its PlayStation name, with the Xbox one: 'Cross (A)'.
$xboxNames = @{ Create = 'View'; Options = 'Menu'; Cross = 'A'; Circle = 'B'; Square = 'X'; Triangle = 'Y'; L1 = 'LB'; R1 = 'RB'; PS = 'Guide' }
function Get-ButtonText([string]$name) { if ($xboxNames[$name]) { "$name ($($xboxNames[$name]))" } else { $name } }
# The recorded quit shortcut in words.
function Get-QuitChordText([string]$chord) {
	$buttons = @($chord.Split('+') | Where-Object { $_ } | ForEach-Object { Get-ButtonText $_ })
	if ($buttons.Count -ge 2) { "hold $($buttons[0]) + $($buttons[1]) together for 1 second" }
	elseif ($buttons.Count -eq 1) { "hold $($buttons[0]) for 2 seconds" } else { '' }
}
# The hotkeys in words, with the buttons chosen.
function Get-HotkeySummary($map) {
	$hold = $map.Modifier
	$parts = @()
	foreach ($key in $hotkeyActions.Keys) {
		if ($map[$key] -and $map[$key] -ne $hold) { $parts += "  $hold + $($map[$key]):  $($hotkeyActions[$key])" }
	}
	$chord = if ($map.QuitChord) { "`nQuit shortcut: " + (Get-QuitChordText $map.QuitChord) } else { '' }
	"Hold $hold, then press:`n" + ($parts -join "`n") + $chord
}

function Get-PlayCommand {
	$size = $settings.resolution -split 'x'
	$arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$root\run-windows.ps1`"", '-Prompt', '-Follow',
		'-Width', $size[0], '-Height', $size[1], '-Language', $settings.language)
	if ($settings.fullscreen) { $arguments += '-Fullscreen'; if ($settings.aspect) { $arguments += '-AspectFit' } }
	if (!$settings.redzone) { $arguments += '-NoRedZone' }
	if ($settings.fps120) { $arguments += '-Fps120' }
	# Presents wait for the display's refresh (Vulkan FIFO): no tearing, and G-SYNC/FreeSync displays keep adapting.
	if ($settings.vsync) { $arguments += @('-PresentMode', 'Fifo') }
	# HDR10 output when Windows HDR is on for the display; SDR otherwise.
	if ($settings.hdr) { $arguments += '-Hdr' }
	$dlss = $frameGenAvailable -and $settings.upscaler -eq 'dlss'
	$fsr  = $fsrAvailable -and $settings.upscaler -eq 'fsr'
	$pro  = $dlss -or $fsr
	if ($settings.upscale_quality -in $presetKeys -and $settings.upscale_quality) {
		# A quality preset: the render height as its ratio of the 2160 output.
		$arguments += @('-UpscaleQuality', $settings.upscale_quality)
	} else {
		# The upscaler's own render height (on the PS5 Pro path always passed: its own height is 1512).
		$height = if ($pro) { [int]$settings.render_dlss } else { [int]$settings.render }
		if ($height -gt 0 -and ($height -ne 1440 -or $pro)) { $arguments += @('-RenderHeight', $height) }
	}
	if ($settings.ratio -in '21:9', '32:9') { $arguments += @('-Aspect', $settings.ratio) }
	if ($settings.quality -in 'low', 'lowest') {
		$arguments += @('-Quality', $settings.quality, '-TextureReduce', $(if ($settings.quality -eq 'lowest') { 2 } else { 1 }))
	}
	$backend   = Get-FrameGenBackend
	$dlssFrames = $backend -eq 'dlss' -and $frameGenAvailable -and [int]$settings.framegen -ge 2
	if ($dlssFrames) { $arguments += @('-FrameGen', ([int]$settings.framegen - 1)) }
	# (FSR 3 makes one generated frame per rendered one.)
	if ($backend -eq 'fsr' -and $fsrAvailable -and [int]$settings.framegen -ge 2) { $arguments += @('-FrameGen', 1, '-FrameGenBackend', 'fsr') }
	if ($dlss) { $arguments += @('-Upscaler', 'dlss') }
	if ($fsr) { $arguments += @('-Upscaler', 'fsr', '-Sharpness', ([string][double]$settings.fsr_sharpness).Replace(',', '.')) }
	if ($dlssFrames -or $dlss) { $arguments += @('-StreamlineDir', "`"$streamline`"") }
	# Switches of the config, one -Set argument (run-windows.ps1 splits it at the commas).
	$sets = @()
	# The emulator's panel (KYTY_FPS_HUD) at the top right of the game image: the frame rate and frame times, or with
	# the GPU, processor and memory too.
	if ([int]$settings.hud -ge 1) { $sets += "KYTY_FPS_HUD=$([Math]::Min([int]$settings.hud, 2))" }
	# Sharp HUD with frame generation: the frames' image without the game's HUD goes to DLSS-G or FSR 3 as well
	# (KYTY_FG_HUDLESS), so the HUD is not interpolated with the scene behind it (smeared, ghosting).
	if ($settings.fg_hudless -and [int]$settings.framegen -ge 2) { $sets += 'KYTY_FG_HUDLESS=1' }
	# Safe GPU mode: no overlapping compute dispatches ($safeGpuTip).
	if ($settings.safegpu) { $sets += 'KYTY_COMPUTE_CHAIN=0' }
	# Vibration for controllers other than a DualSense on USB: the game's haptics as rumble, this strong
	# (KYTY_HAPTICS_RUMBLE; Normal is the emulator's default).
	$rumbleIndex = [Math]::Max(0, [Math]::Min($rumbleStrengths.Count - 1, [int]$settings.rumble))
	if ($rumbleIndex -ne 2) { $sets += "KYTY_HAPTICS_RUMBLE=$($rumbleStrengths[$rumbleIndex])" }
	if ($sets.Count) { $arguments += @('-Set', ($sets -join ',')) }
	if ($settings.game) { $arguments += @('-Game', "`"$($settings.game)`"") }
	# The console stays for the live log; after a crash it waits for a key.
	return "$powershellCommand " + ($arguments -join ' ') + ' & if !errorlevel! neq 0 pause'
}
$rumbleStrengths = @('0', '0.5', '1', '2', '4')
function Get-PrecompileCommand([string]$switch = '') {
	$arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$root\precompile-windows.ps1`"")
	if ($switch) { $arguments += $switch }
	if ($settings.ecores -and $efficiencyMask -ne 0) { $arguments += @('-Affinity', $efficiencyMask) }
	if ($settings.game) { $arguments += @('-Game', "`"$($settings.game)`"") }
	return "$powershellCommand " + ($arguments -join ' ') + ' & pause'
}
function Save-Settings { $settings | ConvertTo-Json | Set-Content $settingsPath -Encoding UTF8 }
if ($DryRun) {
	"play:       cmd /v:on /s /c `"$(Get-PlayCommand)`""
	"precompile: cmd /s /c `"$(Get-PrecompileCommand)`""
	"hints:      cmd /s /c `"$(Get-PrecompileCommand '-ExportHints')`""
	"efficiency cores: 0x{0:X}" -f $efficiencyMask
	"fsr: $(if ($fsrAvailable) { 'AMD FidelityFX runtime next to the emulator' } else { 'amd_fidelityfx_vk.dll missing (FSR off)' }); hotkey button: $(Get-HotkeyButton) ($hotkeysPath)"
	"frame generation: $(if ($frameGenAvailable) { "NVIDIA's Streamline runtime in $streamline" } elseif (!$rtxGpu) { 'no NVIDIA RTX graphics card (DLSS off)' } else { "no NVIDIA-signed Streamline runtime (settings: '$($settings.streamline)')" })"
	return
}

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -Namespace KytyLauncher -Name Dpi -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
[KytyLauncher.Dpi]::SetProcessDPIAware() | Out-Null
# Gamepads through XInput (Xbox-style controllers, and others through Steam Input or DS4Windows): the launcher's
# navigation and the quit shortcut's recorder.
Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
namespace KytyLauncher {
public static class Pad {
	[StructLayout(LayoutKind.Sequential)]
	public struct State { public uint Packet; public ushort Buttons; public byte LeftTrigger, RightTrigger; public short LX, LY, RX, RY; }
	[DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")] static extern uint Get14(uint index, out State state);
	[DllImport("xinput9_1_0.dll", EntryPoint = "XInputGetState")] static extern uint Get910(uint index, out State state);
	[DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
	[DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
	[DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
	static int dll; // 0: xinput1_4, 1: xinput9_1_0, 2: none
	public static bool Available { get { if (dll < 2) { Read(false); } return dll < 2; } }
	static bool Get(uint index, out State state) {
		state = new State();
		while (dll < 2) {
			try { return (dll == 0 ? Get14(index, out state) : Get910(index, out state)) == 0; }
			catch (Exception) { dll++; } // not there (or not loadable): the next, then none
		}
		return false;
	}
	// The buttons held on every connected pad (XINPUT_GAMEPAD_* bits), with the left stick as the D-pad's when `stick`.
	public static int Read(bool stick) {
		int all = 0;
		for (uint index = 0; index < 4; index++) {
			State state;
			if (!Get(index, out state)) { continue; }
			int buttons = state.Buttons;
			if (stick) {
				if (state.LY > 16000) { buttons |= 1; }
				if (state.LY < -16000) { buttons |= 2; }
				if (state.LX < -16000) { buttons |= 4; }
				if (state.LX > 16000) { buttons |= 8; }
			}
			all |= buttons;
		}
		return all;
	}
	// A window of this process is in front (XInput reads the pads whatever window has the focus).
	public static bool InFront() {
		uint process;
		GetWindowThreadProcessId(GetForegroundWindow(), out process);
		return process == (uint)Process.GetCurrentProcess().Id;
	}
	// Shows the focus rectangle (Windows hides it until the keyboard is used).
	public static void ShowFocus(IntPtr window) { SendMessage(window, 0x127, (IntPtr)0x10002, IntPtr.Zero); }
}
}
'@
# XInput's bits by hotkeys.ini's names (the Guide button is not reported).
$padBits = [ordered]@{ Up = 0x1; Down = 0x2; Left = 0x4; Right = 0x8; Options = 0x10; Create = 0x20; L3 = 0x40; R3 = 0x80
	L1 = 0x100; R1 = 0x200; Cross = 0x1000; Circle = 0x2000; Square = 0x4000; Triangle = 0x8000 }
function ConvertFrom-PadBits([int]$bits) { @($padBits.Keys | Where-Object { $bits -band $padBits[$_] }) }
[System.Windows.Forms.Application]::EnableVisualStyles()

$form = New-Object System.Windows.Forms.Form -Property @{
	Text = 'KytyPS5 launcher'; FormBorderStyle = 'FixedDialog'; MaximizeBox = $false; StartPosition = 'CenterScreen'
	AutoSize = $true; AutoSizeMode = 'GrowAndShrink'; Font = New-Object System.Drawing.Font('Segoe UI', 9); Padding = '12,12,12,8'
}
$grid = New-Object System.Windows.Forms.TableLayoutPanel -Property @{ ColumnCount = 2; AutoSize = $true; Dock = 'Fill' }
$form.Controls.Add($grid)
# A row: a label, then the controls side by side.
function Add-Row($label, [object[]]$controls) {
	$grid.Controls.Add((New-Object System.Windows.Forms.Label -Property @{ Text = $label; AutoSize = $true; Anchor = 'Left'; Margin = '0,6,8,6' }))
	$row = New-Object System.Windows.Forms.FlowLayoutPanel -Property @{ AutoSize = $true; WrapContents = $false; Margin = '0,0,0,0' }
	$row.Controls.AddRange($controls)
	$grid.Controls.Add($row)
}

$game = New-Object System.Windows.Forms.TextBox -Property @{ Text = $settings.game; Width = 440 }
$browse = New-Object System.Windows.Forms.Button -Property @{ Text = 'Browse...'; AutoSize = $true }
$browseZar = New-Object System.Windows.Forms.Button -Property @{ Text = '.zar...'; AutoSize = $true }
Add-Row 'Game folder' @($game, $browse, $browseZar)
$title = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Gray' }
Add-Row '' @($title)
# A game: its folder (eboot.bin and sce_sys) or the folder packed into a ZArchive (a .zar file).
function Read-GameParam([string]$game) {
	if (!$game) { return $null }
	if (Test-Path -LiteralPath "$game\sce_sys\param.json") { return Get-Content -LiteralPath "$game\sce_sys\param.json" -Raw -Encoding UTF8 | ConvertFrom-Json }
	$tool = @("$root\kyty_shader_precompile.exe", "$root\_Build\windows\kyty_shader_precompile.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
	if ($game -notmatch '\.zar$' -or !(Test-Path -LiteralPath $game -PathType Leaf) -or !$tool) { return $null }
	$copy = [IO.Path]::GetTempFileName()
	try {
		& $tool --game $game --param $copy 2>$null | Out-Null
		if ($LASTEXITCODE -eq 0) { return Get-Content -LiteralPath $copy -Raw -Encoding UTF8 | ConvertFrom-Json }
	} catch {
	} finally {
		Remove-Item -LiteralPath $copy -ErrorAction SilentlyContinue
	}
	return $null
}
function Update-Title {
	$title.Text = 'Choose the folder with eboot.bin and sce_sys (the game dump), or a .zar archive of it.'
	try {
		$json = Read-GameParam $game.Text.Trim()
		if ($json) {
			$title.Text = '{0} ({1} {2})' -f $json.localizedParameters.($json.localizedParameters.defaultLanguage).titleName,
				$json.titleId, $json.contentVersion
		}
	} catch {}
}
$browse.Add_Click({
	$dialog = New-Object System.Windows.Forms.FolderBrowserDialog -Property @{ Description = 'The game folder (eboot.bin, sce_sys)' }
	if ($game.Text -and (Test-Path -LiteralPath $game.Text -PathType Container)) { $dialog.SelectedPath = $game.Text }
	if ($dialog.ShowDialog($form) -eq 'OK') { $game.Text = $dialog.SelectedPath }
})
# The game folder packed into a ZArchive (zarchive.exe <folder> <file.zar>): read without extracting it.
$browseZar.Add_Click({
	$dialog = New-Object System.Windows.Forms.OpenFileDialog -Property @{ Title = 'The game folder packed into a ZArchive'; Filter = 'ZArchive (*.zar)|*.zar' }
	if ($game.Text -and (Test-Path -LiteralPath $game.Text -PathType Leaf)) { $dialog.InitialDirectory = Split-Path $game.Text }
	if ($dialog.ShowDialog($form) -eq 'OK') { $game.Text = $dialog.FileName }
})
$game.Add_TextChanged({ Update-Title })
Update-Title

# The picture's size, shape and detail, side by side: the aspect ratio offers the resolutions of its shape (a 21:9
# picture in a 3840x2160 window was stretched back to 16:9) and the render heights whose 3D the game holds (no larger
# than its own 3840x2160 in GPU tiles: 21:9 at 2160 ended it at start-up, 1864 drew it red; src/loader/demonsSoulsRenderSize.h).
$ratio = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 205 }
$ratio.Items.AddRange($ratios)
$ratio.SelectedItem = ($ratios | Where-Object { ($_ -split ' ')[0] -eq [string]$settings.ratio } | Select-Object -First 1)
if ($ratio.SelectedIndex -lt 0) { $ratio.SelectedIndex = 0 }
$ratioNote = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Gray'; Margin = '6,6,0,0'
	Text = '21:9 and 32:9 show more to the sides; the HUD keeps its shape, menus and movies are stretched' }
function Get-Ratio { ([string]$ratio.SelectedItem -split ' ')[0] }
function Get-RatioAspect { switch (Get-Ratio) { '21:9' { 64.0 / 27.0 } '32:9' { 32.0 / 9.0 } default { 16.0 / 9.0 } } }
$resolution = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 205 }
# The ratio's resolutions; `wanted` kept when it is one of them, else the one nearest its height.
function Update-Resolutions([string]$wanted) {
	$list = $resolutionsByRatio[(Get-Ratio)]
	$resolution.Items.Clear()
	$resolution.Items.AddRange($list)
	$height = [int](($wanted -split 'x')[1])
	$resolution.SelectedItem = if ($list -contains $wanted) { $wanted } else {
		$list | Sort-Object { [Math]::Abs([int](($_ -split 'x')[1]) - $height) } | Select-Object -First 1
	}
}
Update-Resolutions $settings.resolution
$render = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 205 }
# The highest render height whose 3D at the ratio fits the game's render target memory, made for 3840x2160: no more of
# the GPU's 64 KiB tiles (256x128, 128x128, 128x64, 64x64 pixels) than 3840x2160 (the emulator's FitsRenderTargets).
function Get-MaxRenderHeight {
	$aspectValue = Get-RatioAspect
	for ($h = 2160; $h -ge 720; $h -= 8) {
		$w = [Math]::Floor($h * $aspectValue / 8.0 + 0.5) * 8
		$fits = $true
		foreach ($tile in @(@(256, 128), @(128, 128), @(128, 64), @(64, 64))) {
			$tiles = [Math]::Ceiling($w / $tile[0]) * [Math]::Ceiling($h / $tile[1])
			if ($tiles -gt [Math]::Ceiling(3840 / $tile[0]) * [Math]::Ceiling(2160 / $tile[1])) { $fits = $false }
		}
		if ($fits) { return $h }
	}
	720
}
# Each upscaler keeps its own render height (settings render, render_dlss): the row shows the height last chosen for the
# chosen upscaler ($renderWanted) among those the ratio allows; one above them shows as the highest allowed (and comes
# back with a ratio that allows it), one not in the list as the upscaler's default.
function Get-RenderFallback { if ($script:renderForDlss) { 1080 } else { 1440 } }
function Select-RenderHeight {
	$most = Get-MaxRenderHeight
	$allowed = @($renderHeights | Where-Object { [int](($_ -split ' ')[0]) -le $most })
	$script:fillingRender = $true
	try {
		$render.Items.Clear()
		$render.Items.AddRange($allowed)
		if ($script:renderWanted -gt $most) { $render.SelectedIndex = 0; return }
		foreach ($wanted in @($script:renderWanted, (Get-RenderFallback))) {
			$render.SelectedItem = ($allowed | Where-Object { ($_ -split ' ')[0] -eq [string]$wanted } | Select-Object -First 1)
			if ($render.SelectedIndex -ge 0) { return }
		}
		$render.SelectedIndex = 0
	} finally {
		$script:fillingRender = $false
	}
}
$renderNote = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Gray'; Margin = '6,6,0,0' }
function Update-RenderNote {
	if ($preset -and $preset.SelectedIndex -gt 0) { $renderNote.Text = 'set by Upscale quality above'; return }
	$h = [int](([string]$render.SelectedItem -split ' ')[0])
	$w = [Math]::Floor($h * (Get-RatioAspect) / 8.0 + 0.5) * 8
	$renderNote.Text = "Demon's Souls' 3D at ${w}x${h}; higher = sharper, needs more GPU"
}
$render.Add_SelectedIndexChanged({
	if (!$script:fillingRender) { $script:renderWanted = [int](([string]$render.SelectedItem -split ' ')[0]) }
	Update-RenderNote
})
# (Both upscalers run on the game's PS5 Pro path and share its render height: render_dlss.)
$renderForDlss = $settings.upscaler -in 'dlss', 'fsr' -and (Test-Upscaler $settings.upscaler)
$renderWanted = [int]$(if ($renderForDlss) { $settings.render_dlss } else { $settings.render })
Select-RenderHeight
Update-RenderNote
$ratio.Add_SelectedIndexChanged({
	Update-Resolutions ([string]$resolution.SelectedItem)
	Select-RenderHeight
	Update-RenderNote
	Update-Preset
})
$upscaler = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 205; Enabled = ($frameGenAvailable -or $fsrAvailable) }
$upscaler.Items.AddRange($upscalers)
$upscalerIndex = [Array]::IndexOf($upscalerKeys, [string]$settings.upscaler)
$upscaler.SelectedIndex = if ($upscalerIndex -gt 0 -and (Test-Upscaler $upscalerKeys[$upscalerIndex])) { $upscalerIndex } else { 0 }
$upscalerIndex = $upscaler.SelectedIndex
$upscalerNote = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Gray'; Margin = '6,6,0,0' }
# FSR's sharpening (run-windows.ps1 -Sharpness; the controller hotkeys change it in the game).
$sharpnessLabel = New-Object System.Windows.Forms.Label -Property @{ Text = 'Sharpness'; AutoSize = $true; Margin = '6,6,0,0' }
$sharpness = New-Object System.Windows.Forms.NumericUpDown -Property @{ Minimum = 0; Maximum = 1; DecimalPlaces = 2; Increment = 0.05; Width = 60 }
$sharpness.Value = [decimal][Math]::Min(1.0, [Math]::Max(0.0, [double]$settings.fsr_sharpness))
function Update-UpscalerNote {
	$key = $upscalerKeys[[Math]::Max(0, $upscaler.SelectedIndex)]
	$sharpness.Enabled = $key -eq 'fsr'
	$upscalerNote.Text = switch ($key) {
		'dlss' { 'DLSS: NVIDIA RTX' }
		'fsr' { 'FSR 3.1: any GPU (FSR 4 has no Vulkan version)' }
		default {
			if ($fsrAvailable) { 'DLSS or FSR 3.1 in place of the game''s TAA: a sharper 4K image' }
			elseif ($frameGenAvailable) { 'FSR needs amd_fidelityfx_vk.dll next to the emulator' }
			elseif (!$rtxGpu) { 'FSR needs amd_fidelityfx_vk.dll next to the emulator; DLSS an NVIDIA RTX card' }
			else { 'DLSS needs NVIDIA''s Streamline SDK (choose its folder under Frame generation)' }
		}
	}
}
Update-UpscalerNote
(New-Object System.Windows.Forms.ToolTip -Property @{ AutoPopDelay = 30000 }).SetToolTip($upscaler,
	"Game: the PS5's performance mode, its 3D anti-aliased by the game's TAA at the render height and stretched to 4K.`n" +
	"DLSS: the game's own PS5 Pro mode, Sony's upscaler (PSSR) replaced by NVIDIA DLSS: the 3D at the render height (2688x1512`n" +
	"at 1512, the PS5 Pro's; 1080 at least) made a 3840x2160 image the game finishes at 4K (bloom, depth of field, the HUD).`n" +
	"Sharper than the stretch, at some GPU cost. The in-game Cinematic mode stays native 4K without DLSS.`n" +
	"Each upscaler keeps its own render height. DLSS starts at 1080: on an RTX 5090 about the frame rate of the game's TAA`n" +
	"at 1440, with a sharper 4K image.")
$upscaler.Add_SelectedIndexChanged({
	$key = $upscalerKeys[[Math]::Max(0, $upscaler.SelectedIndex)]
	if (!(Test-Upscaler $key)) {
		$why = if ($key -eq 'fsr') { "AMD's amd_fidelityfx_vk.dll is not next to the emulator (the build copies it from 3rdparty\fidelityfx\bin)." }
			else { 'DLSS needs an NVIDIA RTX graphics card and NVIDIA''s Streamline SDK (its folder under Frame generation).' }
		[void][System.Windows.Forms.MessageBox]::Show($form, $why, 'Upscaler')
		$upscaler.SelectedIndex = $script:upscalerIndex
		return
	}
	$script:upscalerIndex = $upscaler.SelectedIndex
	# The height chosen goes back to the path it was for (the game's, or the PS5 Pro path's); the newly chosen one's is shown.
	$pro = $upscaler.SelectedIndex -ge 1
	if ($pro -ne $script:renderForDlss) {
		if ($script:renderForDlss) { $settings.render_dlss = $script:renderWanted } else { $settings.render = $script:renderWanted }
		$script:renderForDlss = $pro
		$script:renderWanted = [int]$(if ($script:renderForDlss) { $settings.render_dlss } else { $settings.render })
		Select-RenderHeight
	}
	Update-RenderNote
	Update-UpscalerNote
	Update-Preset
})
# The render height from a quality preset: 2160 divided by its ratio (a multiple of 8), no more than the ratio's 3D holds,
# and 1080 at least on the PS5 Pro path (the game's upscaler is off below).
$preset = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 205 }
$preset.Items.AddRange($presets)
$preset.SelectedIndex = [Math]::Max(0, [Array]::IndexOf($presetKeys, [string]$settings.upscale_quality))
$presetNote = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Gray'; Margin = '6,6,0,0' }
function Update-Preset {
	$custom = $preset.SelectedIndex -le 0
	$render.Enabled = $custom
	if ($custom) {
		$presetNote.Text = 'the Render height row decides'
	} else {
		$h = [int]([Math]::Round(2160.0 / $presetRatios[$preset.SelectedIndex] / 8.0)) * 8
		$note = ''
		if ($upscaler.SelectedIndex -ge 1 -and $h -lt 1080) { $h = 1080; $note = ' (the PS5 Pro path stops below 1080: Performance)' }
		$most = Get-MaxRenderHeight
		if ($h -gt $most) { $h = $most; $note = ' (the most this aspect ratio holds)' }
		$w = [Math]::Floor($h * (Get-RatioAspect) / 8.0 + 0.5) * 8
		$presetNote.Text = "3D at ${w}x${h} of the 4K output$note"
	}
	Update-RenderNote
}
$preset.Add_SelectedIndexChanged({ Update-Preset })
(New-Object System.Windows.Forms.ToolTip -Property @{ AutoPopDelay = 30000 }).SetToolTip($preset,
	"The 3D's render height as a share of the game's 2160-line 4K output (FSR's quality modes; DLSS takes the same):`n" +
	"Native AA 2160, Ultra Quality 1664, Quality 1440, Balanced 1272, Performance 1080, Ultra Performance 720.`n" +
	"With DLSS or FSR (the game's PS5 Pro path) the game turns its upscaler off below 1080, so Ultra Performance runs as Performance.`n" +
	"Custom: the Render height row's own height.")
Add-Row 'Resolution' @($resolution)
Add-Row 'Aspect ratio' @($ratio, $ratioNote)
Add-Row 'Upscaler' @($upscaler, $sharpnessLabel, $sharpness, $upscalerNote)
Add-Row 'Upscale quality' @($preset, $presetNote)
Add-Row 'Render height' @($render, $renderNote)
Update-Preset
$fullscreen = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'Fullscreen'; AutoSize = $true; Checked = [bool]$settings.fullscreen }
$aspect = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'Keep the aspect ratio (black bars)'; AutoSize = $true; Checked = [bool]$settings.aspect }
$aspect.Enabled = $fullscreen.Checked
$fullscreen.Add_CheckedChanged({ $aspect.Enabled = $fullscreen.Checked })
Add-Row '' @($fullscreen, $aspect)
$language = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 320 }
$language.Items.AddRange($languages)
$language.SelectedIndex = [Math]::Max(0, [Math]::Min($languages.Count - 1, [int]$settings.language))
Add-Row 'Console language' @($language)
$redzone = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'Red-zone protection (recommended)'; AutoSize = $true; Checked = [bool]$settings.redzone }
Add-Row '' @($redzone)
$fps120 = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'Up to 120 fps (the game''s own frames; movies stay at 60)'; AutoSize = $true; Checked = [bool]$settings.fps120 }
Add-Row '' @($fps120)
$vsync = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'VSync (no screen tearing; adaptive sync displays keep it)'; AutoSize = $true; Checked = [bool]$settings.vsync }
Add-Row '' @($vsync)
$hdr = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'HDR (turn on "Use HDR" in Windows display settings first)'; AutoSize = $true; Checked = [bool]$settings.hdr }
Add-Row '' @($hdr)
$safeGpu = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'Safe GPU mode: prevents GPU-hang crashes and blue screens, but the frame rate is about 37% lower'; AutoSize = $true; Checked = [bool]$settings.safegpu }
(New-Object System.Windows.Forms.ToolTip -Property @{ AutoPopDelay = 30000 }).SetToolTip($safeGpu, $safeGpuTip)
Add-Row '' @($safeGpu)
$quality = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 205 }
$quality.Items.AddRange($qualities)
$quality.SelectedItem = ($qualities | Where-Object { ($_ -split ' ')[0] -eq [string]$settings.quality } | Select-Object -First 1)
if ($quality.SelectedIndex -lt 0) { $quality.SelectedIndex = 0 }
$qualityNote = New-Object System.Windows.Forms.Label -Property @{ Text = 'shorter shadows, simpler detail, smaller textures; faster on weaker PCs'; AutoSize = $true; ForeColor = 'Gray'; Margin = '6,6,0,0' }
$qualityTips = New-Object System.Windows.Forms.ToolTip
$qualityTips.SetToolTip($quality, "Standard: the PS5's picture. With less than 12 GB of video memory, large textures leave out their largest level`n(half the size per side) so that they fit.`nLow: shorter sun shadows, simpler distant detail, textures half the size per side.`nLowest: shorter still, no ambient shadowing or torch shadows, textures a quarter the size per side.")
Add-Row 'Graphics' @($quality, $qualityNote)
$frameGenBackend = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 170 }
$frameGenBackend.Items.AddRange($frameGenBackends)
$frameGenBackend.SelectedIndex = [Math]::Max(0, [Array]::IndexOf($frameGenBackendKeys, (Get-FrameGenBackend)))
$frameGen = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 70 }
$frameGen.Items.AddRange($frameGens)
$frameGen.SelectedItem = if ((Test-FrameGenBackend (Get-FrameGenBackend)) -and [int]$settings.framegen -ge 2) { "$([int]$settings.framegen)x" } else { 'Off' }
if ($frameGen.SelectedIndex -lt 0) { $frameGen.SelectedIndex = 0 }
$frameGenFolder = New-Object System.Windows.Forms.Button -Property @{ Text = 'Streamline...'; AutoSize = $true }
$frameGenNote = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Gray'; Margin = '6,6,0,0' }
function Get-FrameGenBackendKey { $frameGenBackendKeys[[Math]::Max(0, $frameGenBackend.SelectedIndex)] }
function Update-FrameGenNote {
	$frameGenNote.Text = if ((Get-FrameGenBackendKey) -eq 'fsr') {
		if ($fsrAvailable) { 'FSR 3: any GPU, 2x; smoothest from about 60 rendered fps' } else { 'FSR 3 needs amd_fidelityfx_vk.dll next to the emulator' }
	} elseif ($frameGenAvailable) { 'DLSS; NVIDIA RTX 40 or newer (3x and up: RTX 50)' }
		elseif (!$rtxGpu) { 'DLSS: needs an NVIDIA RTX 40 or newer graphics card' }
		else { 'needs NVIDIA''s Streamline SDK: download it, then choose its folder' }
}
# The backend's multipliers (FSR 3: 2x only) and whether it can run here.
$fgHudless = New-Object System.Windows.Forms.CheckBox -Property @{ AutoSize = $true; Checked = [bool]$settings.fg_hudless
	Text = 'Sharp HUD with frame generation (no smearing or ghosting of health bars and menus)' }
(New-Object System.Windows.Forms.ToolTip -Property @{ AutoPopDelay = 30000 }).SetToolTip($fgHudless,
	"Each frame's image without the game's HUD goes to frame generation too, so the HUD stays sharp`n" +
	"instead of being blended between frames with the scene behind it (KYTY_FG_HUDLESS).")
function Update-FrameGenState {
	$key = Get-FrameGenBackendKey
	$wanted = [string]$frameGen.SelectedItem
	$frameGen.Items.Clear()
	$frameGen.Items.AddRange($(if ($key -eq 'fsr') { @('Off', '2x') } else { $frameGens }))
	$frameGen.SelectedItem = if ($frameGen.Items.Contains($wanted)) { $wanted } elseif ($wanted -and $wanted -ne 'Off') { '2x' } else { 'Off' }
	if ($frameGen.SelectedIndex -lt 0) { $frameGen.SelectedIndex = 0 }
	$frameGen.Enabled = Test-FrameGenBackend $key
	$fgHudless.Enabled = $frameGen.Enabled
	$frameGenFolder.Visible = $key -eq 'dlss'
	Update-FrameGenNote
}
$frameGenBackend.Add_SelectedIndexChanged({ Update-FrameGenState })
Update-FrameGenState
$frameGenTips = New-Object System.Windows.Forms.ToolTip
$frameGenTips.SetToolTip($frameGen, "Shown frames = the game's frames x the multiplier. With VSync on, they are capped at the display's refresh`n(a 60 Hz display at 2x runs the game at 30 fps). The game's frames stay at 60 with 'Up to 120 fps' too:`nframe generation needs them evenly spaced.")
$frameGenTips.SetToolTip($frameGenFolder, "The NVIDIA Streamline SDK (a release zip from $streamlineSdkUrl), unpacked: its folder or its bin\x64.`nOnly NVIDIA-signed files are used; nothing of NVIDIA's comes with the emulator.")
$frameGenFolder.Add_Click({
	$dialog = New-Object System.Windows.Forms.FolderBrowserDialog -Property @{
		Description = "The unpacked NVIDIA Streamline SDK ($streamlineSdkUrl), or its bin\x64 folder" }
	if ($settings.streamline -and (Test-Path -LiteralPath $settings.streamline)) { $dialog.SelectedPath = $settings.streamline }
	if ($dialog.ShowDialog($form) -ne 'OK') { return }
	$found = Find-Streamline $dialog.SelectedPath
	if (!$found) {
		[void][System.Windows.Forms.MessageBox]::Show($form, "No sl.interposer.dll signed by NVIDIA in that folder or its bin\x64.`nDownload the Streamline SDK from $streamlineSdkUrl and choose the unpacked folder.", 'Frame generation')
		return
	}
	$settings.streamline = $dialog.SelectedPath
	$script:streamline = $found
	# (Without an NVIDIA RTX graphics card the folder is kept, the rows stay off.)
	$script:frameGenAvailable = $rtxGpu
	$upscaler.Enabled = $rtxGpu -or $fsrAvailable
	Update-FrameGenState
	Update-UpscalerNote
})
Add-Row 'Frame generation' @($frameGenBackend, $frameGen, $frameGenFolder, $frameGenNote)
Add-Row '' @($fgHudless)
# Controller hotkeys: the hold button here, every hotkey in "Hotkeys..." (both kept in hotkeys.ini next to the emulator).
$script:hotkeyMap = Read-Hotkeys
$hotkey = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 290 }
function Set-HotkeyCombo([string]$selected) {
	$hotkey.Items.Clear()
	$names = @($hotkeyButtons)
	if ($selected -and $names -notcontains $selected) { $names += $selected }
	foreach ($name in $names) { [void]$hotkey.Items.Add($hotkeyButtonInfo[$name]) }
	$hotkey.SelectedItem = $hotkeyButtonInfo[$selected]
	if ($hotkey.SelectedIndex -lt 0) { $hotkey.SelectedIndex = 0 }
}
# The hold button chosen in the list (its name: the first word).
function Get-HotkeyComboButton { Get-HotkeyNameFromText ([string]$hotkey.SelectedItem) }
Set-HotkeyCombo $script:hotkeyMap.Modifier
$hotkeyEdit = New-Object System.Windows.Forms.Button -Property @{ Text = 'Hotkeys...'; AutoSize = $true }
$hotkeyNote = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Gray'; Margin = '0,2,0,6' }
$hotkeyTips = New-Object System.Windows.Forms.ToolTip -Property @{ AutoPopDelay = 30000 }
function Update-HotkeyNote {
	$map = $script:hotkeyMap
	$hold = $map.Modifier
	$quit = if ($map.QuitChord) { "To quit the game: $(Get-QuitChordText $map.QuitChord)." }
		elseif ($map.Quit -and $map.Quit -ne $hold) { "To quit the game: hold $hold, then also press $($map.Quit) for 1 second." }
		else { 'Quit has no button (Hotkeys...).' }
	$help = if ($map.Help -and $map.Help -ne $hold) { " $hold + $($map.Help) lists every hotkey on screen." } else { '' }
	$hotkeyNote.Text = $quit + $help
	$hotkeyTips.SetToolTip($hotkey, (Get-HotkeySummary $map))
	$hotkeyTips.SetToolTip($hotkeyNote, (Get-HotkeySummary $map))
	Set-QuitModeCombo
}
$hotkey.Add_SelectedIndexChanged({
	$script:hotkeyMap.Modifier = Get-HotkeyComboButton
	Update-HotkeyNote
})
# Every hotkey's button, in a window of its own.
$hotkeyEdit.Add_Click({
	$dialog = New-Object System.Windows.Forms.Form -Property @{ Text = 'Controller hotkeys'; FormBorderStyle = 'FixedDialog'
		MaximizeBox = $false; MinimizeBox = $false; StartPosition = 'CenterParent'; AutoSize = $true; AutoSizeMode = 'GrowAndShrink'
		Font = $form.Font; Padding = '12,12,12,8' }
	$table = New-Object System.Windows.Forms.TableLayoutPanel -Property @{ ColumnCount = 2; AutoSize = $true; Dock = 'Fill' }
	$dialog.Controls.Add($table)
	$intro = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; Margin = '0,0,0,8'; MaximumSize = '520,0'
		Text = 'Hold the hold button, then press the button of a hotkey. While the hold button is down, the buttons you press do not reach the game.' }
	$table.Controls.Add($intro); $table.SetColumnSpan($intro, 2)
	$choices = @('(none)') + @($hotkeyButtonInfo.Values)
	$combos = @{}
	foreach ($key in @('Modifier') + @($hotkeyActions.Keys)) {
		$label = if ($key -eq 'Modifier') { 'Hold button' } else { $hotkeyActions[$key] }
		$table.Controls.Add((New-Object System.Windows.Forms.Label -Property @{ Text = $label; AutoSize = $true; Anchor = 'Left'; Margin = '0,6,12,6' }))
		$combo = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 290 }
		$combo.Items.AddRange($(if ($key -eq 'Modifier') { @($hotkeyButtonInfo.Values) } else { $choices }))
		$current = $script:hotkeyMap[$key]
		$combo.SelectedItem = if ($current) { $hotkeyButtonInfo[$current] } else { '(none)' }
		if ($combo.SelectedIndex -lt 0) { $combo.SelectedIndex = 0 }
		$table.Controls.Add($combo)
		$combos[$key] = $combo
	}
	$buttons = New-Object System.Windows.Forms.FlowLayoutPanel -Property @{ AutoSize = $true; Margin = '0,10,0,0' }
	$ok = New-Object System.Windows.Forms.Button -Property @{ Text = 'Save'; AutoSize = $true; DialogResult = 'OK' }
	$cancel = New-Object System.Windows.Forms.Button -Property @{ Text = 'Cancel'; AutoSize = $true; DialogResult = 'Cancel' }
	$defaults = New-Object System.Windows.Forms.Button -Property @{ Text = 'Defaults'; AutoSize = $true }
	$defaults.Add_Click({
		foreach ($key in $combos.Keys) { $combos[$key].SelectedItem = $hotkeyButtonInfo[$hotkeyDefaults[$key]] }
	})
	$buttons.Controls.AddRange(@($ok, $cancel, $defaults))
	$table.Controls.Add($buttons); $table.SetColumnSpan($buttons, 2)
	$dialog.AcceptButton = $ok
	$dialog.CancelButton = $cancel
	if ($dialog.ShowDialog($form) -ne 'OK') { return }
	$map = @{}
	foreach ($key in $combos.Keys) {
		$text = [string]$combos[$key].SelectedItem
		$map[$key] = if ($text -eq '(none)') { '' } else { Get-HotkeyNameFromText $text }
	}
	$map.QuitChord = $script:hotkeyMap.QuitChord
	# (Two hotkeys on one button: the first keeps it.)
	$used = @{}
	foreach ($key in $hotkeyActions.Keys) {
		if (!$map[$key]) { continue }
		if ($map[$key] -eq $map.Modifier -or $used.ContainsKey($map[$key])) {
			[void][System.Windows.Forms.MessageBox]::Show($dialog, "$($map[$key]) is already the hold button or another hotkey's: '$($hotkeyActions[$key])' is left without a button.", 'Controller hotkeys')
			$map[$key] = ''
			continue
		}
		$used[$map[$key]] = $true
	}
	$script:hotkeyMap = $map
	Save-Hotkeys $map
	Set-HotkeyCombo $map.Modifier
	Update-HotkeyNote
})
# Quitting the game: the hold button + the quit hotkey, or buttons recorded here (QuitChord in hotkeys.ini: one button
# held 2 seconds, or two held together 1 second; for controllers without the button the hotkeys use).
$quitMode = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 290 }
$quitRecord = New-Object System.Windows.Forms.Button -Property @{ Text = 'Record...'; AutoSize = $true }
$script:quitModeUpdating = $false
function Set-QuitModeCombo {
	$script:quitModeUpdating = $true
	$map = $script:hotkeyMap
	$quitMode.Items.Clear()
	$first = if ($map.Quit -and $map.Quit -ne $map.Modifier) { "Hold $($map.Modifier) + $($map.Quit) (1 second)" } else { 'The hold button + the quit hotkey (none set)' }
	$custom = if ($map.QuitChord) { 'Custom: ' + (@($map.QuitChord.Split('+') | ForEach-Object { Get-ButtonText $_ }) -join ' + ') } else { 'Custom: record the buttons...' }
	[void]$quitMode.Items.AddRange(@($first, $custom))
	$quitMode.SelectedIndex = if ($map.QuitChord) { 1 } else { 0 }
	$quitRecord.Enabled = [bool]$map.QuitChord
	$script:quitModeUpdating = $false
}
# Saves the quit shortcut ('' for none) with the hold button chosen here.
function Save-QuitChord([string]$chord) {
	$map = Read-Hotkeys
	$map.Modifier = $script:hotkeyMap.Modifier
	$map.QuitChord = $chord
	Save-Hotkeys $map
	$script:hotkeyMap = $map
}
# The recorder: the buttons held together on the controller (1 or 2), or $null when cancelled.
function Show-QuitRecorder {
	if (-not [KytyLauncher.Pad]::Available) {
		[void][System.Windows.Forms.MessageBox]::Show($form, 'No XInput on this PC: the launcher cannot read the controller.', 'Quit shortcut')
		return $null
	}
	$dialog = New-Object System.Windows.Forms.Form -Property @{ Text = 'Record the quit shortcut'; FormBorderStyle = 'FixedDialog'
		MaximizeBox = $false; MinimizeBox = $false; StartPosition = 'CenterParent'; AutoSize = $true; AutoSizeMode = 'GrowAndShrink'
		Font = $form.Font; Padding = '12,12,12,8' }
	$panel = New-Object System.Windows.Forms.FlowLayoutPanel -Property @{ FlowDirection = 'TopDown'; AutoSize = $true; WrapContents = $false }
	$info = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; MaximumSize = '460,0'
		Text = ("On the controller, press one button or two buttons together, then let go.`n`n" +
			"In the game, holding the two together for 1 second quits; one button alone, held for 2 seconds.`n" +
			'The game still sees these buttons, so choose ones you do not hold while playing: View + Menu (Back + Start), or L3 + R3.') }
	$script:recLive = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; MaximumSize = '460,0'; Margin = '0,12,0,12'
		Font = New-Object System.Drawing.Font('Segoe UI', 12, [System.Drawing.FontStyle]::Bold); Text = 'Let go of every button...' }
	$row = New-Object System.Windows.Forms.FlowLayoutPanel -Property @{ AutoSize = $true }
	$script:recSave = New-Object System.Windows.Forms.Button -Property @{ Text = 'Save'; DialogResult = 'OK'; AutoSize = $true; Enabled = $false }
	$again = New-Object System.Windows.Forms.Button -Property @{ Text = 'Record again'; AutoSize = $true }
	$cancel = New-Object System.Windows.Forms.Button -Property @{ Text = 'Cancel'; DialogResult = 'Cancel'; AutoSize = $true }
	$row.Controls.AddRange(@($script:recSave, $again, $cancel))
	$panel.Controls.AddRange(@($info, $script:recLive, $row))
	$dialog.Controls.Add($panel)
	$dialog.CancelButton = $cancel
	$script:recState = @{ phase = 'release'; order = @(); chord = '' }
	# Each gamepad tick while recording (the navigation stands aside).
	$script:recRecorder = {
		param([int]$raw)
		$state = $script:recState
		$held = ConvertFrom-PadBits $raw
		if ($state.phase -eq 'release') {
			if (-not $held) { $state.phase = 'wait'; $script:recLive.Text = 'Press the button(s) now...' }
			return
		}
		if ($held) {
			$state.phase = 'hold'
			foreach ($name in $held) { if ($state.order -notcontains $name) { $state.order += $name } }
			$script:recLive.Text = (@($state.order | ForEach-Object { Get-ButtonText $_ }) -join ' + ')
			return
		}
		if ($state.phase -eq 'wait' -or -not $state.order) { return }
		# Every button let go: done.
		$chord = @($state.order | Select-Object -First 2)
		$state.chord = $chord -join '+'
		$state.phase = 'done'
		$script:padRecorder = $null
		$text = 'Recorded: ' + (@($chord | ForEach-Object { Get-ButtonText $_ }) -join ' + ')
		$text += if ($chord.Count -ge 2) { "`nHold them together for 1 second to quit." } else { "`nHold it for 2 seconds to quit." }
		if ($state.order.Count -gt 2) { $text += ' (Only the first two buttons count.)' }
		if ($chord.Count -eq 1 -and @('Create', 'Touchpad', 'Mute', 'Paddle1', 'Paddle2', 'Paddle3', 'Paddle4') -notcontains $chord[0]) {
			$text += "`nCareful: if the game has you hold $(Get-ButtonText $chord[0]) (to run, block or aim), it would quit. Two buttons are safer."
		}
		$script:recLive.Text = $text
		$script:recSave.Enabled = $true
		[void]$script:recSave.Focus()
	}
	$script:recStart = { $script:recState = @{ phase = 'release'; order = @(); chord = '' }; $script:recSave.Enabled = $false
		$script:recLive.Text = 'Let go of every button...'; $script:padRecorder = $script:recRecorder }
	$again.Add_Click({ & $script:recStart })
	$dialog.Add_FormClosed({ $script:padRecorder = $null })
	& $script:recStart
	$result = $dialog.ShowDialog($form)
	$script:padRecorder = $null
	if ($result -eq 'OK' -and $script:recState.chord) { $script:recState.chord } else { $null }
}
function Invoke-QuitRecord {
	$chord = Show-QuitRecorder
	if ($chord) { Save-QuitChord $chord }
	Update-HotkeyNote
}
$quitMode.Add_SelectedIndexChanged({
	if ($script:quitModeUpdating) { return }
	if ($quitMode.SelectedIndex -eq 0) {
		if ($script:hotkeyMap.QuitChord) { Save-QuitChord '' }
		Update-HotkeyNote
	} elseif (-not $script:hotkeyMap.QuitChord) {
		Invoke-QuitRecord
	}
})
$quitRecord.Add_Click({ Invoke-QuitRecord })
Update-HotkeyNote
Add-Row 'Controller hotkeys' @($hotkey, $hotkeyEdit)
Add-Row 'Quit the game' @($quitMode, $quitRecord)
Add-Row '' @($hotkeyNote)
$hud = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 160 }
$hud.Items.AddRange($overlays)
# (An older launcher saved true for the frame rate.)
$hud.SelectedIndex = [Math]::Max(0, [Math]::Min($overlays.Count - 1, [int]$settings.hud))
$hudNote = New-Object System.Windows.Forms.Label -Property @{ Text = 'top right: frame rate and frame times; detailed: GPU, CPU, memory. F8 in the game switches it'; AutoSize = $true; ForeColor = 'Gray'; Margin = '6,6,0,0' }
Add-Row 'Overlay' @($hud, $hudNote)
$rumble = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 160 }
$rumble.Items.AddRange(@('Off', 'Light', 'Normal', 'Strong', 'Maximum'))
$rumble.SelectedIndex = [Math]::Max(0, [Math]::Min(4, [int]$settings.rumble))
$rumbleNote = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Gray'; Margin = '6,6,0,0'
	Text = 'Xbox-style pads, Legion Go, DualSense on Bluetooth: the game''s haptics as rumble (a USB DualSense gets the real haptics)' }
Add-Row 'Vibration' @($rumble, $rumbleNote)
$ecores = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'Precompile on the efficiency cores only (slower, the PC stays responsive)'; AutoSize = $true
	Checked = ([bool]$settings.ecores -and $efficiencyMask -ne 0); Enabled = ($efficiencyMask -ne 0) }
Add-Row '' @($ecores)

$buttons = New-Object System.Windows.Forms.FlowLayoutPanel -Property @{ AutoSize = $true; Margin = '0,10,0,0' }
$play = New-Object System.Windows.Forms.Button -Property @{ Text = 'Play'; AutoSize = $true; Font = New-Object System.Drawing.Font('Segoe UI', 9, [System.Drawing.FontStyle]::Bold) }
$precompile = New-Object System.Windows.Forms.Button -Property @{ Text = 'Precompile shaders'; AutoSize = $true }
$share = New-Object System.Windows.Forms.Button -Property @{ Text = 'Export shader hints'; AutoSize = $true }
(New-Object System.Windows.Forms.ToolTip).SetToolTip($share, ('The shaders your play recorded, without the game''s code, as my-hints-<game>.hints: ' +
	'attached to a GitHub issue, the next packages precompile them for everyone.'))
$logs = New-Object System.Windows.Forms.Button -Property @{ Text = 'Logs folder'; AutoSize = $true }
$readme = New-Object System.Windows.Forms.Button -Property @{ Text = 'README'; AutoSize = $true }
$buttons.Controls.AddRange(@($play, $precompile, $share, $logs, $readme))
$grid.Controls.Add($buttons)
$grid.SetColumnSpan($buttons, 2)
$credit = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Gray'; Margin = '0,8,0,0'
	Text = 'Settings follow the KytyPS5 launcher (src/launcher) by the KytyPS5 developers.' }
$grid.Controls.Add($credit)
$grid.SetColumnSpan($credit, 2)
$padNote = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Gray'; Margin = '0,4,0,0'
	Text = ('Controller: D-pad or left stick up/down moves, left/right changes a setting, A presses or ticks, B goes back, ' +
		'Menu (Start/Options) plays. Xbox-style (XInput) controllers; a DualSense through Steam Input or DS4Windows.') }
$padNote.MaximumSize = '700,0'
$grid.Controls.Add($padNote)
$grid.SetColumnSpan($padNote, 2)
$form.AcceptButton = $play

function Read-Form {
	$settings.game       = $game.Text.Trim()
	$settings.resolution = [string]$resolution.SelectedItem
	$settings.fullscreen = $fullscreen.Checked
	$settings.aspect     = $aspect.Checked
	$settings.language   = $language.SelectedIndex
	$settings.redzone    = $redzone.Checked
	$settings.fps120     = $fps120.Checked
	$settings.vsync      = $vsync.Checked
	$settings.hdr        = $hdr.Checked
	$settings.safegpu    = $safeGpu.Checked
	$settings.hud        = $hud.SelectedIndex
	$height              = [int](([string]$render.SelectedItem -split ' ')[0])
	if ($renderForDlss) { $settings.render_dlss = $height } else { $settings.render = $height }
	$settings.ratio      = ([string]$ratio.SelectedItem -split ' ')[0]
	$settings.quality    = ([string]$quality.SelectedItem -split ' ')[0].ToLowerInvariant()
	# (Kept as they were where the runtimes are missing.)
	$settings.framegen_backend = Get-FrameGenBackendKey
	$settings.fg_hudless       = $fgHudless.Checked
	$settings.rumble           = $rumble.SelectedIndex
	if ($frameGen.Enabled) { $settings.framegen = if ($frameGen.SelectedIndex -gt 0) { $frameGen.SelectedIndex + 1 } else { 0 } }
	if ($upscaler.Enabled) { $settings.upscaler = $upscalerKeys[[Math]::Max(0, $upscaler.SelectedIndex)] }
	$settings.upscale_quality = $presetKeys[[Math]::Max(0, $preset.SelectedIndex)]
	$settings.fsr_sharpness   = [double]$sharpness.Value
	# (The hold button chosen here; the game may have changed hotkeys.ini meanwhile: its other lines are read again.)
	$current = Read-Hotkeys
	if ($script:hotkeyMap.Modifier -and $script:hotkeyMap.Modifier -ne $current.Modifier) {
		$current.Modifier = $script:hotkeyMap.Modifier
		Save-Hotkeys $current
	}
	$settings.ecores     = $ecores.Checked
	Save-Settings
}
$play.Add_Click({
	Read-Form
	Start-Process $cmd -ArgumentList "/v:on /s /c `"$(Get-PlayCommand)`"" -WorkingDirectory $root
	$form.Close()
})
$precompile.Add_Click({
	Read-Form
	Start-Process $cmd -ArgumentList "/s /c `"$(Get-PrecompileCommand)`"" -WorkingDirectory $root
})
$share.Add_Click({
	Read-Form
	Start-Process $cmd -ArgumentList "/s /c `"$(Get-PrecompileCommand '-ExportHints')`"" -WorkingDirectory $root
})
$logs.Add_Click({
	$folder = if (Test-Path "$root\_Build") { "$root\_Build\run-logs" } else { "$root\logs" }
	New-Item -ItemType Directory -Force $folder | Out-Null
	Start-Process explorer.exe $folder
})
$readme.Add_Click({
	$file = @("$root\README.md", "$root\docs\PORTABLE-README.md") | Where-Object { Test-Path $_ } | Select-Object -First 1
	if ($file) { Start-Process notepad.exe "`"$file`"" }
})
# A button newly pressed on the controller: the focused control's keyboard equivalent.
function Invoke-PadPress([int]$pressed) {
	$active = [System.Windows.Forms.Form]::ActiveForm
	if (-not $active) {
		# A message box or a folder dialog: its keys.
		$key = if ($pressed -band 0x1000) { '{ENTER}' } elseif ($pressed -band 0x2000) { '{ESC}' }
			elseif ($pressed -band 0x5) { '+{TAB}' } elseif ($pressed -band 0xA) { '{TAB}' } else { '' }
		if ($key) { [System.Windows.Forms.SendKeys]::SendWait($key) }
		return
	}
	[KytyLauncher.Pad]::ShowFocus($active.Handle)
	$control = $active.ActiveControl
	for ($parent = $control; $parent; $parent = $parent.Parent) {
		if ($parent -is [System.Windows.Forms.NumericUpDown]) { $control = $parent; break }
	}
	if (-not $control) { [void]$active.SelectNextControl($null, $true, $true, $true, $true); return }
	if ($pressed -band 0x3) {
		[void]$active.SelectNextControl($control, [bool]($pressed -band 0x2), $true, $true, $true)
	} elseif ($pressed -band 0xC) {
		$step = if ($pressed -band 0x8) { 1 } else { -1 }
		if ($control -is [System.Windows.Forms.ComboBox]) {
			$index = [Math]::Max(0, [Math]::Min($control.Items.Count - 1, $control.SelectedIndex + $step))
			if ($index -ne $control.SelectedIndex) { $control.SelectedIndex = $index }
		} elseif ($control -is [System.Windows.Forms.NumericUpDown]) {
			if ($step -gt 0) { $control.UpButton() } else { $control.DownButton() }
		} else {
			[void]$active.SelectNextControl($control, $step -gt 0, $true, $true, $true)
		}
	} elseif ($pressed -band 0x1000) {
		if ($control -is [System.Windows.Forms.Button]) { $control.PerformClick() }
		elseif ($control -is [System.Windows.Forms.CheckBox]) { $control.Checked = -not $control.Checked }
		elseif ($control -eq $game) { $browse.PerformClick() }
		else { [void]$active.SelectNextControl($control, $true, $true, $true, $true) }
	} elseif ($pressed -band 0x2000) {
		if ($active -ne $form) { $active.DialogResult = 'Cancel' }
	} elseif ($pressed -band 0x10) {
		if ($active -eq $form) { $play.PerformClick() } elseif ($active.AcceptButton) { $active.AcceptButton.PerformClick() }
	}
}
# Polls the controller (the timer runs during dialogs too); directions repeat while held.
$script:padPrev = 0
$script:padRepeatAt = 0
$script:padRecorder = $null
$padTimer = New-Object System.Windows.Forms.Timer -Property @{ Interval = 30 }
$padTimer.Add_Tick({
	try {
		if (-not [KytyLauncher.Pad]::InFront()) { $script:padPrev = 0; return }
		$bits = [KytyLauncher.Pad]::Read($true)
		if ($script:padRecorder) {
			& $script:padRecorder ([KytyLauncher.Pad]::Read($false))
			# (The buttons still held when it is done do nothing until pressed again.)
			$script:padPrev = $bits
			return
		}
		$now = [Environment]::TickCount
		$pressed = $bits -band -bnot $script:padPrev
		$directions = $bits -band 0xF
		if ($pressed -band 0xF) { $script:padRepeatAt = $now + 400 }
		elseif ($directions -and $now - $script:padRepeatAt -ge 0) { $pressed = $pressed -bor $directions; $script:padRepeatAt = $now + 120 }
		$script:padPrev = $bits
		if ($pressed) { Invoke-PadPress $pressed }
	} catch { $padTimer.Stop() }
})
if ([KytyLauncher.Pad]::Available) { $padTimer.Start() }
$form.Add_FormClosing({ $padTimer.Stop(); Read-Form })
[void]$form.ShowDialog()
