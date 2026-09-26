// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/hdd.hpp"
#include <iostream>
using namespace xhc;
int main(int argc,char** argv){try{
    require(argc>=3,"source operation [output] [host folder]");Options disk;disk.source=fs::u8path(argv[1]);disk.offline_confirmed=true;
    Context ctx;ctx.log=[](const std::string& s){std::cerr<<s<<'\n';};auto catalog=hdd::read_catalog(disk,ctx);std::string op=argv[2];
    if(op=="list"){std::cout<<inventory_json(catalog.volumes)<<'\n';return 0;}
    require(argc>=4,"new output required");disk.destination=fs::u8path(argv[3]);
    if(op=="export"||op=="export-stale") {if(op=="export-stale")catalog.fingerprint="stale";auto out=hdd::export_items(catalog,disk,{hdd::Path::parse("E:/UDATA"),hdd::Path::parse("E:/readme.txt")},disk.destination,ctx);std::cout<<"{\"output\":"<<quote_json(utf8(out))<<"}\n";return 0;}
    if(op=="stale")catalog.fingerprint="stale";hdd::Editor e;e.load(catalog);auto path=[](const char* s){return hdd::Path::parse(s);};
    if(op=="simple")e.new_folder(path("E:/"),"Extra");
    else {e.new_folder(path("E:/"),"Work");e.rename(path("E:/readme.txt"),"renamed.txt");e.copy_move(path("E:/renamed.txt"),path("E:/Work"),false);e.new_folder(path("E:/Work"),"Moved");e.copy_move(path("E:/Work/renamed.txt"),path("E:/Work/Moved"),true);e.remove(path("E:/zero.bin"));}
    if(argc>=5){auto folder=fs::u8path(argv[4]);e.import_paths(path("E:/Work"),{folder},ctx);e.replace_file(path("E:/UDATA/12345678/save.bin"),folder/"new.bin",ctx);}
    if(op=="cancel")ctx.log=[&](const std::string& s){std::cerr<<s<<'\n';if(s.find("Backing up affected ORIGINAL")!=std::string::npos)ctx.cancelled.store(true);};
    if(op=="changed-import"){require(argc>=5,"host folder required");auto f=fs::u8path(argv[4])/"new.bin";fs::remove(f);write_text_new(f,"CHANGED");}
    hdd::SaveOptions o;o.disk=disk;o.disk.raw_output=op!="qcow2";o.folder_output=op=="folder";auto r=hdd::save(e,o,ctx);
    std::cout<<"{\"output\":"<<quote_json(utf8(r.disk.destination))<<",\"backup\":"<<quote_json(utf8(r.backup))<<",\"report\":"<<quote_json(utf8(r.disk.report))<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}}
