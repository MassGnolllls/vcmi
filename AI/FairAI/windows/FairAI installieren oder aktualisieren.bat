@echo off
rem ===========================================================================
rem  FairAI fuer VCMI 1.7.5 - Installation und Update (Windows x64)
rem
rem  Doppelklick genuegt. Die Datei
rem    1. laedt das aktuelle Paket "VCMI 1.7.5 mit FairAI" aus dem Fork
rem       github.com/MassGnolllls/vcmi (Release "fairai-latest") und prueft
rem       dessen Pruefsumme (SHA-256),
rem    2. entpackt es in den Unterordner "VCMI-FairAI" neben dieser Datei,
rem    3. legt beim ersten Mal eine Kopie deiner VCMI-Einstellungen im
rem       Unterordner "Einstellungen" an und traegt dort FairAI als Gegner
rem       auf der Abenteuerkarte ein,
rem    4. legt die Verknuepfung "VCMI mit FairAI" auf den Desktop.
rem
rem  Deine bestehende VCMI-Installation und ihre Einstellungen werden nicht
rem  veraendert. Spieldaten, Karten, Mods und Spielstaende werden gemeinsam
rem  genutzt (Dokumente\My Games\vcmi).
rem  Ein spaeterer Doppelklick aktualisiert auf die neueste Version.
rem ===========================================================================
setlocal
title FairAI fuer VCMI - Installation und Update
set "FAIRAI_SELF=%~f0"
set "FAIRAI_BASE=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$t=[IO.File]::ReadAllText($env:FAIRAI_SELF,[Text.Encoding]::UTF8); $m='#:FAIRAI-'+'POWERSHELL:'; Invoke-Expression $t.Substring($t.IndexOf($m))"
set "FAIRAI_RC=%ERRORLEVEL%"
echo.
pause
exit /b %FAIRAI_RC%

#:FAIRAI-POWERSHELL:
# Ab hier PowerShell (Windows PowerShell 5.1 genuegt). Der Teil oben startet diesen Teil.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch { }

$source     = 'https://github.com/MassGnolllls/vcmi/releases/download/fairai-latest'
$zipName    = 'VCMI-FairAI-windows-x64.zip'
$hashName   = 'VCMI-FairAI-windows-x64.sha256'
$versName   = 'VCMI-FairAI-version.txt'

$base       = $env:FAIRAI_BASE.TrimEnd('\')
$install    = Join-Path $base 'VCMI-FairAI'
$oldInstall = Join-Path $base 'VCMI-FairAI.alt'
$config     = Join-Path $base 'Einstellungen'
$work       = Join-Path $base '_temp'
$utf8       = New-Object System.Text.UTF8Encoding($false)

function Say([string]$text, [string]$color = 'Gray')
{
	Write-Host $text -ForegroundColor $color
}

function Stop-Setup([string]$text)
{
	Write-Host ''
	Write-Host ('FEHLER: ' + $text) -ForegroundColor Red
	Write-Host 'Deine bestehende VCMI-Installation wurde nicht verändert.' -ForegroundColor Yellow
	Write-Host 'Du kannst diese Datei jederzeit noch einmal starten.' -ForegroundColor Yellow
	exit 1
}

function Remove-Tree([string]$path)
{
	for($round = 0; ($round -lt 5) -and (Test-Path -LiteralPath $path); $round++)
	{
		try { [IO.Directory]::Delete($path, $true) }
		catch { Start-Sleep -Milliseconds 700 }
	}
	if(Test-Path -LiteralPath $path)
	{
		throw ('Der Ordner ' + $path + ' lässt sich nicht löschen. Ist darin noch etwas geöffnet?')
	}
}

function Get-File([string]$name, [string]$target, [bool]$showProgress)
{
	$url = $source + '/' + $name
	if(Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
	$curl = Join-Path $env:SystemRoot 'System32\curl.exe'
	if(Test-Path -LiteralPath $curl)
	{
		if($showProgress) { & $curl -L --fail --retry 3 --progress-bar -o $target $url }
		else { & $curl -L --fail --retry 3 --silent --show-error -o $target $url }
		if($LASTEXITCODE -ne 0) { throw ('Download von ' + $name + ' fehlgeschlagen (curl meldet ' + $LASTEXITCODE + ').') }
	}
	else
	{
		Invoke-WebRequest -Uri $url -OutFile $target -UseBasicParsing
	}
	if(-not (Test-Path -LiteralPath $target)) { throw ('Download von ' + $name + ' fehlgeschlagen.') }
}

# Trägt FairAI als Gegner auf der Abenteuerkarte in den Text einer settings.json ein.
# (Kein ConvertTo-Json: VCMI liest keine \u-Schreibweise, die PowerShell dabei erzeugt.)
function Set-EnemyAI([string]$text)
{
	$entry = '"adventureEnemyAI" : "FairAI"'
	$pattern = '"adventureEnemyAI"\s*:\s*"[^"]*"'
	if([regex]::IsMatch($text, $pattern))
	{
		return [regex]::Replace($text, $pattern, $entry)
	}
	$m = [regex]::Match($text, '"ai"\s*:\s*\{')
	if($m.Success)
	{
		$cut = $m.Index + $m.Length
		$rest = $text.Substring($cut)
		$sep = ','
		if($rest.TrimStart().StartsWith('}')) { $sep = '' }
		return $text.Substring(0, $cut) + "`r`n`t`t" + $entry + $sep + $rest
	}
	$i = $text.IndexOf('{')
	if($i -lt 0)
	{
		return "{`r`n`t`"ai`" : {`r`n`t`t" + $entry + "`r`n`t}`r`n}`r`n"
	}
	$rest = $text.Substring($i + 1)
	$sep = ','
	if($rest.TrimStart().StartsWith('}')) { $sep = '' }
	return $text.Substring(0, $i + 1) + "`r`n`t`"ai`" : {`r`n`t`t" + $entry + "`r`n`t}" + $sep + $rest
}

try
{
	Say ''
	Say 'FairAI für VCMI 1.7.5 - Installation und Update' 'White'
	Say '================================================' 'White'
	Say ('Ordner: ' + $base)
	Say ''

	# ---- läuft dieses VCMI gerade?
	$names = @('VCMI_launcher', 'VCMI_client', 'VCMI_server', 'VCMI_mapeditor')
	foreach($p in @(Get-Process -Name $names -ErrorAction SilentlyContinue))
	{
		$exe = $null
		try { $exe = $p.Path } catch { }
		if($exe -and $exe.StartsWith($install, [System.StringComparison]::OrdinalIgnoreCase))
		{
			Stop-Setup 'VCMI mit FairAI läuft noch. Bitte schließen und diese Datei noch einmal starten.'
		}
	}

	# ---- welche Version liegt bereit, welche ist installiert?
	if(-not (Test-Path -LiteralPath $work)) { New-Item -ItemType Directory -Force -Path $work | Out-Null }
	Say '1/4  Frage die neueste Version ab ...' 'Cyan'
	$versFile = Join-Path $work $versName
	$hashFile = Join-Path $work $hashName
	Get-File $versName $versFile $false
	Get-File $hashName $hashFile $false
	$newVersion = ([IO.File]::ReadAllText($versFile)).Trim()
	$wantHash = ([IO.File]::ReadAllText($hashFile)).Trim().ToLower()
	if($wantHash -notmatch '^[0-9a-f]{64}$') { throw 'Die Prüfsumme im Release hat nicht das erwartete Format.' }
	$haveVersion = ''
	$haveFile = Join-Path $install 'FairAI-version.txt'
	if(Test-Path -LiteralPath $haveFile) { $haveVersion = ([IO.File]::ReadAllText($haveFile)).Trim() }
	$short = $newVersion
	if($short.Length -gt 10) { $short = $short.Substring(0, 10) }
	$current = ($haveVersion -eq $newVersion) -and (Test-Path -LiteralPath (Join-Path $install 'VCMI_launcher.exe'))

	if($current)
	{
		Say ('     Version ' + $short + ' ist bereits installiert - kein Download nötig.') 'Green'
	}
	else
	{
		# ---- laden und prüfen
		Say ('2/4  Lade Version ' + $short + ' (das Paket ist groß, bitte warten) ...') 'Cyan'
		$zip = Join-Path $work $zipName
		Get-File $zipName $zip $true
		$gotHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower()
		if($gotHash -ne $wantHash)
		{
			Remove-Item -LiteralPath $zip -Force
			throw 'Die Prüfsumme des geladenen Pakets stimmt nicht. Bitte noch einmal starten; bleibt es dabei, bitte Claude Bescheid geben.'
		}
		Say '     Prüfsumme stimmt.' 'Green'

		# ---- entpacken (im selben Ordner, damit das Verschieben danach nur ein Umbenennen ist)
		Say '3/4  Entpacke ...' 'Cyan'
		$unpack = Join-Path $work 'entpackt'
		Remove-Tree $unpack
		New-Item -ItemType Directory -Force -Path $unpack | Out-Null
		$tar = Join-Path $env:SystemRoot 'System32\tar.exe'
		if(Test-Path -LiteralPath $tar)
		{
			& $tar -xf $zip -C $unpack
			if($LASTEXITCODE -ne 0) { throw ('Entpacken fehlgeschlagen (tar meldet ' + $LASTEXITCODE + ').') }
		}
		else
		{
			Expand-Archive -LiteralPath $zip -DestinationPath $unpack -Force
		}
		$launcher = Get-ChildItem -LiteralPath $unpack -Recurse -Filter 'VCMI_launcher.exe' | Select-Object -First 1
		if(-not $launcher) { throw 'Im Paket fehlt VCMI_launcher.exe.' }
		$appDir = $launcher.Directory.FullName
		$dll = Get-ChildItem -LiteralPath $appDir -Recurse -Filter 'FairAI.dll' | Select-Object -First 1
		if(-not $dll) { throw 'Im Paket fehlt FairAI.dll.' }

		# ---- alten Stand beiseite, neuen an seinen Platz
		Remove-Tree $oldInstall
		if(Test-Path -LiteralPath $install) { Rename-Item -LiteralPath $install -NewName (Split-Path $oldInstall -Leaf) }
		try
		{
			Move-Item -LiteralPath $appDir -Destination $install
		}
		catch
		{
			if((-not (Test-Path -LiteralPath $install)) -and (Test-Path -LiteralPath $oldInstall))
			{
				Rename-Item -LiteralPath $oldInstall -NewName (Split-Path $install -Leaf)
			}
			throw
		}
		[IO.File]::WriteAllText((Join-Path $install 'FairAI-version.txt'), $newVersion + "`r`n", $utf8)
		Remove-Tree $oldInstall
	}
	try { Remove-Tree $work } catch { }

	# ---- eigene Einstellungen für dieses VCMI (deine bisherigen bleiben, wie sie sind)
	Say '4/4  Einstellungen und Verknüpfung ...' 'Cyan'
	$cfgDir = Join-Path $install 'config'
	if(-not (Test-Path -LiteralPath $cfgDir)) { New-Item -ItemType Directory -Force -Path $cfgDir | Out-Null }
	$dirs = "{`r`n`t`"userConfigPath`" : `"" + $config.Replace('\', '/') + "`"`r`n}`r`n"
	[IO.File]::WriteAllText((Join-Path $cfgDir 'dirs.json'), $dirs, $utf8)

	$settings = Join-Path $config 'settings.json'
	if(-not (Test-Path -LiteralPath $settings))
	{
		New-Item -ItemType Directory -Force -Path $config | Out-Null
		$documents = [Environment]::GetFolderPath('MyDocuments')
		$oldConfig = Join-Path $documents 'My Games\vcmi\config'
		if(Test-Path -LiteralPath $oldConfig)
		{
			Copy-Item -Path (Join-Path $oldConfig '*') -Destination $config -Recurse -Force
			Say '     Deine VCMI-Einstellungen (Mods, Tasten, Auflösung) wurden übernommen.'
		}
		$text = ''
		if(Test-Path -LiteralPath $settings) { $text = [IO.File]::ReadAllText($settings) }
		[IO.File]::WriteAllText($settings, (Set-EnemyAI $text), $utf8)
		Say '     FairAI ist als Gegner auf der Abenteuerkarte eingetragen.' 'Green'
	}
	else
	{
		Say '     Deine FairAI-Einstellungen bleiben, wie sie sind.'
	}

	try
	{
		$shell = New-Object -ComObject WScript.Shell
		$targets = @((Join-Path ([Environment]::GetFolderPath('Desktop')) 'VCMI mit FairAI.lnk'), (Join-Path $base 'VCMI mit FairAI starten.lnk'))
		foreach($lnk in $targets)
		{
			$s = $shell.CreateShortcut($lnk)
			$s.TargetPath = (Join-Path $install 'VCMI_launcher.exe')
			$s.WorkingDirectory = $install
			$s.Description = 'VCMI 1.7.5 mit FairAI'
			$s.Save()
		}
	}
	catch
	{
		Say '     Hinweis: Die Verknüpfung konnte nicht angelegt werden. Starte dann VCMI-FairAI\VCMI_launcher.exe direkt.' 'Yellow'
	}

	Say ''
	Say 'Fertig.' 'Green'
	Say ('Installiert ist Version ' + $short + '.')
	Say 'Start: Verknüpfung "VCMI mit FairAI" auf dem Desktop (oder in diesem Ordner).'
	Say 'Im Launcher unter Einstellungen > KI steht FairAI als Gegner auf der Abenteuerkarte;'
	Say 'dort kannst du jederzeit auf Nullkiller2 zurückstellen.'
	Say 'Für ein Update diese Datei einfach noch einmal doppelklicken.'
	exit 0
}
catch
{
	Stop-Setup $_.Exception.Message
}
