// SPDX-License-Identifier: GPL-2.0-or-later
// Independent TEST12 production FATX reader; never linked into the converter.
#include "fatx-hdd.hh"
#include "xhc/common.hpp"
#include <iostream>
using namespace xhc;
static bool read_cb(void* p,uint64_t off,void* b,size_t n){try{static_cast<File*>(p)->read(off,b,n);return true;}catch(...){return false;}}
struct Digest {Sha256 hash;uint64_t bytes=0;};
static bool write_cb(void* p,const void* b,size_t n){auto* d=static_cast<Digest*>(p);d->hash.add(b,n);d->bytes+=n;return true;}
int main(int argc,char** argv){try{require(argc==2,"Usage: oracle disk.raw");File f(fs::u8path(argv[1]));XemuFatxHdd::Snapshot snapshot;require(XemuFatxHdd::BuildSnapshot(read_cb,&f,f.size(),snapshot),"TEST12 parser rejected image.");
    for(const auto& part:snapshot.partitions){require(part.available,"TEST12 unavailable partition: "+part.status);std::function<void(const std::vector<XemuFatxHdd::Entry>&,const std::string&)> scan=[&](const std::vector<XemuFatxHdd::Entry>& entries,const std::string& path){for(const auto& e:entries){auto name=path+"/"+e.name;if(e.directory)scan(e.children,name);else{Digest d;std::string error;require(XemuFatxHdd::StreamFile(read_cb,&f,f.size(),part,e,write_cb,&d,error),"TEST12 stream failed: "+error);std::cout<<hex(name.data(),name.size())<<"\t"<<d.bytes<<"\t"<<d.hash.finish()<<"\n";}}};scan(part.entries,std::string(1,part.letter));}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
