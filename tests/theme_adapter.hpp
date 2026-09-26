// Test-only adapter for shell/modal suites. Real drawing/lifecycle is in gui_theme.cpp.
#pragma once
#include "../src/gui_theme_win32.hpp"
namespace theme_adapter {inline xhc::ui::Theme value=xhc::ui::Theme::Default;inline unsigned changes=0,attached=0;inline bool persist=true;}
inline void InitializeGuiTheme(){theme_adapter::value=xhc::ui::Theme::Default;}
inline xhc::ui::Theme CurrentGuiTheme() noexcept{return theme_adapter::value;}
inline bool SelectGuiTheme(HWND,xhc::ui::Theme theme){theme_adapter::value=theme;++theme_adapter::changes;return theme_adapter::persist;}
inline void ApplyGuiTheme(HWND){++theme_adapter::attached;}
inline void AttachGuiThemeMenu(HWND,HMENU){}
inline void RefreshGuiThemeSystem(HWND){}
inline void ShutdownGuiTheme(){}
inline UINT TrackGuiThemePopup(HMENU menu,UINT flags,int x,int y,HWND owner){return static_cast<UINT>(TrackPopupMenu(menu,flags,x,y,0,owner,nullptr));}
inline HBRUSH GuiThemeAccentBrush(){return nullptr;}
