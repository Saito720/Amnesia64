/* Color emoji for multiplayer chat. Distributed under the GPLv3 or later. */
#ifndef LUX_MULTIPLAYER_CHAT_EMOJI_H
#define LUX_MULTIPLAYER_CHAT_EMOJI_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct ImFont;
struct ImDrawList;
namespace hpl {class cResources;class iLowLevelGraphics;class iTexture;}

class cLuxMultiplayerChatEmoji {
public:
    cLuxMultiplayerChatEmoji();
    ~cLuxMultiplayerChatEmoji();
    bool Load(const std::string& directory,hpl::cResources* resources,hpl::iLowLevelGraphics* graphics);
    bool IsReady() const {return mpTexture && !mvGlyphs.empty();}
    size_t GetGlyphCount() const {return mvGlyphs.size();}
    int GetTextureHandle() const;
    const std::string& GetGlyphUnicode(int glyph) const;
    size_t GetShortcodeCount() const {return mShortcodes.size();}
    const std::map<std::string,std::string>& GetShortcodes() const {return mShortcodes;}
    struct TextSpan {size_t offset,length,displayOffset,displayLength;bool replaced;};
    std::string ExpandShortcodes(const std::string& text,std::vector<TextSpan>* spans=nullptr) const;
    // Unknown tokens own their closing colon, so adjacent literal forms stay
    // intact. literalBytes tells the editor how far to retain that ownership.
    size_t MatchShortcode(const std::string& text,size_t offset,int& glyph,size_t& literalBytes) const;
    // Byte offsets refer to the original UTF-8 text. VS16 is optional in a
    // source sequence; VS15 explicitly retains text presentation.
    size_t Match(const std::string& text,size_t offset,int& glyph) const;
    bool ContainsEmoji(const std::string& text) const;
    void DrawGlyph(ImDrawList* draw,int glyph,float x,float y,float size,unsigned alpha) const;
private:
    struct Glyph {float u0,v0,u1,v1;std::string unicode;};
    struct Node {std::map<uint32_t,size_t> children;int glyph=-1;};
    bool LoadMapping(const std::string& path,int& width,int& height);
    void LoadShortcodes(const std::string& path);
    hpl::iTexture* mpTexture=nullptr;
    std::vector<Glyph> mvGlyphs;
    std::vector<Node> mvNodes;
    std::map<std::string,std::string> mShortcodes;
    size_t mlLongestShortcode=0;
};

namespace luxchat {
struct RichGlyph {
    size_t offset,length;
    float x,y,width;
    int emoji;
    uint32_t codePoint;
    size_t displayOffset,displayLength;
};
struct RichTextLayout {
    std::vector<RichGlyph> glyphs;
    float width=0,height=0;
    size_t emojiCount=0;
    // Canonical text is separate from the editor's original bytes so alias
    // glyphs can join adjacent skin tones, selectors and ZWJ components.
    std::string displayText;
};
RichTextLayout LayoutRichText(const std::string& text,ImFont* font,float fontSize,float wrapWidth,
    const cLuxMultiplayerChatEmoji* emoji,bool wrap=true,bool parseShortcodes=false);
float RichCaretX(const std::string& text,const RichTextLayout& layout,size_t byteOffset);
size_t HitRichText(const RichTextLayout& layout,float x,size_t textLength);
void DrawRichText(ImDrawList* draw,const std::string& text,const RichTextLayout& layout,ImFont* font,
    float fontSize,float x,float y,uint32_t color,const cLuxMultiplayerChatEmoji* emoji,bool outline,
    size_t nameBytes=0,uint32_t nameColor=0);
}
#endif
