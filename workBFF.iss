; workBFF 打包脚本（基于 omni_station.iss 改写）
; 打包内容：bin\RelWithDebInfo 下所有文件（去除 .pdb）
; 版本号从 version.txt 自动读取

#define MyAppVersion GetFileVersion(SourcePath + "\bin\RelWithDebInfo\workBFF.exe")
#define APPFAST "workBFF"
#define MyAppName "workBFF"
#define MyAppPublisher "hxf."
#define MyAppURL "https://www.github.com"
#define MyAppExeName "workBFF.exe"
; 输出安装包文件名（不含扩展），格式：workBFF_Setup_1.0.0.18
#define OutputBaseFilename APPFAST + "_Setup_" + MyAppVersion

[Setup]
; 注意: AppId 唯一标识此应用程序，请勿在其他安装程序中复用
AppId={{6f3a2d81-4b7c-4e9a-9d1e-8c5f2b0a6c47}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
VersionInfoVersion={#MyAppVersion}
VersionInfoTextVersion={#MyAppVersion}
;支持识别命令行指定安装路径
DefaultDirName={param:InstallPath|{autopf}\{#MyAppName}}
DisableProgramGroupPage=yes
PrivilegesRequiredOverridesAllowed=commandline
OutputDir=target
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile=.\icon.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin

[Languages]
Name: "ChineseSimple"; MessagesFile: "compiler:ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkablealone
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkablealone

[Files]
; 主程序
Source: "bin\RelWithDebInfo\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; 其余所有文件（递归子目录），去除 pdb 与 models 目录
Source: "bin\RelWithDebInfo\*"; Excludes: "*.pdb,models\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{autoprograms}\{#APPFAST}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#APPFAST}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Registry]
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string ; ValueName: {#MyAppName}; ValueData: {app}\{#MyAppExeName} ; Flags: uninsdeletevalue
; 卸载时删除程序运行时注册的 scheme 协议（等价于 unregisterScheme() 的 RegDeleteTreeW）
; dontcreatekey: 安装时不创建；uninsdeletekey: 卸载时删除整个键（含所有子键和值）
Root: HKCU; Subkey: "Software\Classes\workbff"; Flags: uninsdeletekey dontcreatekey

[Code]
#ifdef UNICODE
  #define AW "W"
#else
  #define AW "A"
#endif
function TaskKill(FileName: String):Integer;
var
  ResultCode: Integer;
begin
    Exec(ExpandConstant('taskkill.exe'), '/f /im ' + '"' + FileName + '"', '', SW_HIDE,
     ewWaitUntilTerminated, ResultCode);
    Result := ResultCode;
end;

// 安装时检测进程 并结束（主进程及各子进程）
function InitializeSetup(): Boolean;
var
  ret : Integer;
begin
  Result := True;

  ret := TaskKill('workBFF.exe');
  if (ret <> 0 )and( ret <> 128 )then
  begin
      Result := False;
      MsgBox('workBFF.exe 进程结束失败,code:'+IntToStr(ret), MBInformation, MB_OK);
  end;

end;

// 卸载时检测进程 并结束
function InitializeUninstall(): Boolean;
var
  ret : Integer;
begin
  Result := True;

  ret := TaskKill('workBFF.exe');
  if (ret <> 0 )and( ret <> 128 )then
  begin
      Result := False;
      MsgBox('workBFF.exe 进程结束失败,code:'+IntToStr(ret), MBInformation, MB_OK);
  end;

end;

// 覆盖安装前清理目标目录，确保不保留老版本DLL和其他文件
procedure CurStepChanged(CurStep: TSetupStep);
var
  AppDir: String;
begin
  if CurStep = ssInstall then
  begin
    AppDir := ExpandConstant('{app}');
    if DirExists(AppDir) then
      DelTree(AppDir, True, True, True);
  end;
end;

// 卸载时删除安装目录下产生的多余文件
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usDone then
  begin
      DelTree(ExpandConstant('{app}'),True,True,True);
  end;
end;
