; ============================================================
; Inno Setup 脚本 — 由 generate_installer.py 自动生成，请勿手动编辑
; ============================================================

#define MyAppName      "SSB Voice Processor"
#define MyAppVersion   "0.1.0"
#define MyAppPublisher "BI4AGE"
#define MyAppExeName   "SSB Voice Processor.exe"

[Setup]
AppId={{8A3D7C1E-4B2F-4E9A-9C5D-6F1B8A3D7C1E}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppComments=make SSB hifi again - by BI4AGE
VersionInfoCompany=BI4AGE
VersionInfoDescription=SSB Voice Processor Setup - make SSB hifi again
VersionInfoCopyright=(c) BI4AGE
DefaultDirName={autopf}\SSBVoiceProcessor
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
AllowNoIcons=yes
OutputDir=output
OutputBaseFilename=SSBVoiceProcessor_Setup_0.1.0
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
WizardResizable=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ShowLanguageDialog=no
SetupIconFile=../Resources/icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
WizardImageFile=wizard_image.bmp
WizardSmallImageFile=wizard_small.bmp

[Messages]
WelcomeLabel1=欢迎安装 [name]
WelcomeLabel2=即将在本机安装 [name] [ver]。%n%n这是为 SSB 通联准备的实时语音处理器。%n%nMake SSB Hifi Again!
SelectDirLabel3=安装程序将把 [name] 安装到下面的文件夹。
SelectDirBrowseLabel=点「下一步」继续；想换位置就点「浏览」。
SelectTasksLabel2=请选择要执行的附加任务，然后点「下一步」。
ReadyLabel1=准备就绪，可以开始安装。
ReadyLabel2a=点「安装」开始，或点「上一步」检查设置。
InstallingLabel=正在安装 [name]，请稍候……
FinishedHeadingLabel=[name] 安装完成
FinishedLabel=点「完成」关闭安装程序。桌面快捷方式可以直接启动。
FinishedLabelNoIcons=点「完成」关闭安装程序。
ClickFinish=点「完成」关闭安装程序。

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
Source: "../../build/SSBVoiceProcessor_artefacts/Release/SSB Voice Processor.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 卸载时清理安装目录（防止运行过程中生成的缓存残留）
Type: filesandordirs; Name: "{app}"

[Code]
{ ============================================================
   通用辅助函数
   ============================================================ }

{ 返回字符串中最后一个反斜杠的位置，没有返回 0 }
function FindLastBackslash(const S: String): Integer;
var
  I: Integer;
begin
  Result := 0;
  for I := Length(S) downto 1 do
  begin
    if S[I] = '\' then
    begin
      Result := I;
      Exit;
    end;
  end;
end;

{ ============================================================
   安装：强制在用户选定的安装位置下追加应用名目录
   例如用户输入 D:\Tools  -> 实际装到 D:\Tools\SSBVoiceProcessor
   ============================================================ }
function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir, Base: String;
  P: Integer;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    Dir := WizardForm.DirEdit.Text;

    { 去掉末尾反斜杠 }
    while (Length(Dir) > 0) and (Dir[Length(Dir)] = '\') do
      Delete(Dir, Length(Dir), 1);
    if Dir = '' then Exit;

    { 取最后一段 }
    P := FindLastBackslash(Dir);
    if P > 0 then
      Base := Copy(Dir, P + 1, Length(Dir) - P)
    else
      Base := Dir;

    { 如果最后一段不是应用目录名，就追加 }
    if CompareText(Base, 'SSBVoiceProcessor') <> 0 then
      WizardForm.DirEdit.Text := Dir + '\SSBVoiceProcessor';
  end;
end;

{ ============================================================
   卸载：完成后询问是否删除设置和预设数据
   数据目录：%APPDATA%\SSBVoiceProcessor\
   注意：MsgBox 调用必须写在一行，否则 #13#10 会被 Inno
         当成预处理指令，报 Unknown preprocessor directive
   ============================================================ }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
  MsgResult: Integer;
  MsgText: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    DataDir := ExpandConstant('{userappdata}\SSBVoiceProcessor');

    if DirExists(DataDir) then
    begin
      MsgText := '是否同时删除所有设置和预设数据？' + #13#10 + #13#10 + '位置：' + DataDir + #13#10 + #13#10 + '选择「是」将彻底清除（包括所有已保存的预设、背景图片设置、界面偏好等）。' + #13#10 + '选择「否」将保留数据，方便下次重装后继续使用。';
      MsgResult := MsgBox(MsgText, mbConfirmation, MB_YESNO);

      if MsgResult = IDYES then
        DelTree(DataDir, True, True, True);
    end;
  end;
end;
