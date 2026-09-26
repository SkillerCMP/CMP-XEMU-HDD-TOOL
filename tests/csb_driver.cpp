// SPDX-License-Identifier: GPL-2.0-or-later
// Integration driver; not exported or compiled by the end-user Docker build.
#include "xhc/csb.hpp"
#include <iostream>
using namespace xhc;
int main(int argc,char** argv){try{
    require(argc>=3,"Usage: csb-driver source operation [output] [audio]");
    Options options;options.source=fs::u8path(argv[1]);options.offline_confirmed=true;
    Context context;context.log=[](const std::string& s){std::cerr<<s<<'\n';};
    auto c=csb::read_catalog(options,context);std::string operation=argv[2];
    if(operation=="list"){
        std::cout<<"{\"fingerprint\":"<<quote_json(c.fingerprint)<<",\"writable\":"<<(c.writable?"true":"false")<<",\"next_song_id\":"<<c.metadata.next_song_id<<",\"next_soundtrack_id\":"<<c.metadata.next_soundtrack_id<<",\"soundtracks\":[";
        for(size_t i=0;i<c.soundtracks.size();++i){if(i)std::cout<<',';const auto& s=c.soundtracks[i];std::cout<<"{\"id\":"<<s.record.soundtrack_id<<",\"name\":"<<quote_json(s.record.name)<<",\"orphan\":"<<(s.orphan?"true":"false")<<",\"songs\":[";
            for(size_t j=0;j<s.record.tracks.size();++j){if(j)std::cout<<',';const auto& t=s.record.tracks[j];std::cout<<"{\"id\":"<<t.song_id<<",\"title\":"<<quote_json(t.title)<<",\"duration\":"<<t.duration_ms<<'}';}std::cout<<"]}";}
        std::cout<<"]}\n";return 0;
    }
    require(argc>=4,"An output is required.");csb::Editor editor;editor.set_catalog(c);
    if((operation=="create"||operation=="invalid-id"))editor.new_soundtrack();else editor.select(c.soundtracks.front().record.soundtrack_id);
    if(operation=="reorder"||operation=="folder"){
        require(editor.edit().rows.size()>1,"Need multiple fixture songs.");editor.rename_soundtrack("Edited Soundtrack");editor.rename_track(0,"Renamed first");editor.reorder(0,editor.edit().rows.size(),editor.generation());editor.remove_track(0);
    }
    if(operation=="add"||operation=="create"||operation=="bad-ffmpeg") {require(argc>=5,"Audio is required.");editor.add_files({fs::u8path(argv[4])});}
    csb::SaveOptions save;save.disk=options;save.disk.destination=fs::u8path(argv[3]);save.disk.raw_output=operation!="qcow2";save.folder_output=operation=="folder";
    if(operation=="bad-ffmpeg")save.ffmpeg="/nonexistent/ffmpeg";
    if(operation=="stale")c.fingerprint="STALE";
    if(operation=="cancel")context.log=[&](const std::string& s){std::cerr<<s<<'\n';if(s.find("Backing up the existing database")!=std::string::npos)context.cancelled.store(true);};
    auto edit=editor.edit();if(operation=="invalid-id")edit.id=2;edit.delete_soundtrack=operation=="delete";
    auto result=csb::save(c,edit,save,context);std::cout<<"{\"output\":"<<quote_json(utf8(result.disk.destination))<<",\"backup\":"<<quote_json(utf8(result.backup))<<",\"report\":"<<quote_json(utf8(result.disk.report))<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}}
