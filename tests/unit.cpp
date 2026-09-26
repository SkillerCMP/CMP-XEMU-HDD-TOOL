// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/common.hpp"
#include <iostream>
#include <functional>
#include <fstream>
#include <cstring>
using namespace xhc;
namespace {
unsigned checks=0;
void test(bool b,const char* name){++checks;if(!b)throw Error(std::string("Unit failed: ")+name);}
void bad(const std::function<void()>& fn,const char* name){bool refused=false;try{fn();}catch(const Error&){refused=true;}test(refused,name);}
std::string digest(const std::string& s){Sha256 h;h.add(s.data(),s.size());return h.finish();}
}
int main(){try{
    test(digest("")=="E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855","SHA empty");
    test(digest("abc")=="BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD","SHA abc");
    test(digest(std::string(1000000,'a'))=="CDC76E5C9914FB9281A1C7E284D73E67F1809A48A497200E046D39CCC7112CD0","SHA million a");
    for(size_t n=0;n<256;++n){std::string a;for(size_t i=0;i<n;++i)a+=char((i*71+n)&255);test(unb64(b64(a))==a,"base64 roundtrip");}
    test(b64("C")=="Qw==","manifest base64");bad([]{unb64("AA=A");},"base64 pad");bad([]{unb64("?AAA");},"base64 bad");bad([]{unb64("AB==");},"base64 bits");
    for(const char* s:{"abc.wma","222bblk{","ST.DB","C","my songs"})validate_host_component(s);
    for(const char* s:{"..","a/b","a\\b","a:b","NUL.txt","COM1","PRN","x.","x ","","a?"})bad([&]{validate_host_component(s);},"host filename");
    test(Json::parse("{\"a\":[true,false,null,42,\"a\\u00e9\\ud83d\\ude00\"]}").at("a").array[3].u64()==42,"JSON data");
    test(Json::parse("18446744073709551615").u64()==UINT64_MAX,"JSON UINT64");
    for(const char* s:{"01","1.","1e","[1,]","{\"x\":1,\"x\":2}","{\"x\":}","true false","\"\\ud800\"","\"\\udc00\"","\"a\n\""})bad([&]{Json::parse(s);},"JSON reject");
    bad([]{Json::parse("18446744073709551616").u64();},"JSON overflow");bad([]{Json::parse("-1").u64();},"JSON signed");
    test(quote_windows_argument(L"a b")==L"\"a b\"","Windows spaced argument");
    test(quote_windows_argument(L"a\\")==L"\"a\\\\\"","Windows trailing slash");
    test(quote_windows_argument(L"a\"b")==L"\"a\\\"b\"","Windows embedded quote");
    auto root=fs::temp_directory_path()/("xhc-unit-"+random_token());fs::create_directory(root);
    {File f(root/"a",true,4096);const char text[]="abc";f.write(10,text,3);f.flush();bad([&]{f.write(4095,text,3);},"range write");}
    {File f(root/"a");auto b=f.read(10,3);test(std::string(b.begin(),b.end())=="abc","file read");bad([&]{f.read(4095,3);},"range read");Context ctx;f.verify_zero(0,10,ctx);bad([&]{f.verify_zero(0,4096,ctx);},"zero scan");f.stable();bad([&]{const char x=0;f.write(0,&x,1);},"source handle write forbidden");}
    {Bytes bytes(12288,0);bytes[5000]=0x7A;{File output(root/"sparse",true,bytes.size());output.write_sparse(0,bytes.data(),bytes.size());output.flush();}File input(root/"sparse");test(input.read(0,bytes.size())==bytes,"sparse output exact bytes");bad([&]{input.write_sparse(0,bytes.data(),1);},"sparse source write forbidden");}
    bad([&]{File f(root/"a",true,1);},"exclusive create");write_text_new(root/"b","keep");bad([&]{move_new(root/"a",root/"b");},"publish overwrite refusal");test(read_text(root/"b")=="keep","existing preserved");move_new(root/"a",root/"c");test(fs::exists(root/"c"),"exclusive move");
    bad([&]{absolute_safe(root/"b",false);},"existing output");
    test(fs::equivalent(absolute_safe(root/"b",true),root/"b"),"existing absolute-safe path");
    auto future=absolute_safe(root/"future-output",false);test(future.filename()=="future-output","new absolute-safe output path");
    test(is_within(root/"sub"/"x",root),"descendant");test(!is_within(root.string()+"-other",root),"prefix not descendant");
#ifdef _WIN32
    test(is_within(fs::path(L"C:\\\u00E9\\child"), fs::path(L"c:\\\u00C9")), "Unicode Windows path containment");
#endif
#ifndef _WIN32
    fs::create_symlink(root/"b",root/"link");bad([&]{absolute_safe(root/"link",true);},"symlink refusal");
#endif
    fs::remove_all(root);std::cout<<"PASS: "<<checks<<" unit checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
