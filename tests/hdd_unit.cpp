// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/hdd.hpp"
#include "xhc/csb.hpp"
#include "xhc/ui_layout.hpp"
#include <iostream>
using namespace xhc;
namespace {
int count=0;
void ok(bool b,const char* what){++count;require(b,std::string("HDD unit: ")+what);}
template<class F>void bad(F f,const char* what){bool caught=false;try{f();}catch(const Error&){caught=true;}ok(caught,what);}
Node file(const std::string& name){Node n;n.name=name;n.attributes=0x21;n.length=3;n.digest="ABC";n.chain={5};n.times={1,2,3,4,5,6};return n;}
Node directory(const std::string& name){Node n;n.name=name;n.attributes=0x10;return n;}
}
int main(){try{
    auto base=fs::temp_directory_path()/("hdd-unit-"+random_token());fs::create_directory(base);write_text_new(base/"source.raw","source");
    hdd::Catalog c;c.source=base/"source.raw";c.fingerprint="fixture";
    for(const auto& p:kLayout){Volume v;v.layout=p;v.root=directory(std::string(1,p.letter));c.volumes.push_back(v);}
    c.volumes[1].root.children={file("a.bin"),directory("Dest"),directory("Keep")};
    hdd::Editor e;e.load(c);
    ok(hdd::Path::parse("e:\\Dest\\").str()=="E:/Dest","absolute normalized path");
    for(auto p:{"E:","F:/","/etc","E:/..","E:/a/../b","E://x","E:/NUL","E:/a."})bad([&]{hdd::Path::parse(p);},"unsafe path");
    auto root=hdd::Path::parse("E:/"),a=hdd::Path::parse("E:/a.bin"),dest=hdd::Path::parse("E:/Dest");
    ok(hdd::lookup(e.volumes(),hdd::Path::parse("e:/A.BIN")).length==3,"case-insensitive lookup");
    bad([&]{e.remove(root);},"delete root");bad([&]{e.rename(root,"Renamed");},"rename root");
    e.new_folder(dest,"New");ok(e.dirty(),"new directory pending");auto generation=e.generation();
    bad([&]{e.new_folder(dest,"new");},"case collision");ok(e.generation()==generation,"failed operation no mutation");
    e.rename(a,"A.BIN");ok(hdd::lookup(e.volumes(),a).name=="A.BIN","case-only rename");
    e.copy_move(a,dest,false);ok(hdd::lookup(e.volumes(),dest.child("a.bin")).digest=="ABC","copy content digest");
    ok(hdd::lookup(e.volumes(),dest.child("a.bin")).times==file("a.bin").times,"copy timestamps");
    bad([&]{e.copy_move(a,dest,false);},"copy overwrite refused");
    e.copy_move(dest.child("a.bin"),root.child("Keep"),true);bad([&]{hdd::lookup(e.volumes(),dest.child("a.bin"));},"moved old path absent");
    ok(hdd::lookup(e.volumes(),root.child("Keep").child("a.bin")).attributes==0x21,"move attributes");
    bad([&]{e.copy_move(dest,dest.child("New"),false);},"copy to descendant refused");
    bad([&]{e.copy_move(a,hdd::Path::parse("C:/"),false);},"cross-volume chain reuse refused");
    ok(hdd::lookup(e.original().volumes,a).name=="a.bin","original snapshot unchanged");e.discard();ok(!e.dirty(),"discard");
    auto imports=base/"import";fs::create_directory(imports);write_text_new(imports/"new.bin","abc");fs::create_directory(imports/"empty");
    Context ctx;e.import_paths(root,{imports},ctx);ok(hdd::lookup(e.volumes(),root.child("import").child("new.bin")).length==3,"recursive import");
    auto hash=hdd::lookup(e.volumes(),root.child("import").child("new.bin")).digest;ok(hash.size()==64,"import content SHA-256");
    bad([&]{e.import_paths(root,{imports},ctx);},"no implicit folder merge");
    write_text_new(base/"bad:name","bad");generation=e.generation();bad([&]{e.import_paths(dest,{base/"bad:name"},ctx);},"invalid name refused");ok(e.generation()==generation,"bad import preserves pending edits");
    e.replace_file(a,imports/"new.bin",ctx);ok(hdd::lookup(e.volumes(),a).digest==hash,"replacement preview hash");ok(hdd::lookup(e.volumes(),a).attributes==0x21,"replacement keeps attributes");
#ifndef _WIN32
    fs::create_symlink(imports/"new.bin",base/"link");bad([&]{e.import_paths(dest,{base/"link"},ctx);},"symlink import refused");
#endif
    ctx.cancelled=true;generation=e.generation();bad([&]{e.import_paths(dest,{imports},ctx);},"cancel import");ok(e.generation()==generation,"cancel does not apply partial imports");
    for(int width=820;width<=3000;width+=19)for(int left:{-100,190,226,400,2000})for(int middle:{-5,245,360,900,5000}) {
        auto p=ui::csb_panes(width,left,middle);ok(p.left>=190&&p.middle>=245&&p.songs_width>=300,"pane minimum widths");
        ok(p.splitter1_x>=12+p.left&&p.splitter1_x+10<=p.middle_x&&p.splitter2_x>=p.middle_x+p.middle&&p.splitter2_x+10<=p.songs_x,"separate divider hit targets");
        ok(p.songs_x+p.songs_width==width-12,"pane right boundary");
    }
    auto default_path=csb::default_ffmpeg_path();
    ok(default_path.parent_path()==executable_directory()/"tools","FFmpeg default EXE-relative tools (not current directory)");
#ifdef _WIN32
    ok(default_path.filename()=="ffmpeg.exe","Windows FFmpeg filename");
#else
    ok(default_path.filename()=="ffmpeg","POSIX FFmpeg filename");
#endif
    ok(csb::find_ffmpeg(imports/"new.bin")==absolute_safe(imports/"new.bin",true),"explicit helper override retained");
    auto tool_root=default_path.parent_path();bool had_dir=fs::exists(tool_root);if(!had_dir)fs::create_directory(tool_root);
    if(!fs::exists(default_path)){write_text_new(default_path,"test-only helper marker (never executed)");ok(csb::find_ffmpeg({})==absolute_safe(default_path,true),"tools helper lookup first");fs::remove(default_path);}
    if(!had_dir)fs::remove(tool_root);
    fs::remove_all(base);std::cout<<"PASS: "<<count<<" HDD editor/helper/pane checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
