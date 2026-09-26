// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/csb.hpp"
#include <iostream>
#include <numeric>
using namespace xhc;
using namespace xhc::csb;
namespace {
size_t checks=0,cases=0;
void check(bool v,const char* why){++checks;require(v,why);}
template<class F> void refuses(F fn){bool refused=false;try{fn();}catch(const Error&){refused=true;}check(refused,"Expected refusal.");}
Catalog fixture(size_t count) {
    Catalog c;c.fingerprint="fixture";c.metadata.next_soundtrack_id=10;c.metadata.next_song_id=1000;
    Soundtrack s;s.record.soundtrack_id=4;s.record.name="Fixture";
    for(size_t i=0;i<count;++i){s.record.tracks.push_back({static_cast<uint16_t>(i),static_cast<uint32_t>((i+1)*1000),"Track "+std::to_string(i)});c.reserved_serials.insert(static_cast<uint16_t>(i));}
    c.soundtracks.push_back(s);return c;
}
void same_row(const Row& a,const Row& b){check(a.track.song_id==b.track.song_id&&a.track.duration_ms==b.track.duration_ms&&a.track.title==b.track.title&&a.local_file==b.local_file,"Reorder detached row properties.");}
}
int main(){fs::path temp=fs::temp_directory_path()/("xhc-csb-unit-"+random_token());try{
    fs::create_directory(temp);
    for(size_t count=1;count<=30;++count)for(size_t from=0;from<count;++from)for(size_t boundary=0;boundary<=count;++boundary){
        Editor e;e.set_catalog(fixture(count));e.select(4);auto expected=e.edit().rows;auto row=expected[from];expected.erase(expected.begin()+static_cast<std::ptrdiff_t>(from));auto to=boundary>from?boundary-1:boundary;expected.insert(expected.begin()+static_cast<std::ptrdiff_t>(to),row);
        bool changed=e.reorder(from,boundary,e.generation());check(changed==(boundary!=from&&boundary!=from+1),"Wrong reorder mutation flag.");
        check(e.edit().rows.size()==count,"Reorder changed row count.");for(size_t i=0;i<count;++i)same_row(e.edit().rows[i],expected[i]);++cases;
    }
    Editor e;e.set_catalog(fixture(504));e.select(4);auto gen=e.generation();check(e.reorder(503,0,gen),"504-row last-to-first failed.");check(e.edit().rows[0].track.song_id==503,"Wrong moved ID.");check(!e.reorder(0,504,gen),"Stale drag accepted.");check(e.reorder(0,504,e.generation()),"First-to-last failed.");check(e.edit().rows.back().track.song_id==503,"Wrong end ID.");
    check(!e.reorder(504,0,e.generation())&&!e.reorder(0,505,e.generation()),"Invalid drag accepted.");
    e.set_catalog(fixture(2));e.select(4);check(!e.can_save(),"Unchanged save enabled.");e.rename_soundtrack("");check(e.dirty()&&!e.can_save(),"Empty pending name lost dirty protection.");e.rename_soundtrack("Changed");check(e.can_save(),"Rename did not enable save.");e.rename_track(0,"Renamed");check(e.edit().rows[0].track.song_id==0,"Rename changed ID.");
    e.remove_track(1);check(e.edit().rows.size()==1,"Remove failed.");e.clear();check(e.edit().rows.empty()&&!e.can_save(),"Clear enables empty save.");
    auto c=fixture(1);c.metadata.soundtrack_id_index_matches_records=false;e.set_catalog(c);e.select(4);check(e.can_save(),"Header repair save unavailable.");
    c=fixture(1);Soundtrack orphan;orphan.orphan=true;orphan.record.soundtrack_id=10;c.soundtracks.push_back(orphan);e.set_catalog(c);e.select(10);check(e.read_only()&&!e.can_save(),"Orphan write enabled.");refuses([&]{e.clear();});
    e.new_soundtrack();check(e.edit().id==11,"New soundtrack reused orphan ID.");
    const auto wave=temp/fs::u8path("Unicode-\xc3\xa9.wav");write_text_new(wave,"fixture");e.add_files({wave,wave});check(e.edit().rows[0].track.song_id==1000&&e.edit().rows[1].track.song_id==1001,"Local IDs not global/unique.");
    auto prev=e.edit();refuses([&]{e.add_files({wave,temp/"missing.wav"});});check(e.edit().rows.size()==prev.rows.size(),"Failed batch add partially applied.");
    c=fixture(1);c.metadata.next_song_id=65536;e.set_catalog(c);e.select(4);refuses([&]{e.add_files({wave});});
    c=fixture(1);c.metadata.next_soundtrack_id=65536;e.set_catalog(c);refuses([&]{e.new_soundtrack();});
    c=fixture(1);c.writable=false;e.set_catalog(c);e.select(4);refuses([&]{e.rename_track(0,"x");});
    validate_name(std::string(63,'a'),63);refuses([]{validate_name(std::string(64,'a'),63);});
    validate_name("\xF0\x9F\x8E\xB5",2);refuses([]{validate_name("\xF0\x9F\x8E\xB5",1);});
    for(auto s:{std::string("bad\nname"),std::string("\xC0\x80"),std::string("\xED\xA0\x80"),std::string("x\0y",3)})refuses([&]{validate_name(s,63);});
    check(title_from_path(temp/(std::string(50,'a')+".mp3")).size()==31,"Filename title did not fit ST.DB.");
    check(duration(0)=="--:--"&&duration(61000)=="1:01"&&duration(3661000)=="1:01:01","Duration formatting failed.");
    std::vector<StDbSoundtrack> records{{0,"Original",{{0,5000,"A"},{1,6000,"B"}}},{4,"Unicode \xc3\xa9",{{31,7000,"\xF0\x9F\x8E\xB5"}}}};
    auto db=temp/fs::u8path("database-\xc3\xa9.new");std::string error;
    check(XemuCsb::WriteStDb(utf8(db),records,error,{12,60}),"TEST12 writer failed.");
    auto bytes=read_text(db);std::vector<uint8_t> raw(bytes.begin(),bytes.end());
    check(le32(raw.data()+12)==0&&le32(raw.data()+16)==4&&le32(raw.data()+0x19c)==60,"Header index/floors failed.");
    check(le32(raw.data()+0xcc10)==0x0004001f,"Packed global song ID changed.");
    std::vector<StDbSoundtrack> decoded;StDbMetadata metadata;
    check(XemuCsb::ReadStDbFile(utf8(db),decoded,error,&metadata),"Unicode path read failed.");
    check(decoded[1].name==records[1].name&&decoded[1].tracks[0].title==records[1].tracks[0].title,"Unicode text changed.");
    check(metadata.next_soundtrack_id==12&&metadata.next_song_id==60&&metadata.soundtrack_id_index_matches_records,"Metadata mismatch.");
    put32(raw.data()+12,4);put32(raw.data()+16,0);check(XemuCsb::ReadStDb(raw,decoded,error,&metadata)&&!metadata.soundtrack_id_index_matches_records,"Malformed index not detected.");
    fs::remove_all(temp);std::cout<<"PASS: "<<checks<<" CSB unit/property checks; "<<cases<<" exhaustive insertion cases; 504-row/stale/Unicode/global-ID/repair cases.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\nWorkspace: "<<temp<<"\n";return 1;}}
