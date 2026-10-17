$ErrorActionPreference='Stop'
$Git='D:\Git\cmd\git.exe'; $Remote='https://github.com/dedadeda26/ssb-voice-processor.git'; $Proxy='http://127.0.0.1:7890'
if (-not (Test-Path $Git)) { throw "找不到 git.exe：$Git" }
Write-Host "== 当前提交 ==" -ForegroundColor Cyan
& $Git log --oneline -3
$existing = & $Git remote
if ($existing -contains 'origin') { & $Git remote set-url origin $Remote } else { & $Git remote add origin $Remote }
$proxyArgs = @()
if ($Proxy -ne '') { $proxyArgs = @('-c', "http.proxy=$Proxy") }
Write-Host "== 推送到 $Remote ==" -ForegroundColor Cyan
& $Git @proxyArgs push -u origin main
& $Git @proxyArgs push origin v0.1.0
Write-Host ""
Write-Host "完成。Release 页面上传安装包：" -ForegroundColor Green
Write-Host "  SSBVoiceProcessor\Installer\output\SSBVoiceProcessor_Setup_0.1.0.exe"
