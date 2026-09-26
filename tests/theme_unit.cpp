// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/theme.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <array>
namespace {
unsigned checks=0;
void check(bool b,const char* message){++checks;if(!b)throw std::runtime_error(message);}
double luminance(uint32_t rgb) {
    const auto channel=[](uint32_t c){double f=static_cast<double>(c)/255.0;return f<=0.04045?f/12.92:std::pow((f+0.055)/1.055,2.4);};
    return channel((rgb>>16)&255)*0.2126+channel((rgb>>8)&255)*0.7152+channel(rgb&255)*0.0722;
}
double contrast(uint32_t a,uint32_t b){auto x=luminance(a),y=luminance(b);return (std::max(x,y)+0.05)/(std::min(x,y)+0.05);}
}
int main(){try {
    using namespace xhc::ui;
    check(valid_theme(0)==Theme::Default,"zero selects Default");
    check(valid_theme(1)==Theme::Dark&&valid_theme(2)==Theme::Xbox,"menu values map");
    for(auto value:{3u,255u,65535u,0xFFFFFFFFu})check(valid_theme(value)==Theme::Default,"invalid setting falls back to white");
    for(auto mode:{Theme::Default,Theme::Dark,Theme::Xbox}) {
        const auto p=theme_palette(mode);
        const std::array<uint32_t,11> colors{p.background,p.surface,p.field,p.text,p.muted,p.border,p.accent,p.accent_text,p.selection,p.selected_text,p.hover};
        for(auto c:colors)check(c<=0xFFFFFF,"valid RGB color");
        for(auto bg:{p.background,p.surface,p.field,p.hover}) {
            check(contrast(p.text,bg)>=4.5,"readable normal text");
            check(contrast(p.muted,bg)>=3.0,"readable disabled text");
        }
        check(contrast(p.selected_text,p.selection)>=4.5,"selected row text");
        check(contrast(p.accent_text,p.accent)>=4.5,"checked glyph contrast");
        check(contrast(p.accent,p.background)>=3.0,"accent stands out against panel");
        check(theme_name(mode)[0]!=0,"theme has menu label");
        std::cout<<"Palette "<<static_cast<unsigned>(mode)<<": selected text contrast "<<contrast(p.selected_text,p.selection)<<"\n";
    }
    check(theme_palette(Theme::Default).background==0xFFFFFF,"Default is white");
    check(theme_palette(Theme::Xbox).accent==0x9BF00B,"Xbox uses bright green accent");
    check(theme_palette(Theme::Dark).accent!=theme_palette(Theme::Xbox).accent,"Dark and Xbox distinguishable");
    std::cout<<"PASS: "<<checks<<" theme palette/default/contrast checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
