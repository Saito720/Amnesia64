/* Color emoji for multiplayer chat. Distributed under the GPLv3 or later. */
#ifndef LUX_MULTIPLAYER_CHAT_EMOJI_H
#define LUX_MULTIPLAYER_CHAT_EMOJI_H

#include <cstdint>
#include <array>
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
    static const int CategoryCount=8;
    static const char* GetCategoryKey(int category);
    static const char* GetCategoryName(int category);
    struct PickerEntry {
        int glyph,category;
        std::string name,unicode;
        std::string searchKeywords;
        std::vector<std::string> aliases;
        bool skinTone=false;
    };
    const std::vector<PickerEntry>& GetPickerEntries() const {return mvPickerEntries;}
    const PickerEntry* GetPickerEntryForGlyph(int glyph) const;
    static std::string NormalizePickerSearch(const std::string& query);
    std::vector<size_t> FindPickerEntries(int category,const std::string& query) const;
    int ResolvePickerTone(int glyph,int tone) const;
    struct CompletionSuggestion {
        std::string alias,replacement;
        int glyph=-1;
    };
    // Registered aliases only; exact/prefix matches precede substrings. Each
    // replacement resolves to its displayed glyph, including the chosen tone.
    std::vector<CompletionSuggestion> FindCompletionSuggestions(const std::string& query,
        int preferredTone=0,size_t limit=8) const;
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
    void LoadPicker(const std::string& path);
    void LoadPickerTones(const std::string& path);
    hpl::iTexture* mpTexture=nullptr;
    std::vector<Glyph> mvGlyphs;
    std::vector<Node> mvNodes;
    std::map<std::string,std::string> mShortcodes;
    std::vector<PickerEntry> mvPickerEntries;
    std::vector<size_t> mvPickerIndexByGlyph;
    std::vector<int> mvToneBases;
    std::vector<std::array<int,6>> mvToneVariants;
    size_t mlLongestShortcode=0;
};

namespace luxchat {
struct EmojiCompletionToken {
    bool active=false;
    size_t start=0,end=0;
    std::string query;
};
// Offsets are native UTF-8 bytes. The replacement range includes the opening
// colon, the remaining alias word after the caret, and an optional closing
// colon. Supplying the catalog excludes already completed registered tokens.
EmojiCompletionToken FindEmojiCompletionToken(const std::string& text,size_t cursor,
    size_t selectionStart,size_t selectionEnd,const cLuxMultiplayerChatEmoji* emoji=nullptr);
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
