/**
 * @file textrender.cpp
 * @brief TextRenderBase plugin for KiriKiri2.
 *
 * Ported from the drop-in replacement by Hikaru Terazono (3c1u),
 * licensed under Apache-2.0 / MIT.
 * https://github.com/3c1u/TextRender
 */

#include "ncbind.hpp"
#include "FontRasterizer.h"
#include "tvpfontstruc.h"

#include <optional>
#include <string>
#include <vector>

#define NCB_MODULE_NAME TJS_W("textrender.dll")

extern FontRasterizer *GetCurrentRasterizer();

using tjs_ustring = std::basic_string<tjs_char>;
using RgbColor = uint32_t;

#define setprop_t(d, p, ty) \
  { tTJSVariant v(ty(p)); d->PropSet(TJS_MEMBERENSURE, TJS_W(#p), nullptr, &v, d); }

#define setprop_opt_t(d, p, ty) \
  if (p != std::nullopt) { \
    tTJSVariant v(ty(*p)); d->PropSet(TJS_MEMBERENSURE, TJS_W(#p), nullptr, &v, d); \
  } else { \
    tTJSVariant v; d->PropSet(TJS_MEMBERENSURE, TJS_W(#p), nullptr, &v, d); \
  }

#define setprop(d, p) setprop_t(d, p, )

#define setprop_opt(d, p) setprop_opt_t(d, p, )

#define getprop_t(d, p, ty) \
  { tTJSVariant v; \
    if (TJS_SUCCEEDED(d->PropGet(0, TJS_W(#p), nullptr, &v, d)) && v.Type() != tvtVoid) { \
      p = ty((tjs_int)(v)); \
    } }

#define getprop_opt_t(d, p, ty) \
  { tTJSVariant v; \
    if (TJS_SUCCEEDED(d->PropGet(0, TJS_W(#p), nullptr, &v, d)) && v.Type() != tvtVoid) { \
      p = ty(v); \
    } else { p = std::nullopt; } }

#define getprop(d, p) getprop_t(d, p, )

static tjs_ustring variant_to_ustring(const tTJSVariant &v) {
  const tjs_char *s = v.GetString();
  return s ? tjs_ustring(s) : tjs_ustring();
}

static bool ustring_contains(const tjs_ustring &s, tjs_char ch) {
  return s.find(ch) != tjs_ustring::npos;
}

struct TextRenderState {
  bool bold = false;
  bool italic = false;
  tjs_ustring face{TJS_W("user")};
  int fontSize = 24;
  RgbColor chColor = 0xffffff;
  int rubySize = 10;
  int rubyOffset = -2;
  bool shadow = true;
  RgbColor shadowColor = 0x000000;
  bool edge = false;
  RgbColor edgeColor = 0x0080ff;
  int lineSpacing = 6;
  int pitch = 0;
  int lineSize = 0;

  tTJSVariant serialize() const {
    auto dict = TJSCreateDictionaryObject();
    setprop(dict, bold);
    setprop(dict, italic);
    setprop(dict, fontSize);
    { tTJSVariant v(ttstr(face.c_str())); dict->PropSet(TJS_MEMBERENSURE, TJS_W("face"), nullptr, &v, dict); }
    setprop_t(dict, chColor, static_cast<tjs_int>);
    setprop(dict, rubySize);
    setprop(dict, rubyOffset);
    setprop(dict, shadow);
    setprop_t(dict, shadowColor, static_cast<tjs_int>);
    setprop(dict, edge);
    setprop_t(dict, edgeColor, static_cast<tjs_int>);
    setprop(dict, lineSpacing);
    setprop(dict, pitch);
    setprop(dict, lineSize);
    auto res = tTJSVariant(dict, dict);
    dict->Release();
    return res;
  }

  void deserialize(tTJSVariant t) {
    auto dict = t.AsObjectNoAddRef();
    if (!dict) return;
    getprop(dict, bold);
    getprop(dict, italic);
    getprop(dict, fontSize);
    { tTJSVariant v;
      if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("face"), nullptr, &v, dict)) && v.Type() != tvtVoid) {
        const tjs_char *s = v.GetString();
        if (s) face = s;
      }
    }
    getprop_t(dict, chColor, static_cast<RgbColor>);
    getprop(dict, rubySize);
    getprop(dict, rubyOffset);
    getprop(dict, shadow);
    getprop_t(dict, shadowColor, static_cast<RgbColor>);
    getprop(dict, edge);
    getprop_t(dict, edgeColor, static_cast<RgbColor>);
    getprop(dict, lineSpacing);
    getprop(dict, pitch);
    getprop(dict, lineSize);
  }
};

struct TextRenderOptions {
  tjs_ustring following{
      u"%),:;]}\uff61\uff63\uff9e\uff9f\u3002\uff0c\u3001\uff0e\uff1a\uff1b\u309b\u309c\u30fd\u30fe\u309d\u309e\u3005\u2019\u201d\uff09\u3015\uff3d\uff5d\u3009\u300b\u300d\u300f\u3011\u00b0\u2032\u2033\u2103\uffe0\uff05\u2030\u3000!.?"
      u"\uff64\uff65\uff67\uff68\uff69\uff6a\uff6b\uff6c\uff6d\uff6e\uff6f\uff70\u30fb\uff1f\uff01\u30fc\u3041\u3043\u3045\u3047\u3049\u3063\u3083\u3085\u3087\u308e\u30a1\u30a3\u30a5\u30a7\u30a9\u30c3\u30e3\u30e5\u30e7\u30ee\u30f5\u30f6"};
  tjs_ustring leading{u"\\$([{\uff62\u2018\u201c\uff08\u3014\uff3b\uff5b\u3008\u300a\u300c\u300e\u3010\uffe5\uff04\uffe1"};
  tjs_ustring begin{u"\u300c\u300e\uff08\u2018\u201c\u3014\uff3b\uff5b\u3008\u300a"};
  tjs_ustring end{u"\u300d\u300f\uff09\u2019\u201d\u3015\uff3d\uff5d\u3009\u300b"};

  void deserialize(tTJSVariant t) {
    auto dict = t.AsObjectNoAddRef();
    if (!dict) return;
    { tTJSVariant v;
      if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("following"), nullptr, &v, dict)) && v.Type() != tvtVoid) {
        const tjs_char *s = v.GetString(); if (s) following = s;
      }
    }
    { tTJSVariant v;
      if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("leading"), nullptr, &v, dict)) && v.Type() != tvtVoid) {
        const tjs_char *s = v.GetString(); if (s) leading = s;
      }
    }
    { tTJSVariant v;
      if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("begin"), nullptr, &v, dict)) && v.Type() != tvtVoid) {
        const tjs_char *s = v.GetString(); if (s) begin = s;
      }
    }
    { tTJSVariant v;
      if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("end"), nullptr, &v, dict)) && v.Type() != tvtVoid) {
        const tjs_char *s = v.GetString(); if (s) end = s;
      }
    }
  }
};

struct CharacterInfo {
  bool bold = false;
  bool italic = false;
  bool graph = false;
  bool vertical = false;
  tjs_ustring face{TJS_W("user")};
  int x = 0;
  int y = 0;
  int cw = 0;
  int size = 0;
  RgbColor color = 0xffffff;
  std::optional<RgbColor> edge = std::nullopt;
  std::optional<RgbColor> shadow = std::nullopt;
  tjs_ustring text;

  tTJSVariant serialize() const {
    auto dict = TJSCreateDictionaryObject();
    setprop(dict, bold);
    setprop(dict, italic);
    setprop(dict, graph);
    setprop(dict, vertical);
    setprop(dict, x);
    setprop(dict, y);
    setprop(dict, cw);
    setprop(dict, size);
    { tTJSVariant v(ttstr(face.c_str())); dict->PropSet(TJS_MEMBERENSURE, TJS_W("face"), nullptr, &v, dict); }
    setprop_t(dict, color, static_cast<tjs_int>);
    setprop_opt_t(dict, edge, static_cast<tjs_int>);
    setprop_opt_t(dict, shadow, static_cast<tjs_int>);
    { tTJSVariant v(ttstr(text.c_str())); dict->PropSet(TJS_MEMBERENSURE, TJS_W("text"), nullptr, &v, dict); }
    auto res = tTJSVariant(dict, dict);
    dict->Release();
    return res;
  }
};

#define property_accessor(name, type, storage) \
  type get_##name() const { return storage; } \
  void set_##name(type v) { storage = v; }

#define property_accessor_cast(name, type, cast, storage) \
  cast get_##name() const { return cast(storage); } \
  void set_##name(cast v) { storage = type(v); }

#define property_accessor_string(name, storage) \
  tTJSVariant get_##name() const { return tTJSVariant(ttstr(storage.c_str())); } \
  void set_##name(tTJSVariant v) { \
    const tjs_char *s = v.GetString(); \
    storage = s ? tjs_ustring(s) : tjs_ustring(); \
  }

#define property_delegate(name) NCB_PROPERTY(name, get_##name, set_##name);

class TextRenderBase {
public:
  TextRenderBase() {}
  virtual ~TextRenderBase() {}

  bool render(tTJSString text, int autoIndent, int diff, int all, bool same);
  void setRenderSize(int width, int height);
  void setDefault(tTJSVariant defaultSettings);
  void setOption(tTJSVariant options);
  tTJSVariant getCharacters(int start, int end);
  void clear();
  void done();

  tTJSVariant getKeyWait();
  tTJSVariant renderDelay();
  int calcShowCount(int elapsed);
  tTJSVariant renderText(tTJSString text);

  property_accessor(vertical, bool, m_vertical);
  property_accessor(bold, bool, m_state.bold);
  property_accessor(italic, bool, m_state.italic);
  property_accessor_string(face, m_state.face);
  property_accessor(fontSize, int, m_state.fontSize);
  property_accessor_cast(chColor, RgbColor, tjs_int, m_state.chColor);
  property_accessor(rubySize, int, m_state.rubySize);
  property_accessor(rubyOffset, int, m_state.rubyOffset);
  property_accessor(shadow, bool, m_state.shadow);
  property_accessor_cast(shadowColor, RgbColor, tjs_int, m_state.shadowColor);
  property_accessor(edge, bool, m_state.edge);
  property_accessor(lineSpacing, int, m_state.lineSpacing);
  property_accessor(pitch, int, m_state.pitch);
  property_accessor(lineSize, int, m_state.lineSize);

  property_accessor(defaultBold, bool, m_default.bold);
  property_accessor(defaultItalic, bool, m_default.italic);
  property_accessor_string(defaultFace, m_default.face);
  property_accessor(defaultFontSize, int, m_default.fontSize);
  property_accessor_cast(defaultChColor, RgbColor, tjs_int, m_default.chColor);
  property_accessor(defaultRubySize, int, m_default.rubySize);
  property_accessor(defaultRubyOffset, int, m_default.rubyOffset);
  property_accessor(defaultShadow, bool, m_default.shadow);
  property_accessor_cast(defaultShadowColor, RgbColor, tjs_int, m_default.shadowColor);
  property_accessor(defaultEdge, bool, m_default.edge);
  property_accessor(defaultLineSpacing, int, m_default.lineSpacing);
  property_accessor(defaultPitch, int, m_default.pitch);
  property_accessor(defaultLineSize, int, m_default.lineSize);

  double get_fontScale() const { return m_fontScale; }
  void set_fontScale(double v) { m_fontScale = v; }

private:
  int m_boxWidth = 0;
  int m_boxHeight = 0;
  int m_x = 0;
  int m_y = 0;
  int m_indent = 0;
  int m_autoIndent = 0;
  bool m_overflow = false;
  bool m_isBeginningOfLine = true;
  bool m_vertical = false;
  double m_fontScale = 1.0;

  TextRenderOptions m_options{};
  TextRenderState m_default{};
  TextRenderState m_state{};

  std::vector<CharacterInfo> m_characters{};
  std::vector<CharacterInfo> m_buffer{};
  uint32_t m_mode = 0;

  void pushCharacter(tjs_char ch);
  void pushGraphicalCharacter(const tjs_ustring &graph);
  void performLinebreak();
  void flush(bool force = false);
  void updateFont();
};

enum TextRenderMode {
  kTextRenderModeLeading = 0,
  kTextRenderModeNormal,
  kTextRenderModeFollowing,
};

static bool readchar(tTJSString const &str, size_t &i, tjs_char &c) {
  auto const len = (size_t)str.GetLen();
  if (++i >= len) return false;
  c = str[i];
  return true;
}

static void read_integer(tTJSString const &str, size_t &i, int &value) {
  tjs_char ch;
  bool is_negative = false;
  while (true) {
    if (!readchar(str, i, ch)) {
      TVPThrowExceptionMessage(TJS_W("TextRenderBase::render() parse error: expected integer or ';', found EOF"));
    }
    if ('0' <= ch && ch <= '9') { value = value * 10 + (ch - '0'); continue; }
    if (ch == '-') { is_negative = !is_negative; continue; }
    if (ch == ';') { if (is_negative) value = -value; return; }
    TVPThrowExceptionMessage(TJS_W("TextRenderBase::render() parse error: unexpected char"));
  }
}

bool TextRenderBase::render(tTJSString text, int autoIndent, int diff, int all, bool same) {
  m_autoIndent = autoIndent;
  size_t len = (size_t)text.GetLen();
  for (size_t i = 0; i < len; ++i) {
    tjs_char ch = text[i];
    switch (ch) {
    case '%': {
      if (!readchar(text, i, ch))
        TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error after %%"));

      switch (ch) {
      case 'f': {
        tjs_ustring fontname;
        while (true) {
          if (!readchar(text, i, ch))
            TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error in %%f"));
          if (ch == ';') break;
          fontname += ch;
        }
        if (!fontname.empty()) m_state.face = fontname;
        updateFont();
        break;
      }
      case 'b': {
        int value = 0;
        read_integer(text, i, value);
        m_state.bold = (value != 0);
        updateFont();
        break;
      }
      case 'i': {
        int value = 0;
        read_integer(text, i, value);
        m_state.italic = (value != 0);
        updateFont();
        break;
      }
      case 's': {
        int value = 0;
        read_integer(text, i, value);
        m_state.fontSize = value;
        updateFont();
        break;
      }
      case 'e': {
        int value = 0;
        read_integer(text, i, value);
        m_state.edge = (value != 0);
        break;
      }
      case 'd': {
        int value = 0;
        read_integer(text, i, value);
        break;
      }
      case 'r': m_state = m_default; updateFont(); break;
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9': {
        int value = static_cast<int>(ch - '0');
        read_integer(text, i, value);
        m_state.fontSize = m_default.fontSize * value / 100;
        updateFont();
        break;
      }
      default: break;
      }
      break;
    }
    case '\\': {
      if (!readchar(text, i, ch))
        TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error after backslash"));

      switch (ch) {
      case 'n': flush(); performLinebreak(); break;
      case 't': pushCharacter('\t'); break;
      case 'i': m_indent = m_x; break;
      case 'r': m_indent = 0; break;
      case 'w': pushCharacter(' '); break;
      case 'k': break;
      case 'x': break;
      default: pushCharacter(ch); break;
      }
      break;
    }
    case '[': {
      while (true) {
        if (!readchar(text, i, ch))
          TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error in ruby []"));
        if (ch == ']') break;
      }
      break;
    }
    case '#': {
      RgbColor colour = 0x00;
      while (true) {
        if (!readchar(text, i, ch))
          TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error in color #"));
        if (ch == ';') break;
        RgbColor c = 0;
        if ('0' <= ch && ch <= '9') c = static_cast<RgbColor>(ch - '0');
        else if ('A' <= ch && ch <= 'F') c = 0x0a + static_cast<RgbColor>(ch - 'A');
        else if ('a' <= ch && ch <= 'f') c = 0x0a + static_cast<RgbColor>(ch - 'a');
        colour = (colour << 4) | c;
      }
      m_state.chColor = colour;
      break;
    }
    case '&': {
      tjs_ustring graph;
      while (true) {
        if (!readchar(text, i, ch))
          TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error in graphic &"));
        if (ch == ';') break;
        graph += ch;
      }
      pushGraphicalCharacter(graph);
      break;
    }
    case '$': {
      while (true) {
        if (!readchar(text, i, ch))
          TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error in eval $"));
        if (ch == ';') break;
      }
      break;
    }
    default:
      pushCharacter(ch);
      break;
    }
  }

  return !m_overflow;
}

void TextRenderBase::performLinebreak() {
  auto rasterizer = GetCurrentRasterizer();
  m_x = m_indent;
  m_isBeginningOfLine = true;
  m_y += rasterizer->GetAscentHeight() + m_state.lineSpacing;
}

void TextRenderBase::pushGraphicalCharacter(const tjs_ustring &) {
  // graphical character embedding is not yet implemented
}

void TextRenderBase::pushCharacter(tjs_char ch) {
  auto isLeadingChar = ustring_contains(m_options.leading, ch);
  auto isFollowingChar = ustring_contains(m_options.following, ch);
  auto isIndent = ustring_contains(m_options.begin, ch);
  auto isIndentDecr = ustring_contains(m_options.end, ch);

  uint32_t current;
  if (isLeadingChar) current = kTextRenderModeLeading;
  else if (isFollowingChar) current = kTextRenderModeFollowing;
  else current = kTextRenderModeNormal;

  if (m_mode == kTextRenderModeFollowing || m_mode != kTextRenderModeLeading) {
    flush();
  }

  auto rasterizer = GetCurrentRasterizer();
  auto text_height = rasterizer->GetAscentHeight();

  int advance_width = 0, advance_height = 0;
  rasterizer->GetTextExtent(ch, advance_width, advance_height);

  CharacterInfo info;
  info.bold = m_state.bold;
  info.italic = m_state.italic;
  info.graph = false;
  info.vertical = false;
  info.face = m_state.face;
  info.x = 0;
  info.y = 0;
  info.cw = advance_width;
  info.size = text_height;
  info.color = m_state.chColor;
  info.edge = m_state.edge ? std::make_optional(m_state.edgeColor) : std::nullopt;
  info.shadow = m_state.shadow ? std::make_optional(m_state.shadowColor) : std::nullopt;
  tjs_char tmp[] = { ch, 0 };
  info.text = tmp;

  m_buffer.push_back(std::move(info));

  if (m_autoIndent) {
    if (m_isBeginningOfLine && m_autoIndent < 0) {
      m_x -= advance_width;
    }
    if (isIndent) {
      m_indent = m_x + advance_width;
    }
    if (isIndentDecr && m_indent > 0) {
      flush();
      m_indent = 0;
    }
  }

  m_mode = current;
  m_isBeginningOfLine = false;
}

void TextRenderBase::flush(bool force) {
  if (m_buffer.empty()) return;

  auto x = m_x;
  for (auto &ch : m_buffer) {
    auto advance_width = ch.cw;
    auto new_x = advance_width + x + m_state.pitch;

    if (m_boxWidth < new_x) {
      if (force) {
        performLinebreak();
        x = m_x;
        new_x = advance_width + x + m_state.pitch;
      } else {
        performLinebreak();
        flush(true);
        return;
      }
    }
    ch.x = x;
    ch.y = m_y;
    x = new_x;
  }

  m_x = x;
  m_characters.insert(m_characters.end(), m_buffer.begin(), m_buffer.end());
  m_buffer.clear();
}

void TextRenderBase::setRenderSize(int width, int height) {
  m_boxWidth = width;
  m_boxHeight = height;
  clear();
}

void TextRenderBase::setDefault(tTJSVariant defaultSettings) {
  m_default.deserialize(defaultSettings);
}

void TextRenderBase::setOption(tTJSVariant options) {
  m_options.deserialize(options);
}

tTJSVariant TextRenderBase::getCharacters(int start, int end) {
  auto array = TJSCreateArrayObject();

  if ((end < start) || (start == 0 && end == 0)) {
    for (size_t i = 0, cnt = m_characters.size(); i < cnt; ++i) {
      auto ch = m_characters[i].serialize();
      array->PropSetByNum(TJS_MEMBERENSURE, (tjs_int)i, &ch, array);
    }
  }

  return tTJSVariant(array, array);
}

void TextRenderBase::clear() {
  m_characters.clear();
  m_state = m_default;
  m_overflow = false;
  m_x = 0;
  m_y = 0;
  m_indent = 0;
  m_isBeginningOfLine = true;
  updateFont();
}

void TextRenderBase::updateFont() {
  auto rasterizer = GetCurrentRasterizer();
  tTVPFont font;
  font.Height = m_state.fontSize;
  font.Flags = static_cast<tjs_uint32>(
      (m_state.bold ? TVP_TF_BOLD : 0) | (m_state.italic ? TVP_TF_ITALIC : 0));
  font.Angle = 0;
  font.Face = ttstr(m_state.face.c_str());
  rasterizer->ApplyFont(font);
}

void TextRenderBase::done() {
  flush();
}

tTJSVariant TextRenderBase::getKeyWait() {
  auto array = TJSCreateArrayObject();
  return tTJSVariant(array, array);
}

tTJSVariant TextRenderBase::renderDelay() {
  auto array = TJSCreateArrayObject();
  return tTJSVariant(array, array);
}

int TextRenderBase::calcShowCount(int elapsed) {
  return static_cast<int>(m_characters.size());
}

tTJSVariant TextRenderBase::renderText(tTJSString text) {
  clear();
  render(text, 0, 0, 1, false);
  done();
  return getCharacters(0, 0);
}

NCB_REGISTER_CLASS(TextRenderBase) {
  Constructor();

  NCB_METHOD(render);
  NCB_METHOD(setRenderSize);
  NCB_METHOD(setDefault);
  NCB_METHOD(setOption);
  NCB_METHOD(getCharacters);
  NCB_METHOD(clear);
  NCB_METHOD(done);

  NCB_METHOD(getKeyWait);
  NCB_METHOD(renderDelay);
  NCB_METHOD(calcShowCount);
  NCB_METHOD(renderText);

  property_delegate(vertical);
  property_delegate(bold);
  property_delegate(italic);
  property_delegate(face);
  property_delegate(fontSize);
  property_delegate(chColor);
  property_delegate(rubySize);
  property_delegate(rubyOffset);
  property_delegate(shadow);
  property_delegate(shadowColor);
  property_delegate(edge);
  property_delegate(lineSpacing);
  property_delegate(pitch);
  property_delegate(lineSize);

  property_delegate(defaultBold);
  property_delegate(defaultItalic);
  property_delegate(defaultFace);
  property_delegate(defaultFontSize);
  property_delegate(defaultChColor);
  property_delegate(defaultRubySize);
  property_delegate(defaultRubyOffset);
  property_delegate(defaultShadow);
  property_delegate(defaultShadowColor);
  property_delegate(defaultEdge);
  property_delegate(defaultLineSpacing);
  property_delegate(defaultPitch);
  property_delegate(defaultLineSize);

  NCB_PROPERTY(fontScale, get_fontScale, set_fontScale);
};
