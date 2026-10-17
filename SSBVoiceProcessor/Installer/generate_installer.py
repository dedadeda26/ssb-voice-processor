#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_installer.py

为 SSB Voice Processor 生成 Inno Setup 安装包脚本。

用法：
    cd D:\\SSBVoiceProcessor\\Installer
    python generate_installer.py
    "D:\\Dev\\InnoSetup\\ISCC.exe" installer.iss
"""

import os
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont, ImageFilter
except ImportError:
    print("缺少 Pillow，请运行: pip install Pillow")
    sys.exit(1)

# ============ 路径 ============
HERE     = Path(__file__).resolve().parent
PROJECT  = HERE.parent
BUILD    = PROJECT.parent / "build"   # CMake 的构建目录在仓库根目录下
RES_DIR  = PROJECT / "Resources"
OUT_DIR  = HERE / "output"

# ============ Inno Setup 安装目录 ============
INNO_DIR = Path(r"D:\Dev\InnoSetup")

# ============ 常量 ============
APP_NAME      = "SSB Voice Processor"
APP_VERSION   = "0.1.0"
APP_PUBLISHER = "BI4AGE"
APP_ID        = "{{8A3D7C1E-4B2F-4E9A-9C5D-6F1B8A3D7C1E}}"

DATA_FOLDER   = "SSBVoiceProcessor"

COLOR_TOP    = (26, 33, 48)
COLOR_BOTTOM = (12, 17, 25)
COLOR_ACCENT = (126, 204, 255)
COLOR_TEXT   = (238, 243, 251)


def check_chinese_isl():
    candidates = [
        INNO_DIR / "Languages" / "ChineseSimplified.isl",
        INNO_DIR / "Languages" / "ChineseSimplified-12-5.5.0.isl",
    ]
    for c in candidates:
        if c.exists():
            return True, c.name
    return False, None


def find_exe():
    candidates = [
        BUILD / "SSBVoiceProcessor_artefacts" / "Release" / "SSBVoiceProcessor.exe",
        BUILD / "SSBVoiceProcessor_artefacts" / "Release" / "SSB Voice Processor.exe",
        BUILD / "SSBVoiceProcessor_artefacts" / "Release" / "Standalone" / "SSBVoiceProcessor.exe",
        BUILD / "SSBVoiceProcessor_artefacts" / "Release" / "Standalone" / "SSB Voice Processor.exe",
        BUILD / "SSBVoiceProcessor_artefacts" / "Debug" / "SSBVoiceProcessor.exe",
        BUILD / "SSBVoiceProcessor_artefacts" / "Debug" / "SSB Voice Processor.exe",
    ]
    for c in candidates:
        if c.exists():
            return c
    if BUILD.exists():
        for p in BUILD.rglob("*.exe"):
            name = p.name
            if "SSB" in name or "Voice" in name:
                return p
        for p in BUILD.rglob("*.exe"):
            low = p.name.lower()
            if "cmake" in low or "juceaide" in low:
                continue
            return p
    return None


def gradient_bg(size, top_color, bottom_color):
    w, h = size
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        r = int(top_color[0] * (1 - t) + bottom_color[0] * t)
        g = int(top_color[1] * (1 - t) + bottom_color[1] * t)
        b = int(top_color[2] * (1 - t) + bottom_color[2] * t)
        for x in range(w):
            px[x, y] = (r, g, b)
    return img


def draw_centered_glow(img, center, radius, color, alpha_peak=0.45, layers=24):
    w, h = img.size
    cx, cy = center
    overlay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    for i in range(layers, 0, -1):
        r = radius * i / layers
        a = int(255 * alpha_peak * (1 - i / layers) ** 2)
        if a < 2:
            continue
        od.ellipse([cx - r, cy - r, cx + r, cy + r],
                   fill=(color[0], color[1], color[2], a))
    overlay = overlay.filter(ImageFilter.GaussianBlur(6))
    return Image.alpha_composite(img.convert("RGBA"), overlay).convert("RGB")


def draw_bottom_glow(img, color):
    w, h = img.size
    overlay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    glow_h = int(h * 0.18)
    for y in range(glow_h):
        t = y / max(1, glow_h - 1)
        a = int(255 * 0.20 * t)
        od.rectangle([0, h - glow_h + y, w, h - glow_h + y + 1],
                     fill=(color[0], color[1], color[2], a))
    overlay = overlay.filter(ImageFilter.GaussianBlur(4))
    return Image.alpha_composite(img.convert("RGBA"), overlay).convert("RGB")


def draw_icon_centered(img, icon_path, icon_size, center):
    if not icon_path.exists():
        return img
    icon = Image.open(icon_path).convert("RGBA")
    icon = icon.resize((icon_size, icon_size), Image.LANCZOS)
    cx, cy = center
    pos = (cx - icon_size // 2, cy - icon_size // 2)
    img_rgba = img.convert("RGBA")
    img_rgba.paste(icon, pos, icon)
    return img_rgba.convert("RGB")


def draw_text_centered(img, text, center, font_size, color, bold=False):
    draw = ImageDraw.Draw(img)
    font = None
    candidates = [
        "C:/Windows/Fonts/segoeuib.ttf" if bold else "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/msyhbd.ttc"   if bold else "C:/Windows/Fonts/msyh.ttc",
    ]
    for c in candidates:
        try:
            font = ImageFont.truetype(c, font_size)
            break
        except Exception:
            continue
    if font is None:
        font = ImageFont.load_default()
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    cx, cy = center
    draw.text((cx - tw // 2, cy - th // 2 - bbox[1]),
              text, font=font, fill=color)
    return img


def generate_wizard_big(icon_path):
    W, H = 164, 314
    img = gradient_bg((W, H), COLOR_TOP, COLOR_BOTTOM)
    img = draw_centered_glow(img, (W // 2, 110), 70, COLOR_ACCENT, alpha_peak=0.28)
    img = draw_icon_centered(img, icon_path, 88, (W // 2, 110))
    img = draw_bottom_glow(img, COLOR_ACCENT)
    img = draw_text_centered(img, "SSB",       (W // 2, 220), 15, COLOR_TEXT, bold=True)
    img = draw_text_centered(img, "Voice",     (W // 2, 240), 15, COLOR_TEXT, bold=True)
    img = draw_text_centered(img, "Processor", (W // 2, 260), 15, COLOR_TEXT, bold=True)
    img = draw_text_centered(img, "by BI4AGE", (W // 2, 292), 11, COLOR_ACCENT, bold=True)
    return img


def generate_wizard_small(icon_path):
    W, H = 55, 55
    img = Image.new("RGB", (W, H), COLOR_BOTTOM)
    img = draw_icon_centered(img, icon_path, 48, (W // 2, H // 2))
    return img


def to_rel(path, base):
    try:
        return path.relative_to(base).as_posix()
    except ValueError:
        rel = os.path.relpath(str(path), str(base))
        return rel.replace("\\", "/")


def build_iss(exe_path, exe_name, icon_ico, wizard_big, wizard_small,
              has_chinese, isl_name):
    exe_rel = to_rel(exe_path, HERE)

    icon_rel = ""
    if icon_ico and icon_ico.exists():
        icon_rel = to_rel(icon_ico, HERE)

    wizard_rel = wizard_big.name if wizard_big else ""
    small_rel  = wizard_small.name if wizard_small else ""

    # ============ 头部（f-string） ============
    iss = f'''; ============================================================
; Inno Setup 脚本 — 由 generate_installer.py 自动生成，请勿手动编辑
; ============================================================

#define MyAppName      "{APP_NAME}"
#define MyAppVersion   "{APP_VERSION}"
#define MyAppPublisher "{APP_PUBLISHER}"
#define MyAppExeName   "{exe_name}"

[Setup]
AppId={APP_ID}
AppName={{#MyAppName}}
AppVersion={{#MyAppVersion}}
AppVerName={{#MyAppName}} {{#MyAppVersion}}
AppPublisher={{#MyAppPublisher}}
AppComments=make SSB hifi again - by BI4AGE
VersionInfoCompany={APP_PUBLISHER}
VersionInfoDescription={APP_NAME} Setup - make SSB hifi again
VersionInfoVersion={APP_VERSION}.0
VersionInfoCopyright=(c) BI4AGE
DefaultDirName={{autopf}}\\SSBVoiceProcessor
DefaultGroupName={{#MyAppName}}
DisableProgramGroupPage=yes
AllowNoIcons=yes
OutputDir=output
OutputBaseFilename=SSBVoiceProcessor_Setup_{APP_VERSION}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
WizardResizable=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ShowLanguageDialog=no
'''

    if icon_rel:
        iss += 'SetupIconFile=' + icon_rel + '\n'
        iss += 'UninstallDisplayIcon={app}\\{#MyAppExeName}\n'

    if wizard_rel:
        iss += 'WizardImageFile=' + wizard_rel + '\n'
    if small_rel:
        iss += 'WizardSmallImageFile=' + small_rel + '\n'

    if has_chinese and isl_name:
        iss += f'''
[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\\{isl_name}"
'''

    # ============ 主体（f-string） ============
    iss += f'''
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
Name: "desktopicon"; Description: "{{cm:CreateDesktopIcon}}"; GroupDescription: "{{cm:AdditionalIcons}}"; Flags: checkedonce

[Files]
Source: "{exe_rel}"; DestDir: "{{app}}"; Flags: ignoreversion

[Icons]
Name: "{{group}}\\{{#MyAppName}}"; Filename: "{{app}}\\{{#MyAppExeName}}"
Name: "{{group}}\\{{cm:UninstallProgram,{{#MyAppName}}}}"; Filename: "{{uninstallexe}}"
Name: "{{autodesktop}}\\{{#MyAppName}}"; Filename: "{{app}}\\{{#MyAppExeName}}"; Tasks: desktopicon

[Run]
Filename: "{{app}}\\{{#MyAppExeName}}"; Description: "{{cm:LaunchProgram,{{#MyAppName}}}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 卸载时清理安装目录（防止运行过程中生成的缓存残留）
Type: filesandordirs; Name: "{{app}}"

[Code]
{{ ============================================================
   通用辅助函数
   ============================================================ }}

{{ 返回字符串中最后一个反斜杠的位置，没有返回 0 }}
function FindLastBackslash(const S: String): Integer;
var
  I: Integer;
begin
  Result := 0;
  for I := Length(S) downto 1 do
  begin
    if S[I] = '\\' then
    begin
      Result := I;
      Exit;
    end;
  end;
end;

{{ ============================================================
   安装：强制在用户选定的安装位置下追加应用名目录
   例如用户输入 D:\\Tools  -> 实际装到 D:\\Tools\\SSBVoiceProcessor
   ============================================================ }}
function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir, Base: String;
  P: Integer;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    Dir := WizardForm.DirEdit.Text;

    {{ 去掉末尾反斜杠 }}
    while (Length(Dir) > 0) and (Dir[Length(Dir)] = '\\') do
      Delete(Dir, Length(Dir), 1);
    if Dir = '' then Exit;

    {{ 取最后一段 }}
    P := FindLastBackslash(Dir);
    if P > 0 then
      Base := Copy(Dir, P + 1, Length(Dir) - P)
    else
      Base := Dir;

    {{ 如果最后一段不是应用目录名，就追加 }}
    if CompareText(Base, 'SSBVoiceProcessor') <> 0 then
      WizardForm.DirEdit.Text := Dir + '\\SSBVoiceProcessor';
  end;
end;

{{ ============================================================
   卸载：完成后询问是否删除设置和预设数据
   数据目录：%APPDATA%\\SSBVoiceProcessor\\
   注意：MsgBox 调用必须写在一行，否则 #13#10 会被 Inno
         当成预处理指令，报 Unknown preprocessor directive
   ============================================================ }}
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
  MsgResult: Integer;
  MsgText: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    DataDir := ExpandConstant('{{userappdata}}\\{DATA_FOLDER}');

    if DirExists(DataDir) then
    begin
      MsgText := '是否同时删除所有设置和预设数据？' + #13#10 + #13#10 + '位置：' + DataDir + #13#10 + #13#10 + '选择「是」将彻底清除（包括所有已保存的预设、背景图片设置、界面偏好等）。' + #13#10 + '选择「否」将保留数据，方便下次重装后继续使用。';
      MsgResult := MsgBox(MsgText, mbConfirmation, MB_YESNO);

      if MsgResult = IDYES then
        DelTree(DataDir, True, True, True);
    end;
  end;
end;
'''

    return iss


def main():
    OUT_DIR.mkdir(exist_ok=True)

    print("=" * 60)
    print("SSB Voice Processor 安装包生成器")
    print("=" * 60)
    print()

    has_chinese, isl_name = check_chinese_isl()
    if has_chinese:
        print(f"[0/4] 找到中文语言包: {isl_name}")
    else:
        print(f"[0/4] 未找到中文语言包，安装向导将使用英文界面")
        print(f"      下载：https://gitee.com/mirrors_jrsoftware/issrc/raw/main/Files/Languages/Unofficial/ChineseSimplified.isl")
        print()

    exe_path = find_exe()
    if exe_path is None:
        print("[X] 找不到编译好的 exe。请先在 build 目录里编译")
        return
    exe_name = exe_path.name
    print(f"[1/4] 找到 exe: {to_rel(exe_path, PROJECT)}")
    print(f"      实际文件名: {exe_name}")

    icon_ico = RES_DIR / "icon.ico"
    if icon_ico.exists():
        print(f"[2/4] 找到图标: {to_rel(icon_ico, PROJECT)}")
    else:
        print(f"[2/4] 未找到 icon.ico（使用默认图标）")
        icon_ico = None

    icon_png = RES_DIR / "icon.png"
    if icon_png.exists():
        big   = generate_wizard_big(icon_png)
        small = generate_wizard_small(icon_png)
        big_path   = HERE / "wizard_image.bmp"
        small_path = HERE / "wizard_small.bmp"
        big.save(big_path, "BMP")
        small.save(small_path, "BMP")
        print(f"[3/4] 生成向导图片:")
        print(f"      {big_path.name}   ({big.size[0]}x{big.size[1]})")
        print(f"      {small_path.name} ({small.size[0]}x{small.size[1]})")
    else:
        print(f"[3/4] 未找到 icon.png，跳过向导图片")
        big_path = small_path = None

    iss_content = build_iss(exe_path, exe_name, icon_ico,
                            big_path, small_path,
                            has_chinese, isl_name)
    iss_path = HERE / "installer.iss"
    iss_path.write_text(iss_content, encoding="utf-8-sig")
    print(f"[4/4] 生成 {iss_path.name}")

    print()
    print("下一步：编译安装包")
    print(f'    "{INNO_DIR / "ISCC.exe"}" "{iss_path}"')
    print()
    print(f"生成的安装包：{OUT_DIR}\\SSBVoiceProcessor_Setup_{APP_VERSION}.exe")
    print()


if __name__ == "__main__":
    main()