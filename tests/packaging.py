#!/usr/bin/env python3
"""Static packaging/build checks for the current Xemu HDD Tools Windows Docker workflow.
These checks do not claim Docker or MinGW execution in the authoring environment.
"""
from pathlib import Path
import ast
import hashlib
import re
import struct

root=Path(__file__).resolve().parent.parent
count=0

def compact_code(text):
    return re.sub(r"\s+", "", str(text))

class CodeText(str):
    """Source text with whitespace-insensitive containment for static guards."""
    def __contains__(self, needle):
        return super().__contains__(needle) or compact_code(needle) in compact_code(self)

    def index(self, needle, *args):
        return compact_code(self).index(compact_code(needle), *args)

def code(path):
    return CodeText((root/path).read_text())

def check(ok,what):
    global count
    assert ok,what
    count+=1

required=[
    '.clang-format','Dockerfile','CMakeLists.txt','Build-Windows-Docker.bat','Build-Windows-Docker.ps1',
    'cmake/mingw64.cmake','src/gui_win32.cpp','src/app.rc','src/app.manifest','src/xemu-hdd-tools.ico','assets/xemu-hdd-tools-icon.png',
    'HELP.md','Install_Info.md','THIRD_PARTY_NOTICES.md','VALIDATION.md','LICENSE','BUILD-REVISION.txt','tools/package_qemu.py'
]
for path in required: check((root/path).is_file(),'missing '+path)
for path in list((root/'tests').rglob('*.py'))+list((root/'tools').glob('*.py')):
    ast.parse(path.read_text()); count+=1

# Keep tools/ reserved for helpers used by the actual product/build package.
tool_scripts=sorted(path.name for path in (root/'tools').glob('*.py'))
check(tool_scripts==['package_qemu.py'],'tools/ contains only the Docker/QEMU packaging helper')
for support in ['check_sanitizers.py','normalize_build_timestamps.py','wine_smoke.py']:
    check((root/'tests'/'support'/support).is_file(),'test support helper present: '+support)

docker=(root/'Dockerfile').read_text()
ps1=(root/'Build-Windows-Docker.ps1').read_text()
cmake=(root/'CMakeLists.txt').read_text()
help_text=' '.join((root/'HELP.md').read_text().split())
install_text=' '.join((root/'Install_Info.md').read_text().split())
notices=(root/'THIRD_PARTY_NOTICES.md').read_text()

# User Docker path: compile/export Windows only.
check('g++-mingw-w64-x86-64-posix' in docker,'MinGW C++ compiler required')
check('binutils-mingw-w64-x86-64' in docker,'MinGW resource/binutils required')
check('-DCMAKE_TOOLCHAIN_FILE=cmake/mingw64.cmake' in docker,'Windows CMake toolchain selected')
check('-DXHC_BUILD_TESTS=OFF' in docker,'end-user Docker build does not build regression programs')
check('Xemu-HDD-Tools.exe' in docker and 'xemu-hdd-convert.exe' in docker,'both Windows applications exported')
check('FROM scratch AS windows-artifacts' in docker,'artifact-only export stage exists')
check("touch -d '2000-01-01 00:00:00 UTC'" in docker,'Docker copy timestamp normalization retained')
check('-Werror' in cmake,'strict compiler warnings retained')

for forbidden in ['wine64','wine_smoke.py','clang-18','libclang-rt','XHC_SANITIZERS=ON',
                  'XHC_REQUIRE_QEMU_TESTS=ON','qemu-utils']:
    check(forbidden not in docker,'Docker user build must not contain '+forbidden)

# QEMU is packaged but never executed during Docker build.
check('qemu-w64-setup-20260811.exe' in docker,'pinned QEMU Windows installer')
check('5bcf9eed634e8575a37b74f445af41a2fe4106da512d0c30c368301d4c105037fdfab40a5287367a28a957624cddebbc8c07e16c88ab6634f554cdf3d16bf543' in docker,'published QEMU installer SHA-512 pinned')
check('sha512sum -c -' in docker,'QEMU installer checksum verified')
check('package_qemu.py' in docker and 'COPY --from=qemu-helper /qemu-tools/' in docker,'qemu-img dependency closure packaged')
for forbidden_exec in ['wine ', 'wine64', 'qemu-img --version', 'qemu-img check', 'qemu-img convert']:
    check(forbidden_exec not in docker,'Docker must not execute helper: '+forbidden_exec)

check('Build-Windows-Docker-' in ps1 and 'Add-Content -LiteralPath $log' in ps1,'normal build saves a log')
check('--target windows-artifacts' in ps1,'PowerShell exports Windows artifact stage')
check('WINDOWS BUILD: PASS' in ps1,'clear successful Windows build status')
check('Bundled qemu-img helper:' in ps1,'bundled helper location reported')
check('does not execute QEMU/Wine' in ps1,'simple build scope stated')

# Runtime helper remains external/selectable.
process=code('src/process.cpp')
cli=code('src/cli.cpp')
gui=code('src/gui_win32.cpp')
check('qemu-img.exe' in process,'Windows qemu-img discovery')
check('--qemu-img PATH' in cli,'CLI explicit qemu-img option')
workspace_gui=code('src/gui_workspace_win32.cpp')
check('qemu-img executable (shared by all tabs)' in workspace_gui,'global GUI qemu-img selector')
check('tools\\qemu-img.exe' in help_text and 'tools\\ffmpeg.exe' in help_text,'HELP documents only the two runtime helper paths')
check('does **not** perform native Windows runtime validation' in install_text,'Install_Info explains Docker validation boundary')

# Source hygiene: the public tree is formatted and free of development scratch notes.
style=(root/'.clang-format').read_text()
check('BasedOnStyle: LLVM' in style and 'SortIncludes: Never' in style and 'SortUsingDeclarations: Never' in style,
      'checked-in C++ formatting policy preserves dependency order')
source_files=[p for base in (root/'src',root/'include') for p in base.rglob('*')
              if p.is_file() and p.suffix.lower() in {'.cpp','.cc','.h','.hpp','.hh'}]
for path in source_files:
    text=path.read_text()
    check(not re.search(r'\b(?:TODO|FIXME|HACK|XXX)\b',text),f'no development scratch marker: {path.relative_to(root)}')
    run=maximum=0
    for line in text.splitlines():
        if line.lstrip().startswith('//'):
            run+=1; maximum=max(maximum,run)
        else:
            run=0
    check(maximum<=8,f'no oversized line-comment block: {path.relative_to(root)}')

# Windows path and macro portability protections.
def guarded_macro(path, macro):
    lines=(root/path).read_text().splitlines()
    hits=[i for i,line in enumerate(lines) if line.strip()==f'#define {macro}']
    check(bool(hits),f'{path}: expected {macro}')
    for i in hits:
        prev=[line.strip() for line in lines[max(0,i-3):i] if line.strip()]
        check(f'#ifndef {macro}' in prev,f'{path}: unguarded {macro}')
for src in ['src/common.cpp','src/process.cpp','src/cli.cpp','src/gui_win32.cpp']:
    text=(root/src).read_text()
    if 'NOMINMAX' in text: guarded_macro(src,'NOMINMAX')
    if 'WIN32_LEAN_AND_MEAN' in text: guarded_macro(src,'WIN32_LEAN_AND_MEAN')
common=code('src/common.cpp')
check('fs::path cur=a.root_path();' in common and 'a.relative_path()' in common,'Windows root path handling')
check('for(const auto& component:a){' not in common,'do not split Windows drive root')

# Version identity is internally consistent.
check('VERSION 0.6.1' in cmake,'CMake version 0.6.1')
check('Xemu HDD Tools 0.6b' in cli,'CLI brand/version 0.6b')
check('Xemu HDD Tools 0.6b' in gui,'GUI brand/version 0.6b')
check('version="0.6.1.0"' in (root/'src/app.manifest').read_text(),'manifest version 0.6.1')
check('Build revision: 0.6b' in (root/'BUILD-REVISION.txt').read_text(),'build revision 0.6b')

# CSB is standalone and backup ownership is explicit.
csb=code('src/csb/csb.cpp')
csb_gui=code('src/gui_csb_win32.cpp')
check('src/gui_csb_win32.cpp' in cmake and 'src/csb/csb.cpp' in cmake,'CSB page/backend compiled')
check('WC_TABCONTROLW' in gui and 'Custom Soundtrack Builder' in gui,'native Win32 tabs')
check('Backups' in csb and 'CSB-' in csb and 'database.bak' in csb,'dedicated host backup transaction')
check('old_folder' in csb and 'backup_node(read, vol, *old_folder' in csb,'affected folder backup')
check('backup-verified.txt' in csb and 'hash_file' in csb,'backup verification markers')
check('o.workspace_parent = backup' in csb,'temporary CSB work stays in backup transaction')
check('complete.json' in csb and 'csb-failure.txt' in csb,'completion/failure records')
check('fs::remove_all(backup)' not in csb and 'music.CSB-stage-' not in csb,'no silent backup deletion or live music staging')
check('e.id >= c.metadata.next_soundtrack_id' in csb,'new soundtrack counter checked in backend')
check('ffmpeg' in csb and 'wmav2' in csb and '44100' in csb,'external TEST12 audio profile')
check('PostMessageW' in csb_gui and 'std::thread' in csb_gui,'CSB work outside UI loop')
check('LVN_BEGINDRAG' in csb_gui and 'WM_LBUTTONUP' in csb_gui and 'drag_generation' in csb_gui,'delivered generation-checked row drops')
check('NM_CUSTOMDRAW' in csb_gui and 'CDDS_POSTPAINT' in csb_gui and 'LVM_SETINSERTMARK,0' not in csb_gui,'report-view drag marker is custom-painted, not icon-view-only API')
check('VK_ESCAPE' in csb_gui and 'end_drag' in csb_gui,'drag cancellation')
check('app.shared.state.busy()' in gui and 'BeginWorkspaceJob' in csb_gui,'all tabs share workspace job exclusion')
check('id==IDCANCEL' in gui and 'WM_KEYDOWN,VK_ESCAPE' in gui,'Escape translated by IsDialogMessage reaches the CSB drag handler')
check('ListView_HitTest(list,&hit)!=row' in csb_gui,'context menu does not target a restored old selection')
check('alignas(DWORD) Template' in csb_gui,'dialog template has required DWORD alignment')
check('target_include_directories(XEMU-HDD-Converter PRIVATE tests/' not in cmake,'declaration facades absent from production includes')
for src in ['src/gui_csb_win32.cpp']:
    guarded_macro(src,'NOMINMAX');guarded_macro(src,'WIN32_LEAN_AND_MEAN')
check('FILEVERSION 0,6,1,0' in (root/'src/app.rc').read_text(),'numeric resource version matches runtime')

hdd=code('src/hdd/hdd.cpp')
hdd_gui=code('src/gui_hdd_win32.cpp')
progress=code('src/gui_progress_win32.hpp')
splitter=code('src/gui_splitter_win32.cpp')
check('HDD Directory' in gui and 'CreateHddPage' in gui,'third standalone HDD Directory tab')
check('app->shared.state.busy()' in gui and 'BeginWorkspaceJob' in hdd_gui,'HDD work participates in cross-tab exclusion')
check('app->active_tab==2' in gui and 'app->hdd_page,WM_DROPFILES' in gui,'HDD targeted drop routing')
check('default_ffmpeg_path()' in csb and 'tools' in csb,'EXE-relative default FFmpeg path')
check('app.shared.state.helpers(tools/L"qemu-img.exe",tools/L"ffmpeg.exe")' in gui,'EXE-relative defaults prefilled once in global workspace')
check('CreateXhcSplitter(p.window,Splitter1' in csb_gui and 'Splitter2' in csb_gui,'two real CSB splitter controls')
check('SetCapture(w)' in splitter and 'IDC_SIZEWE' in splitter and 'WM_CANCELMODE' in splitter,'splitter hit/capture/cancel handling')
check('~static_cast<LONG_PTR>(PBS_MARQUEE)' in progress and 'PBM_SETPOS' in progress and 'InvalidateRect' in progress,'idle progress style/position/repaint reset')
for source in (gui,csb_gui,hdd_gui):check('SetOperationProgress' in source,'all three pages share progress cleanup')
check('Backups' in hdd and 'HDD-' in hdd and 'backup_changed' in hdd,'affected original HDD backups')
check('o.workspace_parent=backup' in hdd and 'hdd-failure.txt' in hdd,'HDD failures retain dedicated recovery')
check('snapshot_fingerprint(vols,system)==editor.original().fingerprint' in hdd,'HDD stale-source refusal')
check('Source changed since REFRESH' in hdd and 'export_node' in hdd,'source-aware verified exports')
check('std::thread' in hdd_gui and 'WM_DROPFILES' in hdd_gui,'HDD import/export/saves queued away from UI')
check('C: System' in hdd_gui and 'E: Data' in hdd_gui and 'Z: Cache' in hdd_gui,'HDD partition tabs')
for name in ('Name','Type','Size (bytes)','Cluster','Modified','Attr'):check('L"'+name+'"' in hdd_gui,'HDD column '+name)
for source in ('src/gui_hdd_win32.cpp','src/gui_splitter_win32.cpp'):
    guarded_macro(source,'NOMINMAX');guarded_macro(source,'WIN32_LEAN_AND_MEAN')
# Shared workspace replaces duplicated per-page sources, output, consent and helpers.
workspace=code('src/workspace.cpp')
converter=code('src/converter.cpp')
for name in ['src/gui_workspace_win32.cpp','src/workspace.cpp','include/xhc/workspace.hpp',
             'tests/workspace_unit.cpp','tests/gui_workspace.cpp','tests/gui_shell.cpp']:
    check((root/name).is_file(),'shared workspace source/test exists: '+name)
check('src/gui_workspace_win32.cpp' in cmake and 'src/workspace.cpp' in cmake,'workspace compiled into correct targets')
check('SourceQcow' in gui and 'SourceFolder' in gui and 'SourceRaw' in gui,'three global source radios')
check('OutputQcow' in gui and 'OutputFolder' in gui and 'OutputRaw' in gui,'three global output radios')
check('BS_AUTORADIOBUTTON|WS_GROUP|WS_TABSTOP' in gui,'Windows radio groups have explicit boundaries')
check('ConvertRadio' in gui and 'VerifyRadio' in gui and 'CBS_DROPDOWNLIST' not in gui,'converter has Convert/Verify radios only')
check('PickWorkspaceFolder(app.window,false)' in gui and 'PickWorkspaceFile(app.window,kind,false)' in gui,'type-aware single source Browse')
check('PickWorkspaceFolder(app.window,true)' in gui and 'PickWorkspaceFile(app.window,kind,true)' in gui,'type-aware new output Browse')
check('CsbPageDirty' in gui and 'HddPageDirty' in gui and 'NeedsDiscard' in gui,'source change checks both dirty editors')
check('CsbPageInvalidate' in gui and 'HddPageInvalidate' in gui,'committed source change invalidates both catalogs')
check('source_fields(app);return false;' in gui,'declined source change restores displayed selection')
check('active_ != job' in workspace and 'active_ = 0' in workspace,'stale completion cannot release a different operation')
check('const Settings& settings() const' in (root/'include/xhc/workspace.hpp').read_text(),'settings are read-only outside workspace admission')
check('active_tab == 0 && verify' in workspace,'Verify only disables output on converter tab')
for page in (csb_gui,hdd_gui):
    check('ui::disk_options(p.shared->state.settings(),false)' in page,'editor uses global source and helper settings')
    check('p.shared->state.settings().output_format' in page,'editor uses global output format')
    check('p.shared->state.busy()' in page and 'FinishWorkspaceJob' in page,'editor respects global job lifetime')
    check('ConfirmWorkspaceOperation' in page and 'ConnectSnapshotQuestion' in page,'editor gets shared confirmation and conditional snapshot prompt')
    check('Source selection unchanged' in page,'save never silently retargets the global source')
    for retired in ['enum Id { Source','Show helper paths','HelperF,','HelperQ,','Snapshots,','Offline,']:
        check(retired not in page,'no duplicated global control: '+retired)
check('CreateMenu()' in gui and 'HelpersMenu,L"Helper locations..."' in gui,'one global menu-bar helper dialog')
check('Restore tools-folder defaults' in workspace_gui,'helper restore defaults is explicit')
check('d->offline=false;d->exclude=false;' in workspace_gui,'each dialog resets both consent choices')
check('if(!d->offline)return TRUE' in workspace_gui,'Enter cannot bypass offline acknowledgment')
check('EnableWindow(ok,d.helpers?TRUE:FALSE)' in workspace_gui,'confirmation starts with Proceed disabled')
check('o.offline_confirmed=false;o.acknowledge_snapshot_exclusion=false;' in workspace_gui,'caller cannot inherit previous consent')
check('alignas(DWORD) Template' in workspace_gui,'global dialog template is DWORD aligned')
check('snapshot' in workspace_gui and 'MB_DEFBUTTON2' in workspace_gui,'snapshot exclusion defaults to No')
check('s.state.active_job()!=q.job' in workspace_gui,'stale snapshot callback refused')
check('ctx.check();' in converter and 'confirm_snapshot_exclusion(snapshots)' in converter,'snapshot authorization checks cancellation')
check('o.mode==Mode::Analyze || !snapshots || o.acknowledge_snapshot_exclusion' in converter,'no-snapshot/read-only operations need no exclusion consent')
check('o.expected_source' in converter and 'selected source format' in converter,'chosen source type checked against actual input')
for page in ('src/gui_workspace_win32.cpp',):
    guarded_macro(page,'NOMINMAX');guarded_macro(page,'WIN32_LEAN_AND_MEAN')
check('CsbPageKeepOpen' in gui and 'HddPageKeepOpen' in gui,'veto from another dirty tab clears prior idle-close permission')
check('return !dirty(p)||MessageBoxW' in csb_gui,'uncommitted name text participates in CSB discard protection')
check('if(p.populating||p.busy||p.shared->state.busy())return;' in csb_gui,"hidden CSB editor notifications respect another tab's job")
check('n->idFrom==Entries&&!p->busy&&!p->shared->state.busy()' in hdd_gui,"hidden HDD notifications respect another tab's job")
# Appearance changes may not mutate disk/editor/job state or bypass native input.
theme=code('src/gui_theme_win32.cpp')
theme_palette=code('include/xhc/theme.hpp')
for path in ('src/gui_theme_win32.cpp','src/gui_theme_win32.hpp','include/xhc/theme.hpp',
             'tests/theme_unit.cpp','tests/gui_theme.cpp'):
    check((root/path).is_file(),'theme source/documentation exists: '+path)
check('ThemeDefaultMenu' in gui and 'ThemeDarkMenu' in gui and 'ThemeXboxMenu' in gui,'three global theme commands')
check('Default (White)' in gui and 'ark Mode' in gui and 'Xbox Mode' in gui,'requested theme menu labels')
check('CheckMenuRadioItem(app.theme_menu' in gui,'one theme selected using submenu command IDs')
check(gui.index('id>=ThemeDefaultMenu') < gui.index('if(app->shared.state.busy())return 0;',gui.index('case WM_COMMAND')),'theme commands available without disturbing busy operation')
check('src/gui_theme_win32.cpp' in cmake and 'uxtheme dwmapi advapi32' in cmake,'theme links only documented standard Windows libraries')
check('Default=0' in theme_palette.replace(' ','') and '0xFFFFFF' in theme_palette,'white mode is initial default')
check('0x9BF00B' in theme_palette,'Xbox lime accent')
check('SetWindowSubclass' in theme and 'DefSubclassProc' in theme and 'RemoveWindowSubclass' in theme,'documented subclass lifetime and native input delegation')
check('GWLP_USERDATA' not in theme and 'SetWindowLongPtrW' not in theme,'theme does not replace controller state or control types')
check('prior|CDRF_NOTIFYITEMDRAW|CDRF_NOTIFYPOSTPAINT' in theme,'list custom drawing retains item/postpaint callbacks')
check('fill_list_empty_area' in theme and 's->kind==Kind::List?field():background()' in theme,'dark themes repaint empty list interiors')
check('s->kind==Kind::Panel||s->kind==Kind::Button' in theme,'dark themes repaint complete app-owned page surfaces')
check('prior|CDRF_NOTIFYITEMDRAW|CDRF_NOTIFYPOSTPAINT' in theme and 'GuiThemeAccentBrush' in csb_gui,'CSB postpaint insertion marker')
check('RegGetValueW' in theme and 'RegSetValueExW' in theme and 'RegCloseKey' in theme,'per-user theme persistence')
check('HKEY_CURRENT_USER' in theme and 'HKEY_LOCAL_MACHINE' not in theme,'no machine-wide appearance preference')
check('selected=Theme::Default' in theme and 'valid_theme(value)' in theme,'missing or invalid preference falls back to white')
check('return result==ERROR_SUCCESS' in theme and 'this session' in gui,'preference failure reported without blocking session theme')
check('SPI_GETHIGHCONTRAST' in theme and 'if(high_contrast)return DefSubclassProc' in theme,'high-contrast native override')
check('WM_NCDESTROY' in theme and 'DeleteObject' in theme and 'ShutdownGuiTheme' in gui,'GDI and window lifetime cleanup')
check('WM_PRINTCLIENT' in theme and 'WM_PAINT' in theme,'native paint and print-client paths')
check('BS_AUTORADIOBUTTON' in theme and 'BM_GETCHECK' in theme,'radio and checkbox paint reads native state')
check('PBM_SETMARQUEE' in theme and 'KillTimer' in theme and 's.marquee' in theme,'progress timer follows original operation state')
check('DrawFocusRect' in theme and 'foreground(false)' in theme,'focus and disabled appearance handled')
for path in ('src/gui_win32.cpp','src/gui_workspace_win32.cpp','src/gui_csb_win32.cpp','src/gui_hdd_win32.cpp'):
    check('ApplyGuiTheme' in code(path),'root or application-owned dialogs inherit theme: '+path)
for page in (csb_gui,hdd_gui):
    check('TrackGuiThemePopup' in page,'context-menu lifetime wrapper used')
for prohibited in ('CreateProcess','run_process','qemu-img','ffmpeg','fs::remove',
                   'convert(','.source(','.output(','.invalidate(', 'MakeIntResource'):
    check(prohibited not in theme,'appearance module has no data/backend side effect: '+prohibited)
check('GetProcAddress' not in theme and 'MAKEINTRESOURCE' not in theme,'no undocumented dark-mode ordinals')
check('ThemeDefaultMenu' not in workspace and 'SelectGuiTheme' not in converter,'theme independent of operation admission and converter')
for path in ('src/gui_theme_win32.cpp',):
    guarded_macro(path,'NOMINMAX');guarded_macro(path,'WIN32_LEAN_AND_MEAN')
check('101 ICON "xemu-hdd-tools.ico"' in (root/'src/app.rc').read_text(),'application icon embedded as resource 101')
icon_bytes=(root/'src/xemu-hdd-tools.ico').read_bytes()
reserved,icon_type,icon_count=struct.unpack_from('<HHH',icon_bytes,0)
check((reserved,icon_type)==(0,1) and icon_count==10,'application ICO has ten valid image entries')
icon_sizes=[]
for i in range(icon_count):
    width,height=struct.unpack_from('<BB',icon_bytes,6+i*16)
    icon_sizes.append((256 if width==0 else width,256 if height==0 else height))
check(sorted(icon_sizes)==[(16,16),(20,20),(24,24),(32,32),(40,40),(48,48),(64,64),(96,96),(128,128),(256,256)],'application ICO contains all Windows display sizes')
check(hashlib.sha256(icon_bytes).hexdigest().upper()=='D477EDC65FC1D2E09BEDCEC3CF09BB982266C328596A2BA627D0EEA35E4D8D56','application ICO is the verified transparent-edge resource')
check('MAKEINTRESOURCEW(kAppIconResource)' in gui and 'cls.hIconSm=cls.hIcon' in gui,'main window uses embedded large/small icon')
check('OUTPUT_NAME "Xemu-HDD-Tools"' in cmake,'main GUI output filename is Xemu-HDD-Tools')
check('ProductName", "Xemu HDD Tools' in (root/'src/app.rc').read_text(),'Windows product metadata renamed')
check('\\"program\\":\\"Xemu HDD Tools\\"' in converter and '\\"version\\":\\"0.6b\\"' in converter,'receipt identity')

# Native tab sizing and themed-surface coverage.
root_gui=code('src/gui_win32.cpp')
hdd_gui=code('src/gui_hdd_win32.cpp')
check('TCS_FIXEDWIDTH' in root_gui and 'TCM_SETITEMSIZE' in root_gui and '220' in root_gui,'main tabs use full-label fixed width')
check('TCS_FIXEDWIDTH' in hdd_gui and 'TCM_SETITEMSIZE' in hdd_gui and '122' in hdd_gui,'HDD partition tabs use full-label fixed width')

# Current release-package documentation boundary.
check(not (root/'docs').exists(),'legacy docs directory is absent')
check(not (root/'validation').exists(),'historical validation directory is absent')
check(not (root/'third_party').exists(),'third_party folder replaced by one root notices file')
check(not (root/'README.md').exists(),'mixed README removed in favor of HELP and Install_Info')
check(not (root/'tools/README-WINDOWS-QEMU.txt').exists(),'old helper README is absent')
help_raw=(root/'HELP.md').read_text()
install_raw=(root/'Install_Info.md').read_text()
for heading in ('# Windows GUI','# HDD Converter','# Custom Soundtrack Builder','# HDD Directory','# Command-line converter','# Recovery and source protection'):
    check(heading in help_raw,'HELP section: '+heading)
check('# Building the Windows programs' not in help_raw and 'Docker Desktop running' not in help_raw,'HELP excludes build/install instructions')
check('tools\\qemu-img.exe' in help_raw and 'tools\\ffmpeg.exe' in help_raw,'HELP explains required helper files')
for heading in ('# Xemu HDD Tools 0.6b — Install and Build Information','## Requirements','## Build the Windows package','## qemu-img setup','## FFmpeg setup','## Developer validation'):
    check(heading in install_raw,'Install_Info section: '+heading)
check('Build-Windows-Docker.bat' in install_raw,'Install_Info contains Windows Docker build command')
check('QEMU / qemu-img' in notices and 'TEST12 compatibility provenance' in notices and 'FFmpeg' in notices,'single third-party notices file preserves provenance')
check('cp HELP.md Install_Info.md THIRD_PARTY_NOTICES.md VALIDATION.md LICENSE BUILD-REVISION.txt' in docker,'Windows package ships split help/install/notices documents')
validation=(root/'VALIDATION.md').read_text()
check(validation.startswith('# Xemu HDD Tools 0.6b — Validation') and 'validation record for the current Xemu HDD Tools 0.6b source release' in validation,'single current-version validation report')
print('PASS:',count,'Xemu HDD Tools v0.6b current-release packaging checks')
