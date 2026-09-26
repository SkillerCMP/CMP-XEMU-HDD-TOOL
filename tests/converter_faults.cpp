// SPDX-License-Identifier: GPL-2.0-or-later
// Fault injection uses the public logging callback, not production test switches.
#include "xhc/converter.hpp"
#include <fstream>
#include <iostream>
using namespace xhc;
namespace {
void overwrite(const fs::path& path, uint64_t offset, char value) {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    require(bool(f), "Test injection cannot open staging/source file.");
    f.seekp(static_cast<std::streamoff>(offset)); f.put(value); f.flush();
    require(bool(f), "Test injection write failed.");
}
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"Usage: xhc-converter-faults fixture.raw workspace");
        auto source=fs::absolute(argv[1]), root=fs::absolute(argv[2]);
        unsigned checks=0;
        for (unsigned which=0;which<5;++which) {
            Options options; options.source=source; options.destination=root/("fault-output-"+std::to_string(which));
            options.mode=Mode::ImageToFolder; options.offline_confirmed=true;
            Context ctx; fs::path stage; bool injected=false; char old=0;
            ctx.log=[&](const std::string& line) {
                if(line.rfind("Workspace: ",0)==0) stage=fs::u8path(line.substr(11));
                if(injected)return;
                if(which==0 && line.rfind("Verifying every free region",0)==0) {
                    // Known-free tail of fresh E: must be verified zero.
                    overwrite(stage/"result"/"Images"/"E.img",kLayout[1].size-128,'X'); injected=true;
                } else if(which==1 && line.rfind("Creating and verifying host",0)==0) {
                    // Occupied private mirror name forces safe abort; no source changes.
                    fs::create_directory(stage/"result"/"C"); injected=true;
                } else if(which==2 && line=="Rechecking the original source before publication...") {
                    std::ifstream f(source,std::ios::binary);f.get(old);f.close();
                    overwrite(source,0,static_cast<char>(old^1));injected=true;
                } else if(which==3 && line=="Rechecking the original source before publication...") {
                    fs::create_directory(options.destination);
                    write_text_new(options.destination/"existing.txt","DO NOT REPLACE");injected=true;
                } else if(which==4 && line.rfind("Rebuilding clean E",0)==0) {
                    injected=true;ctx.cancelled.store(true);
                }
            };
            bool failed=false; std::string message;
            try { convert(options,ctx); }
            catch(const Error& e) { failed=true;message=e.what(); }
            if(which==2 && injected) overwrite(source,0,old);
            require(injected && failed,"Injected failure was not caught.");
            require(!stage.empty()&&fs::exists(stage),"Failed transaction was not retained.");
            require(message.find(utf8(stage))!=std::string::npos,"Exact recovery path not reported.");
            require(fs::exists(stage/"failure.txt"),"Failure report was not retained.");
            if(which==3) require(read_text(options.destination/"existing.txt")=="DO NOT REPLACE","Racing destination overwritten.");
            else require(!fs::exists(options.destination),"Failed operation published output.");
            if(which==0) require(message.find("nonzero")!=std::string::npos,"Free-cluster corruption not specifically detected.");
            if(which==2) require(message.find("Source changed")!=std::string::npos,"Source mutation not detected.");
            ++checks;std::cout<<"PASS fault "<<which<<": "<<message.substr(0,message.find('\n'))<<"\n";
        }
        std::cout<<"PASS: "<<checks<<" forced-failure transaction scenarios\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
