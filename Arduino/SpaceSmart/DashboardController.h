#pragma once
#include "DashboardModel.h"
namespace Dashboard {
// Returns true only when a persisted preference changed.
inline bool rotate(State& s, int clicks) {
  if (!clicks) return false;
  if (s.page == Page::LedTest) {
    s.ledTest.selected = uint8_t(wrap(s.ledTest.selected + clicks, 6));
    return false;
  }
  if (s.page == Page::LightSetup) { s.lightSetupItem=uint8_t(wrap(s.lightSetupItem+clicks,6)); s.lightPairConfirm=false; return false; }
  if (s.page == Page::Light && s.detail) {
    if (s.editing) { s.lightRequest=s.lightItem; s.lightDelta=std::max(-15,std::min(15,clicks)); }
    else s.lightItem=uint8_t(wrap(s.lightItem+clicks,5));
    return false;
  }
  if (!s.detail) {
    s.page = Page(wrap(int(s.page) + clicks, int(Page::Count)));
    return false;
  }
  if (s.page == Page::Graph) {
    s.metric = uint8_t(wrap(s.metric + clicks, 4));
    return false;
  }
  if (s.page == Page::Settings) {
    if (!s.editing) {
      s.setting = uint8_t(wrap(s.setting + clicks, 5));
      return false;
    }
    switch (s.setting) {
      case 0:
        s.theme = uint8_t(wrap(s.theme + clicks, THEME_COUNT));
        break;
      case 1:
        if (abs(clicks) % 2) s.rotation = s.rotation % 2 ? 0 : 3;
        break;
      case 2:
        s.brightness = uint8_t(std::max(0, std::min(64, int(s.brightness) + clicks * 2)));
        break;
      case 3:
        s.focus.setMinutes(int(s.focus.minutes) + clicks * 5);
        break;
    }
    return true;
  }
  return false;
}
inline void push(State& s, uint32_t now) {
  if(s.page==Page::LightSetup) {
    if(s.lightSetupItem==2 && (!s.lightPairConfirm || uint32_t(now-s.lightPairConfirmAt)>5000)) { s.lightPairConfirm=true; s.lightPairConfirmAt=now; return; }
    s.lightRequest=10+s.lightSetupItem; s.lightDelta=0; s.lightPairConfirm=false; return;
  }
  if (s.page == Page::LedTest) {
    if (s.ledTest.active) s.ledTest.stop(); else s.ledTest.start(now);
    return;
  }
  if (!s.detail) {
    s.detail = true;
    return;
  }
  switch (s.page) {
    case Page::Light:
      if(s.lightItem==4) { s.page=Page::LightSetup; s.lightSetupItem=0; s.editing=false; }
      else if(s.lightItem==1||s.lightItem==2) s.editing=!s.editing;
      else { s.lightRequest=s.lightItem; s.lightDelta=0; }
      break;
    case Page::Graph:
      s.span = (s.span + 1) % 3;
      break;
    case Page::Focus:
      s.focus.toggle(now);
      break;
    case Page::Settings:
      if (s.setting == 4) { s.page = Page::LedTest; s.detail = true; s.editing = false; }
      else s.editing = !s.editing;
      break;
    default:
      break;
  }
}
inline void back(State& s, bool longPress = false) {
  if(s.page==Page::LightSetup) { s.page=Page::Light; s.detail=true; s.editing=false; s.lightPairConfirm=false; if(!longPress)return; }
  if (s.page == Page::LedTest) {
    s.ledTest.stop(); s.page = Page::Settings; s.detail = true; s.editing = false; s.setting = 4;
    if (!longPress) return;
  }
  if (longPress) {
    if (s.page == Page::Focus && s.detail)
      s.focus.reset();
    else {
      s.page = Page::Environment;
      s.detail = false;
      s.editing = false;
    }
  } else if (s.editing)
    s.editing = false;
  else if (s.detail)
    s.detail = false;
  else
    s.page = Page::Environment;
}
inline void openSettings(State& s) {
  s.ledTest.stop();
  s.page = Page::Settings;
  s.detail = true;
  s.editing = false;
}
}  // namespace Dashboard
