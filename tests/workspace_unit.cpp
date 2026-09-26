// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/workspace.hpp"
#include <iostream>
#include <fstream>
using namespace xhc;
namespace {
unsigned checks=0;
void check(bool b,const char* message){++checks;require(b,message);}
template<class Fn> void fails(Fn fn,const std::string& message){try{fn();}catch(const Error& e){check(std::string(e.what()).find(message)!=std::string::npos,e.what());return;}throw Error("Expected failure: "+message);}
}
int main(){try{
    using namespace ui;
    const Format formats[]={Format::Qcow2,Format::Folder,Format::Raw};
    for(auto src:formats)for(auto dst:formats)for(bool verify:{false,true}) {
        Settings s;s.source="source";s.output="output";s.source_format=src;s.output_format=dst;s.qemu_img="qemu";s.ffmpeg="ffmpeg";
        auto o=disk_options(s,verify);
        Mode expected=verify?Mode::Analyze:src==Format::Folder?(dst==Format::Folder?Mode::RebuildFolder:Mode::FolderToImage):(dst==Format::Folder?Mode::ImageToFolder:Mode::CleanImage);
        check(o.mode==expected,"9-way source/output dispatch");check(o.raw_output==(dst==Format::Raw),"RAW output option");
        check(o.source==s.source&&o.qemu_img==s.qemu_img,"shared source and helper copied");
        check(o.destination==(verify?fs::path{}:s.output),"Verify never consumes output");
        check(!o.offline_confirmed&&!o.acknowledge_snapshot_exclusion&&!o.confirm_snapshot_exclusion,"fresh operation has no inherited consent");
        check(o.expected_source==(src==Format::Qcow2?SourceKind::Qcow2:src==Format::Folder?SourceKind::Folder:SourceKind::Raw),"explicit source type");
    }
    for(unsigned dirty=0;dirty<4;++dirty)for(bool approve:{false,true}) {
        Workspace w;w.source("one",Format::Raw,false,false);w.output("destination",Format::Folder);w.helpers("qemu","ffmpeg");
        auto rev=w.revision();auto r=w.source("two",Format::Qcow2,dirty!=0,approve);
        const bool changed=!dirty||approve;
        check(r==(changed?SourceChange::Applied:SourceChange::NeedsDiscard),"both dirty-editor combinations require one decision");
        check(w.settings().source==(changed?"two":"one"),"declined change preserves source");
        check(w.revision()==rev+(changed?1:0),"revision changes only on committed source");
        check(w.settings().output=="destination"&&w.settings().qemu_img=="qemu","source edit does not lose unrelated settings");
    }
    Workspace w;w.source("raw",Format::Raw,false,false);w.output("dest",Format::Qcow2);w.helpers("qemu-a","ffmpeg-a");
    auto rev=w.revision();check(w.source("raw",Format::Raw,true,false)==SourceChange::Unchanged,"same source does not ask discard");
    check(w.source("raw",Format::Qcow2,true,false)==SourceChange::NeedsDiscard,"same path different kind invalidates");
    check(w.revision()==rev,"refused type change preserves revision");
    for(int cycle=0;cycle<100;++cycle) {
        auto ticket=w.begin();check(w.busy()&&w.active_job()==ticket.id,"one shared active job");
        fails([&]{w.begin();},"Another operation");
        check(w.source("other",Format::Folder,false,true)==SourceChange::Busy,"busy source change refused even with discard");
        check(!w.output("other-out",Format::Raw)&&!w.helpers("qemu-b","ffmpeg-b"),"busy output/helper changes refused");
        check(!w.finish(ticket.id+1)&&!w.finish(0)&&w.busy(),"stale completion cannot unlock another job");
        check(ticket.settings.source=="raw"&&ticket.settings.qemu_img=="qemu-a","frozen ticket");
        check(w.finish(ticket.id)&&!w.busy(),"correct completion releases workspace");
        check(!w.finish(ticket.id),"duplicate completion refused");
    }
    for(unsigned tab=0;tab<3;++tab)for(bool busy:{false,true})for(bool verify:{false,true})
        check(output_enabled(busy,tab,verify)==(!busy&&!(tab==0&&verify)),"Verify disables output only on converter tab");
    Context c;Options o;unsigned calls=0;o.confirm_snapshot_exclusion=[&](uint64_t n){++calls;check(n==3,"actual snapshot count passed");return true;};
    for(auto mode:{Mode::Analyze,Mode::CleanImage})for(bool ack:{false,true})for(uint64_t count:{uint64_t(0),uint64_t(3)}) {
        o.mode=mode;o.acknowledge_snapshot_exclusion=ack;auto before=calls;
        check(allow_snapshot_exclusion(o,count,c),"optional exclusion allowed when applicable");
        check(calls==before+((mode!=Mode::Analyze&&count&&!ack)?1u:0u),"prompt only actual unacknowledged snapshots on writes");
    }
    o.mode=Mode::CleanImage;o.acknowledge_snapshot_exclusion=false;o.confirm_snapshot_exclusion={};
    check(!allow_snapshot_exclusion(o,3,c),"CLI refuses snapshots without acknowledgment or UI callback");
    o.confirm_snapshot_exclusion=[](uint64_t){return false;};check(!allow_snapshot_exclusion(o,3,c),"No cancels without assuming permission");
    o.confirm_snapshot_exclusion=[&](uint64_t){c.cancelled=true;return true;};fails([&]{allow_snapshot_exclusion(o,3,c);},"Cancelled");c.cancelled=false;
    fs::path root=fs::temp_directory_path()/("xhc-workspace-"+random_token());fs::create_directory(root);
    struct Cleanup{fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}} cleanup{root};
    auto qc=root/"snapshot.qcow2",raw=root/"raw.img";
    Bytes h(104);h[0]='Q';h[1]='F';h[2]='I';h[3]=0xfb;h[7]=2;h[63]=3;
    {File f(qc,true,h.size());f.write(0,h.data(),h.size());f.flush();}
    {File f(raw,true,104);f.flush();}
    o=Options{};o.mode=Mode::CleanImage;o.source=qc;o.destination=root/"out.raw";o.raw_output=true;o.offline_confirmed=true;
    o.expected_source=SourceKind::Raw;fails([&]{convert(o,c);},"selected source format");
    o.source=raw;o.expected_source=SourceKind::Qcow2;fails([&]{convert(o,c);},"selected source format");
    o.source=qc;o.expected_source=SourceKind::Folder;fails([&]{convert(o,c);},"selected file/folder type");
    o.source=root;o.expected_source=SourceKind::Raw;fails([&]{convert(o,c);},"selected file/folder type");
    o.source=qc;o.expected_source=SourceKind::Qcow2;calls=0;
    o.confirm_snapshot_exclusion=[&](uint64_t n){++calls;check(n==3,"QCOW header drives real engine consent");return false;};
    fails([&]{convert(o,c);},"Acknowledge snapshot exclusion");check(calls==1,"real converter requests exactly once");
    check(!fs::exists(o.destination),"decline never publishes output");
    {File f(qc);check(f.read(0,h.size())==h,"source header unchanged after refusals");}
    std::cout<<"PASS: "<<checks<<" shared-workspace, type-admission and snapshot-consent checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
