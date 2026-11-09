; ============================================================================
;  TOIRE-2A - Windows VST3 installer (Inno Setup 6)
;
;  Build with:
;    "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" installer\TOIRE-2A.iss
;  or, preferably, through the wrapper that also stages the payload:
;    powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
;
;  这一份是从 TOREI-EQ 的安装脚本改来的，那五条经验在下面保留：
;
;  1. VST3 插件是一个 BUNDLE（文件夹），不是单个文件。整个 Contents/ 都要装，
;     少一个 WebView2Loader.dll 或 webui 就会加载失败 / 界面白屏。
;
;  2. ArchitecturesInstallIn64BitMode 决定 {commoncf} 解析到 64 位 Common Files。
;     没有它，64 位插件会被装到 x86 目录，大多数 DAW 找不到。
;
;  3. WebView2 是硬依赖（界面就是一个网页）。Windows 11 自带，
;     老一点的 Windows 10 可能没有。这里内置 Evergreen 引导程序，
;     只在检测到缺失时才运行。
;
;  4. {app} 就是 bundle 目录本身，[Files] 装进 {app} 而不是硬编码 {commoncf}。
;     这样卸载程序会留在 bundle 内部，/DIR= 也能生效（可以安全地试装到临时目录）。
;
;  5. TOIRE-2A 的界面在 <bundle>\Contents\webui\index.html。
;     PluginEditor 的 findWebUiIndex() 会依次找
;       webui/index.html
;       Contents/webui/index.html      <- 安装后的位置
;       ../webui/index.html
;       ../../webui/index.html
;     所以 payload 必须是完整 bundle，且 webui 已经在 Contents 下 ——
;     build-installer.ps1 就是按这个结构暂存的。
; ============================================================================

#define AppName      "TOIRE-2A"
#define AppVersion   "1.2.0"
#define AppPublisher "TOIRE"
; JUCE 的 target 名决定了 bundle 文件夹名，必须与之完全一致。
#define BundleName   "TOIRE-2A.vst3"

[Setup]
AppId={{4ECAC824-4A73-4E51-9D68-CCC87218A0DF}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} VST3 installer
VersionInfoCompany={#AppPublisher}

; 64 位模式下 {commoncf} = C:\Program Files\Common Files
DefaultDirName={commoncf}\VST3\{#BundleName}
DisableDirPage=yes
DisableProgramGroupPage=yes

; 共享的 VST3 目录是机器级的，需要管理员权限。
; PrivilegesRequiredOverridesAllowed 让命令行可以覆盖成当前用户安装，
; 这样可以把包试装到临时目录而不影响正式安装。
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763

Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; 提示用户关闭正在占用插件的程序，但绝不自动重启 DAW
CloseApplications=yes
RestartApplications=no

OutputDir=..\dist
OutputBaseFilename=TOIRE-2A-{#AppVersion}-win64
UninstallDisplayName={#AppName} {#AppVersion} (VST3)
UninstallDisplayIcon={app}\Contents\x86_64-win\{#BundleName}
AppComments=VST3 64-bit audio plugin for Windows. UI rendered with Microsoft Edge WebView2.

InfoBeforeFile=BEFORE-INSTALL.txt

[Languages]
; Inno 只认带 UTF-8 BOM 的 .isl 文件
#define DepsDir AddBackslash(SourcePath) + "deps"
#if FileExists(DepsDir + "\ChineseSimplified.isl")
Name: "chinesesimplified"; MessagesFile: "{#DepsDir}\ChineseSimplified.isl"
#else
Name: "english"; MessagesFile: "compiler:Default.isl"
#endif

[Files]
; 整个 bundle，作为一个文件夹放进共享 VST3 目录。
; payload\TOIRE-2A.vst3 里已经包含 Contents\... 和 Contents\webui\index.html。
Source: "payload\{#BundleName}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

; dontcopy：只有 PrepareToInstall 判断运行时缺失时才解包到 {tmp}
Source: "deps\MicrosoftEdgeWebview2Setup.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall dontcopy

[Run]
Filename: "{tmp}\MicrosoftEdgeWebview2Setup.exe"; Parameters: "/silent /install"; StatusMsg: "正在安装 Microsoft WebView2 运行时（插件界面依赖，需要联网，约几十秒）..."; Flags: waituntilterminated; Check: WebView2Missing

[UninstallDelete]
; bundle 文件夹会随卸载程序一起删掉，没有任何东西写到 {app} 之外。
; 用户设置（%APPDATA%\TOiRE-2A）刻意保留 —— 那是用户的东西，
; BEFORE-INSTALL.txt 里说明了怎么手动清除。
Type: filesandordirs; Name: "{app}"

[Code]
function KeyHasWebView2(const RootKey: Integer; const SubKey: String): Boolean;
var
  Version: String;
begin
  Result := RegQueryStringValue(RootKey, SubKey, 'pv', Version) and (Version <> '');
end;

function WebView2Missing: Boolean;
begin
  { Microsoft 把机器级运行时登记在 WOW6432Node 下，用户级登记在 HKCU\SOFTWARE 下。

    这里显式指定 HKLM32/HKLM64 而不是依赖安装程序当前模式：
    用 32 位视图去查 'SOFTWARE\WOW6432Node\...' 会被二次重定向，静默查不到，
    于是明明没装却报告"已存在" —— 那个用户的界面就会白屏。

    两种键位在每个视图下都试一遍，这样结果不依赖具体 Windows 版本的登记方式。 }
  Result := not (KeyHasWebView2(HKLM64, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}')
              or KeyHasWebView2(HKLM64, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}')
              or KeyHasWebView2(HKLM32, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}')
              or KeyHasWebView2(HKLM32, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}')
              or KeyHasWebView2(HKCU,   'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}')
              or KeyHasWebView2(HKCU,   'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  { dontcopy 的文件不会自动解包，需要在 [Run] 用到之前手动提取 }
  if WebView2Missing then
    ExtractTemporaryFile('MicrosoftEdgeWebview2Setup.exe');

  Result := '';
end;
