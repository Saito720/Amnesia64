/* Color emoji for multiplayer chat. Distributed under the GPLv3 or later. */
#include "LuxMultiplayerChatEmoji.h"
#include "LuxMultiplayerChatProtocol.h"
#include "LuxTypes.h"
#if USE_SDL2
#include "imgui.h"
#include "impl/LowLevelGraphicsSDL.h"
#ifdef LoadBitmap
#undef LoadBitmap
#endif
#endif
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>

namespace {
#if USE_SDL2
struct TextureState {
    GLint binding=0,unpackAlignment=4;
    GLboolean enabled;
    TextureState():enabled(glIsEnabled(GL_TEXTURE_2D)) {
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpackAlignment);
    }
    ~TextureState() {
        glBindTexture(GL_TEXTURE_2D,binding);glPixelStorei(GL_UNPACK_ALIGNMENT,unpackAlignment);
        if(enabled) glEnable(GL_TEXTURE_2D);else glDisable(GL_TEXTURE_2D);
    }
};
#endif
bool ReadPoint(const std::string& text,size_t& cursor,uint32_t& point) {
    if(cursor>=text.size()) return false;
    const uint8_t first=static_cast<uint8_t>(text[cursor++]);
    if(first<0x80) {point=first;return true;}
    unsigned count;uint32_t minimum;
    if(first>=0xc2 && first<=0xdf) {count=1;minimum=0x80;point=first&0x1f;}
    else if(first>=0xe0 && first<=0xef) {count=2;minimum=0x800;point=first&0x0f;}
    else if(first>=0xf0 && first<=0xf4) {count=3;minimum=0x10000;point=first&7;}
    else return false;
    if(count>text.size()-cursor) return false;
    for(unsigned i=0;i<count;++i) {
        const uint8_t next=static_cast<uint8_t>(text[cursor++]);
        if((next&0xc0)!=0x80) return false;
        point=(point<<6)|(next&0x3f);
    }
    return point>=minimum && point<=0x10ffff && !(point>=0xd800 && point<=0xdfff);
}
bool IsSpace(uint32_t point) {
    return point==' ' || point=='\t' || point==0xa0 || point==0x3000;
}
void AppendPoint(std::string& text,uint32_t point) {
    if(point<=0x7f) text.push_back(static_cast<char>(point));
    else if(point<=0x7ff) {text.push_back(static_cast<char>(0xc0|(point>>6)));text.push_back(static_cast<char>(0x80|(point&0x3f)));}
    else if(point<=0xffff) {
        text.push_back(static_cast<char>(0xe0|(point>>12)));text.push_back(static_cast<char>(0x80|((point>>6)&0x3f)));
        text.push_back(static_cast<char>(0x80|(point&0x3f)));
    } else {
        text.push_back(static_cast<char>(0xf0|(point>>18)));text.push_back(static_cast<char>(0x80|((point>>12)&0x3f)));
        text.push_back(static_cast<char>(0x80|((point>>6)&0x3f)));text.push_back(static_cast<char>(0x80|(point&0x3f)));
    }
}
bool CompletionSpace(uint32_t point) {
    return point<=0x20 || point==0x85 || point==0xa0 || point==0x1680 ||
        (point>=0x2000 && point<=0x200a) || point==0x2028 || point==0x2029 ||
        point==0x202f || point==0x205f || point==0x3000;
}
bool CompletionAliasPoint(uint32_t point) {
    if(point<0x80) return (point>='a' && point<='z') || (point>='A' && point<='Z') ||
        (point>='0' && point<='9') || point=='_' || point=='+' || point=='-';
    return !CompletionSpace(point) && !(point>=0x200b && point<=0x206f) &&
        !(point>=0xfe00 && point<=0xfe0f);
}
std::string CompletionSearch(const std::string& value) {
    std::string folded;size_t cursor=0;uint32_t point=0;
    while(ReadPoint(value,cursor,point)) {
        if(point>='A' && point<='Z') point=point-'A'+'a';
        if(point==0xd1) point=0xf1; // The registered piñata alias.
        AppendPoint(folded,point);
    }
    return cLuxMultiplayerChatEmoji::NormalizePickerSearch(folded);
}
int CompletionAliasRank(const std::string& alias,const std::string& search) {
    const std::string normalized=CompletionSearch(alias);
    if(normalized==search) return 0;
    if(normalized.compare(0,search.size(),search)==0) return 1;
    return normalized.find(search)!=std::string::npos?2:3;
}
bool CompletionExplicitTone(const std::string& search) {
    return search.find("::")!=std::string::npos || search.find("tone")!=std::string::npos || search.find("skin")!=std::string::npos ||
        search.find("light")!=std::string::npos || search.find("dark")!=std::string::npos ||
        search.find("medium")!=std::string::npos;
}
}

cLuxMultiplayerChatEmoji::cLuxMultiplayerChatEmoji() {mvNodes.emplace_back();}
cLuxMultiplayerChatEmoji::~cLuxMultiplayerChatEmoji() {if(mpTexture) hplDelete(mpTexture);}

bool cLuxMultiplayerChatEmoji::LoadMapping(const std::string& path,int& width,int& height) {
    FILE* file=hpl::cPlatform::OpenFile(hpl::cString::UTF8ToWChar(path),_W("rb"));
    if(!file) return false;
    char line[1024];
    bool valid=std::fgets(line,sizeof(line),file) && std::sscanf(line,"%d %d",&width,&height)==2 &&
        width>0 && height>0 && width<=8192 && height<=8192;
    mvGlyphs.clear();mvNodes.clear();mvNodes.emplace_back();
    while(valid && std::fgets(line,sizeof(line),file)) {
        std::istringstream record(line);std::string sequence;int x,y,w,h;
        if(!(record>>sequence>>x>>y>>w>>h) || x<0 || y<0 || w<=0 || h<=0 ||
           w>width || h>height || x>width-w || y>height-h || mvGlyphs.size()>=8192) {valid=false;break;}
        size_t node=0,start=0;unsigned points=0;std::string unicode;
        while(start<sequence.size()) {
            const size_t end=sequence.find('-',start);
            const std::string hex=sequence.substr(start,end==std::string::npos?end:end-start);
            char* parsedEnd=nullptr;const unsigned long point=std::strtoul(hex.c_str(),&parsedEnd,16);
            if(hex.empty() || *parsedEnd || point==0 || point>0x10ffff || ++points>32) {valid=false;break;}
            AppendPoint(unicode,static_cast<uint32_t>(point));
            if(point!=0xfe0f) {
                auto child=mvNodes[node].children.find(static_cast<uint32_t>(point));
                if(child==mvNodes[node].children.end()) {
                    const size_t next=mvNodes.size();mvNodes[node].children[static_cast<uint32_t>(point)]=next;
                    mvNodes.emplace_back();node=next;
                } else node=child->second;
            }
            if(end==std::string::npos) break;
            start=end+1;
        }
        if(!valid || node==0) {valid=false;break;}
        const int index=static_cast<int>(mvGlyphs.size());
        mvGlyphs.push_back({static_cast<float>(x)/width,static_cast<float>(y)/height,
            static_cast<float>(x+w)/width,static_cast<float>(y+h)/height,unicode});
        mvNodes[node].glyph=index;
    }
    std::fclose(file);
    if(!valid || mvGlyphs.empty()) {mvGlyphs.clear();mvNodes.clear();mvNodes.emplace_back();return false;}
    return true;
}

bool cLuxMultiplayerChatEmoji::Load(const std::string& directory,hpl::cResources* resources,hpl::iLowLevelGraphics* graphics) {
#if USE_SDL2
    if(mpTexture) {hplDelete(mpTexture);mpTexture=nullptr;}
    int width=0,height=0;
    if(!LoadMapping(directory+"mapping.txt",width,height)) return false;
    LoadShortcodes(directory+"shortcodes.txt");
    LoadPicker(directory+"picker.txt");
    LoadPickerTones(directory+"picker-tones.txt");
    const hpl::tWString image=hpl::cString::UTF8ToWChar(directory+"atlas.png");
    if(!hpl::cPlatform::FileExists(image)) return false;
    hpl::cBitmap* bitmap=resources->GetBitmapLoaderHandler()->LoadBitmap(image,0);
    if(!bitmap) return false;
    if(bitmap->GetWidth()!=width || bitmap->GetHeight()!=height) {hplDelete(bitmap);return false;}
    const TextureState previousState;
    mpTexture=graphics->CreateTexture("MultiplayerTwemoji",hpl::eTextureType_2D,hpl::eTextureUsage_Normal);
    const bool loaded=mpTexture && mpTexture->CreateFromBitmap(bitmap);
    hplDelete(bitmap);
    if(!loaded) {if(mpTexture) hplDelete(mpTexture);mpTexture=nullptr;return false;}
    mpTexture->SetFilter(hpl::eTextureFilter_Bilinear);
    mpTexture->SetWrapSTR(hpl::eTextureWrap_ClampToEdge);
    return true;
#else
    return false;
#endif
}

const char* cLuxMultiplayerChatEmoji::GetCategoryKey(int category) {
    static const char* keys[]={"people","nature","food","activity","travel","objects","symbols","flags"};
    return category>=0 && category<CategoryCount?keys[category]:"symbols";
}
const char* cLuxMultiplayerChatEmoji::GetCategoryName(int category) {
    static const char* names[]={"Smileys & People","Animals & Nature","Food & Drink","Activities",
        "Travel & Places","Objects","Symbols","Flags"};
    return category>=0 && category<CategoryCount?names[category]:"Symbols";
}
void cLuxMultiplayerChatEmoji::LoadPicker(const std::string& path) {
    mvPickerEntries.clear();
    std::vector<bool> seen(mvGlyphs.size(),false);
    const auto add=[&](int glyph,int category,const std::string& name) {
        if(glyph<0 || static_cast<size_t>(glyph)>=seen.size() || seen[glyph]) return;
        PickerEntry entry;entry.glyph=glyph;entry.category=category;entry.name=name;entry.unicode=GetGlyphUnicode(glyph);
        size_t position=0;uint32_t point=0;
        static const char* toneNames[]={"light skin tone","medium light skin tone","medium skin tone",
            "medium dark skin tone","dark skin tone"};
        while(ReadPoint(entry.unicode,position,point)) if(point>=0x1f3fb && point<=0x1f3ff) {
            entry.skinTone=true;entry.searchKeywords+='|';entry.searchKeywords+=toneNames[point-0x1f3fb];
        }
        mvPickerEntries.push_back(std::move(entry));seen[glyph]=true;
    };
    FILE* file=hpl::cPlatform::OpenFile(hpl::cString::UTF8ToWChar(path),_W("rb"));
    if(file) {
        char line[1024];
        while(std::fgets(line,sizeof(line),file)) {
            if(line[0]=='#') continue;
            std::istringstream record(line);std::string key,name,sequence,unicode;
            if(!std::getline(record,key,'\t') || !std::getline(record,name,'\t') || !std::getline(record,sequence) ||
                name.empty() || name.size()>256) continue;
            while(!sequence.empty() && (sequence.back()=='\r' || sequence.back()=='\n')) sequence.pop_back();
            int category=-1;
            for(int i=0;i<CategoryCount;++i) if(key==GetCategoryKey(i)) {category=i;break;}
            if(category<0) continue;
            size_t start=0;bool valid=true;
            while(start<sequence.size()) {
                const size_t end=sequence.find('-',start);
                const std::string hex=sequence.substr(start,end==std::string::npos?end:end-start);
                char* parsedEnd=nullptr;const unsigned long point=std::strtoul(hex.c_str(),&parsedEnd,16);
                if(hex.empty() || *parsedEnd || point==0 || point>0x10ffff || (point>=0xd800 && point<=0xdfff)) {valid=false;break;}
                AppendPoint(unicode,static_cast<uint32_t>(point));
                if(end==std::string::npos) break;
                start=end+1;
            }
            int glyph=-1;
            if(valid && !unicode.empty() && Match(unicode,0,glyph)==unicode.size()) add(glyph,category,name);
        }
        std::fclose(file);
    }
    // Older packages retain usable entries even without a complete catalog.
    for(size_t glyph=0;glyph<mvGlyphs.size();++glyph) if(!seen[glyph]) add(static_cast<int>(glyph),6,"");
    mvPickerIndexByGlyph.resize(mvGlyphs.size());
    for(size_t i=0;i<mvPickerEntries.size();++i) mvPickerIndexByGlyph[mvPickerEntries[i].glyph]=i;
    for(const auto& alias:mShortcodes) {
        int glyph=-1;
        if(Match(alias.second,0,glyph)==alias.second.size() && glyph>=0) {
            PickerEntry& entry=mvPickerEntries[mvPickerIndexByGlyph[glyph]];
            entry.aliases.push_back(alias.first);if(entry.name.empty()) entry.name=alias.first;
        }
    }
}
const cLuxMultiplayerChatEmoji::PickerEntry* cLuxMultiplayerChatEmoji::GetPickerEntryForGlyph(int glyph) const {
    return glyph>=0 && static_cast<size_t>(glyph)<mvPickerIndexByGlyph.size()?
        &mvPickerEntries[mvPickerIndexByGlyph[glyph]]:nullptr;
}
std::string cLuxMultiplayerChatEmoji::NormalizePickerSearch(const std::string& query) {
    size_t first=0,last=query.size();
    while(first<last && (query[first]==' ' || query[first]=='\t')) ++first;
    while(last>first && (query[last-1]==' ' || query[last-1]=='\t')) --last;
    if(last-first>1 && query[first]==':' && query[last-1]==':') {++first;--last;}
    std::string result;bool space=false;
    for(size_t i=first;i<last;++i) {
        char point=query[i];
        if(point>='A' && point<='Z') point=point-'A'+'a';
        const bool sign=point=='-' && i==first && i+1<last && query[i+1]>='0' && query[i+1]<='9';
        if(point==' ' || point=='\t' || point=='_' || (point=='-' && !sign)) {space=!result.empty();continue;}
        if(space) result.push_back(' ');space=false;result.push_back(point);
    }
    return result;
}
std::vector<size_t> cLuxMultiplayerChatEmoji::FindPickerEntries(int category,const std::string& query) const {
    const std::string search=NormalizePickerSearch(query);std::vector<size_t> matches;
    int queryGlyph=-1;const bool exactEmoji=!search.empty() && Match(search,0,queryGlyph)==search.size();
    for(size_t i=0;i<mvPickerEntries.size();++i) {
        const PickerEntry& entry=mvPickerEntries[i];
        if(search.empty()) {if(entry.category==category && !entry.skinTone) matches.push_back(i);continue;}
        bool matched=exactEmoji?entry.glyph==queryGlyph:
            (NormalizePickerSearch(entry.name).find(search)!=std::string::npos ||
             entry.searchKeywords.find(search)!=std::string::npos);
        if(!matched) for(const std::string& alias:entry.aliases)
            if(NormalizePickerSearch(alias).find(search)!=std::string::npos) {matched=true;break;}
        if(matched) matches.push_back(i);
    }
    return matches;
}
void cLuxMultiplayerChatEmoji::LoadPickerTones(const std::string& path) {
    mvToneBases.resize(mvGlyphs.size());mvToneVariants.resize(mvGlyphs.size());
    for(size_t i=0;i<mvGlyphs.size();++i) {
        mvToneBases[i]=static_cast<int>(i);mvToneVariants[i].fill(-1);mvToneVariants[i][0]=static_cast<int>(i);
    }
    const auto find=[&](const std::string& sequence) {
        std::string unicode;size_t start=0;
        while(start<sequence.size()) {
            const size_t end=sequence.find('-',start);const std::string hex=sequence.substr(start,end==std::string::npos?end:end-start);
            char* parsedEnd=nullptr;const unsigned long point=std::strtoul(hex.c_str(),&parsedEnd,16);
            if(hex.empty() || *parsedEnd || point==0 || point>0x10ffff || (point>=0xd800 && point<=0xdfff)) return -1;
            AppendPoint(unicode,static_cast<uint32_t>(point));
            if(end==std::string::npos) break;start=end+1;
        }
        int glyph=-1;return !unicode.empty() && Match(unicode,0,glyph)==unicode.size()?glyph:-1;
    };
    FILE* file=hpl::cPlatform::OpenFile(hpl::cString::UTF8ToWChar(path),_W("rb"));
    if(!file) return;
    char line[1024];
    while(std::fgets(line,sizeof(line),file)) {
        if(line[0]=='#') continue;
        std::istringstream record(line);std::string baseSequence,variantSequence;int tone=-1;
        if(!(record>>baseSequence>>tone>>variantSequence) || tone<0 || tone>5) continue;
        const int base=find(baseSequence),variant=find(variantSequence);
        if(base<0 || variant<0) continue;
        mvToneBases[variant]=base;
        if(tone>0) mvToneVariants[base][tone]=variant;
    }
    std::fclose(file);
}
int cLuxMultiplayerChatEmoji::ResolvePickerTone(int glyph,int tone) const {
    if(glyph<0 || static_cast<size_t>(glyph)>=mvToneBases.size() || tone<0 || tone>5) return glyph;
    const int base=mvToneBases[glyph],variant=mvToneVariants[base][tone];
    return variant>=0?variant:glyph;
}
std::vector<cLuxMultiplayerChatEmoji::CompletionSuggestion> cLuxMultiplayerChatEmoji::FindCompletionSuggestions(
    const std::string& query,int preferredTone,size_t limit) const {
    std::vector<CompletionSuggestion> result;
    size_t position=0,count=0;uint32_t point=0;
    while(position<query.size()) {
        if(!ReadPoint(query,position,point)) return result;
        if(point==':') {
            const size_t colon=position-1;
            if(!colon || !((colon && query[colon-1]==':') ||
                (position<query.size() && query[position]==':'))) return result;
        } else {
            if(!CompletionAliasPoint(point)) return result;
            ++count;
        }
    }
    if(count<2 || query.size()>128 || !limit || mShortcodes.empty()) return result;
    if(preferredTone<0 || preferredTone>5) preferredTone=0;
    limit=(std::min)(limit,size_t(64));
    const std::string search=CompletionSearch(query);
    if(search.empty()) return result;
    const bool explicitTone=CompletionExplicitTone(search);
    struct Candidate {CompletionSuggestion suggestion;int rank;size_t order;};
    std::map<int,Candidate> byGlyph;
    const auto bestAlias=[&](const PickerEntry& entry,bool requireMatch) {
        std::string best;int bestRank=4;
        for(const std::string& alias:entry.aliases) {
            const int rank=CompletionAliasRank(alias,search);
            if(requireMatch && rank>=3) continue;
            const bool canonical=alias==entry.name,bestCanonical=best==entry.name;
            const bool compound=alias.find(':')!=std::string::npos,bestCompound=best.find(':')!=std::string::npos;
            if(rank<bestRank || (rank==bestRank &&
                ((canonical && !bestCanonical) || (!canonical && !bestCanonical &&
                 ((!compound && bestCompound) || (compound==bestCompound &&
                  (alias.size()<best.size() || (alias.size()==best.size() && alias<best)))))))) {
                best=alias;bestRank=rank;
            }
        }
        return best;
    };
    for(size_t index=0;index<mvPickerEntries.size();++index) {
        const PickerEntry& source=mvPickerEntries[index];
        const std::string matchedAlias=bestAlias(source,true);
        if(matchedAlias.empty()) continue;
        const int glyph=explicitTone?source.glyph:ResolvePickerTone(source.glyph,preferredTone);
        const PickerEntry* display=GetPickerEntryForGlyph(glyph);
        std::string displayAlias=display?bestAlias(*display,true):std::string();
        if(displayAlias.empty() && display) displayAlias=bestAlias(*display,false);
        CompletionSuggestion suggestion;suggestion.glyph=glyph;
        suggestion.alias=displayAlias.empty()?matchedAlias:displayAlias;
        suggestion.replacement=displayAlias.empty()?GetGlyphUnicode(glyph):":"+displayAlias+":";
        const int rank=CompletionAliasRank(matchedAlias,search);
        const int base=explicitTone?source.glyph:ResolvePickerTone(source.glyph,0);
        const size_t order=base>=0 && static_cast<size_t>(base)<mvPickerIndexByGlyph.size()?mvPickerIndexByGlyph[base]:index;
        const auto previous=byGlyph.find(glyph);
        if(previous==byGlyph.end() || rank<previous->second.rank ||
            (rank==previous->second.rank && order<previous->second.order))
            byGlyph[glyph]={std::move(suggestion),rank,order};
    }
    std::vector<Candidate> candidates;
    for(auto& candidate:byGlyph) candidates.push_back(std::move(candidate.second));
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b) {
        if(a.rank!=b.rank) return a.rank<b.rank;
        if(a.order!=b.order) return a.order<b.order;
        return a.suggestion.alias<b.suggestion.alias;
    });
    for(size_t i=0;i<(std::min)(limit,candidates.size());++i) result.push_back(std::move(candidates[i].suggestion));
    return result;
}
void cLuxMultiplayerChatEmoji::LoadShortcodes(const std::string& path) {
    mShortcodes.clear();mlLongestShortcode=0;
    FILE* file=hpl::cPlatform::OpenFile(hpl::cString::UTF8ToWChar(path),_W("rb"));
    if(!file) return;
    char line[1024];
    while(mShortcodes.size()<20000 && std::fgets(line,sizeof(line),file)) {
        if(line[0]=='#') continue;
        std::istringstream record(line);std::string alias,sequence,unicode;
        if(!(record>>alias>>sequence) || alias.empty() || alias.size()>128) continue;
        size_t start=0;bool valid=true;
        while(start<sequence.size()) {
            const size_t end=sequence.find('-',start);
            const std::string hex=sequence.substr(start,end==std::string::npos?end:end-start);
            char* parsedEnd=nullptr;const unsigned long point=std::strtoul(hex.c_str(),&parsedEnd,16);
            if(hex.empty() || *parsedEnd || point==0 || point>0x10ffff || (point>=0xd800 && point<=0xdfff)) {valid=false;break;}
            AppendPoint(unicode,static_cast<uint32_t>(point));
            if(end==std::string::npos) break;
            start=end+1;
        }
        int glyph=-1;
        if(valid && !unicode.empty() && Match(unicode,0,glyph)==unicode.size()) {
            mShortcodes[alias]=unicode;mlLongestShortcode=(std::max)(mlLongestShortcode,alias.size());
        }
    }
    std::fclose(file);
}
std::string cLuxMultiplayerChatEmoji::ExpandShortcodes(const std::string& text,std::vector<TextSpan>* spans) const {
    std::string expanded;
    if(spans) spans->clear();
    const auto append=[&](size_t offset,size_t length,const std::string& display,bool replaced) {
        if(spans) spans->push_back({offset,length,expanded.size(),display.size(),replaced});
        expanded+=display;
    };
    for(size_t cursor=0;cursor<text.size();) {
        if(text[cursor]!=':' || mShortcodes.empty()) {
            const size_t start=cursor;
            do {++cursor;} while(cursor<text.size() && (text[cursor]!=':' || mShortcodes.empty()));
            append(start,cursor-start,text.substr(start,cursor-start),false);continue;
        }
        int glyph=-1;size_t literal=0;
        const size_t matched=MatchShortcode(text,cursor,glyph,literal);
        if(matched) {
            const auto value=mShortcodes.find(text.substr(cursor+1,matched-2));
            append(cursor,matched,value->second,true);cursor+=matched;
        } else {
            literal=(std::max)(size_t(1),literal);
            append(cursor,literal,text.substr(cursor,literal),false);cursor+=literal;
        }
    }
    return expanded;
}
size_t cLuxMultiplayerChatEmoji::MatchShortcode(const std::string& text,size_t offset,int& glyph,size_t& literalBytes) const {
    glyph=-1;literalBytes=0;
    if(offset>=text.size() || text[offset]!=':' || mShortcodes.empty()) return 0;
    size_t matched=0;
    const size_t limit=(std::min)(text.size(),offset+mlLongestShortcode+2);
    for(size_t end=offset+1;end<limit;++end) if(text[end]==':') {
        const auto alias=mShortcodes.find(text.substr(offset+1,end-offset-1));
        if(alias!=mShortcodes.end()) {matched=end-offset+1;Match(alias->second,0,glyph);}
    }
    if(matched) return matched;
    // Only plausible names claim a closing delimiter. A URL's colon must not
    // consume an unrelated emoji token later in the sentence.
    const size_t end=text.find(':',offset+1);
    bool plausible=end!=std::string::npos && end>offset+1;
    if(plausible) for(size_t i=offset+1;i<end;++i) {
        const unsigned char c=static_cast<unsigned char>(text[i]);
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
            c=='_' || c=='+' || c=='-' || c>=0x80)) {plausible=false;break;}
    }
    literalBytes=plausible?end-offset+1:1;
    return 0;
}

int cLuxMultiplayerChatEmoji::GetTextureHandle() const {return mpTexture?mpTexture->GetCurrentLowlevelHandle():0;}
const std::string& cLuxMultiplayerChatEmoji::GetGlyphUnicode(int glyph) const {
    static const std::string empty;
    return glyph>=0 && static_cast<size_t>(glyph)<mvGlyphs.size()?mvGlyphs[glyph].unicode:empty;
}
size_t cLuxMultiplayerChatEmoji::Match(const std::string& text,size_t offset,int& glyph) const {
    glyph=-1;
    if(offset>=text.size() || mvNodes.empty()) return 0;
    size_t cursor=offset,node=0,matched=0;
    while(cursor<text.size()) {
        uint32_t point;
        if(!ReadPoint(text,cursor,point)) break;
        if(point==0xfe0e) return 0;
        if(point==0xfe0f) {
            if(!node) return 0;
            if(mvNodes[node].glyph>=0) {glyph=mvNodes[node].glyph;matched=cursor-offset;}
            continue;
        }
        const auto next=mvNodes[node].children.find(point);
        if(next==mvNodes[node].children.end()) break;
        node=next->second;
        if(mvNodes[node].glyph>=0) {glyph=mvNodes[node].glyph;matched=cursor-offset;}
    }
    return matched;
}
bool cLuxMultiplayerChatEmoji::ContainsEmoji(const std::string& text) const {
    if(!IsReady()) return false;
    for(size_t cursor=0;cursor<text.size();) {
        int glyph;
        if(Match(text,cursor,glyph)) return true;
        const size_t start=cursor;uint32_t point;
        if(!ReadPoint(text,cursor,point)) cursor=start+1;
    }
    return false;
}
void cLuxMultiplayerChatEmoji::DrawGlyph(ImDrawList* draw,int glyph,float x,float y,float size,unsigned alpha) const {
#if USE_SDL2
    if(!IsReady() || glyph<0 || static_cast<size_t>(glyph)>=mvGlyphs.size()) return;
    const Glyph& image=mvGlyphs[glyph];
    draw->AddImage(static_cast<ImTextureID>(GetTextureHandle()),ImVec2(x,y),ImVec2(x+size,y+size),
        ImVec2(image.u0,image.v0),ImVec2(image.u1,image.v1),IM_COL32(255,255,255,alpha));
#endif
}

namespace luxchat {
EmojiCompletionToken FindEmojiCompletionToken(const std::string& text,size_t cursor,
    size_t selectionStart,size_t selectionEnd,const cLuxMultiplayerChatEmoji* emoji) {
    EmojiCompletionToken result;
    if(cursor>text.size() || selectionStart!=selectionEnd || selectionStart>text.size()) return result;
    struct Point {size_t start,end;uint32_t value;};
    std::vector<Point> points;size_t position=0,caretPoint=0;bool boundary=cursor==0;
    while(position<text.size()) {
        const size_t start=position;uint32_t point=0;
        if(!ReadPoint(text,position,point)) return result;
        points.push_back({start,position,point});
        if(position==cursor) {caretPoint=points.size();boundary=true;}
    }
    if(!boundary) return result;
    size_t opening=caretPoint;
    while(opening && CompletionAliasPoint(points[opening-1].value)) --opening;
    if(caretPoint-opening<2 || !opening || points[opening-1].value!=':') return result;
    --opening;
    size_t start=points[opening].start;
    size_t endPoint=caretPoint;
    while(endPoint<points.size() && CompletionAliasPoint(points[endPoint].value)) ++endPoint;
    size_t end=endPoint?points[endPoint-1].end:cursor;
    if(endPoint<points.size() && points[endPoint].value==':') end=points[endPoint].end;
    if(cursor-start-1>128 || end-start>130) return result;

    // Only actual opening delimiters may trigger completion. Completed known
    // and unknown tokens own their closing colon, including adjacent forms.
    size_t lastKnownStart=std::string::npos,lastKnownEnd=0;int lastKnownGlyph=-1;
    std::vector<size_t> ownedClosings;
    for(size_t scan=0;scan<=start;) {
        if(text[scan]!=':') {++scan;continue;}
        int glyph=-1;size_t literal=0;
        const size_t matched=emoji?emoji->MatchShortcode(text,scan,glyph,literal):0;
        if(matched) {
            if(start<scan+matched) return result;
            lastKnownStart=scan;lastKnownEnd=scan+matched;lastKnownGlyph=glyph;
            ownedClosings.push_back(scan+matched-1);scan+=matched;continue;
        }
        literal=0;
        const size_t closing=text.find(':',scan+1);
        if(closing!=std::string::npos && closing>scan+1) {
            size_t namePoint=scan+1;bool valid=true;
            while(namePoint<closing) {
                uint32_t point=0;
                if(!ReadPoint(text,namePoint,point) || !CompletionAliasPoint(point)) {valid=false;break;}
            }
            if(valid) literal=closing-scan+1;
        }
        if(literal>1 && scan<start) {
            if(start<scan+literal) return result;
            ownedClosings.push_back(scan+literal-1);scan+=literal;continue;
        }
        ++scan;
    }
    // Generated skin-tone aliases can extend a completed primary alias,
    // e.g. :adult::skin-tone-1:. Only actual registered compound prefixes own
    // the earlier range; ordinary :smile::gr stays a separate new token.
    if(emoji && lastKnownStart!=std::string::npos && lastKnownEnd==start) {
        const std::string compound=CompletionSearch(text.substr(lastKnownStart+1,cursor-lastKnownStart-1));
        bool extended=false;
        // Packaged compound aliases are skin-tone suffixes. Inspect just this
        // registered glyph's actual base and five tone entries, not the whole
        // alias dictionary on every editor frame.
        for(int tone=0;tone<6 && !extended;++tone) {
            const auto* entry=emoji->GetPickerEntryForGlyph(emoji->ResolvePickerTone(lastKnownGlyph,tone));
            if(!entry) continue;
            for(const std::string& alias:entry->aliases) {
                if(CompletionSearch(alias).compare(0,compound.size(),compound)==0) {extended=true;break;}
            }
        }
        if(extended) {
            start=lastKnownStart;
            while(opening && points[opening].start>start) --opening;
            if(cursor-start-1>128 || end-start>130) return result;
        }
    }
    if(opening) {
        const uint32_t previous=points[opening-1].value;
        const bool punctuation=previous=='(' || previous=='[' || previous=='{' || previous=='<' ||
            previous==')' || previous==']' || previous=='}' || previous=='>' || previous=='"' || previous=='\'' ||
            previous==',' || previous==';' || previous=='.' || previous=='!' || previous=='?';
        if(!CompletionSpace(previous) && !punctuation &&
            !(previous==':' && std::binary_search(ownedClosings.begin(),ownedClosings.end(),points[opening-1].start))) return result;
    }
    // A whitespace-delimited URL/email segment must never become an editor
    // replacement target, even when the caret sits before a later @ or /.
    size_t segmentStart=opening,segmentEnd=endPoint;
    while(segmentStart && !CompletionSpace(points[segmentStart-1].value)) --segmentStart;
    while(segmentEnd<points.size() && !CompletionSpace(points[segmentEnd].value)) ++segmentEnd;
    const size_t segmentByteStart=points[segmentStart].start;
    const size_t segmentByteEnd=segmentEnd?points[segmentEnd-1].end:text.size();
    const std::string segment=CompletionSearch(text.substr(segmentByteStart,segmentByteEnd-segmentByteStart));
    if(segment.find("://")!=std::string::npos || segment.find('@')!=std::string::npos || segment.find("www.")!=std::string::npos)
        return result;
    result.active=true;result.start=start;result.end=end;result.query=text.substr(start+1,cursor-start-1);
    return result;
}
RichTextLayout LayoutRichText(const std::string& text,ImFont* font,float fontSize,float wrapWidth,
    const cLuxMultiplayerChatEmoji* emoji,bool wrap,bool parseShortcodes) {
    RichTextLayout result;
#if USE_SDL2
    if(!font || fontSize<=0) return result;
    wrapWidth=wrap?(std::max)(1.0f,wrapWidth):FLT_MAX;
    const float scale=fontSize/font->FontSize,lineHeight=fontSize+3.0f;
    std::vector<cLuxMultiplayerChatEmoji::TextSpan> spans;
    result.displayText=parseShortcodes && emoji && emoji->IsReady()?emoji->ExpandShortcodes(text,&spans):text;
    const std::string& display=result.displayText;
    std::vector<size_t> sourceStart(display.size()),sourceEnd(display.size());
    if(spans.empty()) for(size_t i=0;i<display.size();++i) {sourceStart[i]=i;sourceEnd[i]=i+1;}
    else for(const auto& span:spans) for(size_t i=0;i<span.displayLength;++i) {
        sourceStart[span.displayOffset+i]=span.offset+(span.replaced?0:i);
        sourceEnd[span.displayOffset+i]=span.offset+(span.replaced?span.length:i+1);
    }
    std::vector<RichGlyph> tokens;
    for(size_t cursor=0;cursor<display.size();) {
        const size_t start=cursor;int matched=-1;
        const size_t bytes=emoji && emoji->IsReady()?emoji->Match(display,cursor,matched):0;
        uint32_t point=0;float width=0;
        if(bytes) {cursor+=bytes;width=fontSize+1.0f;}
        else {
            if(!ReadPoint(display,cursor,point)) {cursor=start+1;point=0xfffd;}
            // Presentation selectors and a fallback joiner have no ink of their own.
            if(!luxnet::ChatCodePointIsFormat(point) && point!='\n') {
                const ImFontGlyph* glyph=font->FindGlyph(static_cast<ImWchar>(point));
                width=glyph?glyph->AdvanceX*scale:0;
            }
            matched=-1;
        }
        const size_t rawStart=sourceStart[start],rawEnd=sourceEnd[cursor-1];
        tokens.push_back({rawStart,rawEnd-rawStart,0,0,width,matched,point,start,cursor-start});
    }
    float x=0,y=0;
    for(size_t i=0;i<tokens.size();++i) {
        RichGlyph token=tokens[i];
        if(token.codePoint=='\n') {x=0;y+=lineHeight;continue;}
        const bool space=token.emoji<0 && IsSpace(token.codePoint);
        const bool wordStart=!space && (i==0 || tokens[i-1].emoji>=0 || IsSpace(tokens[i-1].codePoint));
        if(wordStart && token.emoji<0 && wrap) {
            float word=0;
            for(size_t j=i;j<tokens.size() && tokens[j].emoji<0 && !IsSpace(tokens[j].codePoint) &&
                tokens[j].codePoint!='\n';++j) word+=tokens[j].width;
            if(word<=wrapWidth && x>0 && x+word>wrapWidth) {x=0;y+=lineHeight;}
        }
        if(wrap && x>0 && x+token.width>wrapWidth) {x=0;y+=lineHeight;}
        if(wrap && space && x==0) continue;
        token.x=x;token.y=y;result.glyphs.push_back(token);
        if(token.emoji>=0) ++result.emojiCount;
        x+=token.width;result.width=(std::max)(result.width,x);
        result.height=(std::max)(result.height,y+fontSize);
    }
#endif
    return result;
}
float RichCaretX(const std::string& text,const RichTextLayout& layout,size_t byteOffset) {
    for(const RichGlyph& glyph:layout.glyphs) {
        if(byteOffset<=glyph.offset) return glyph.x;
        if(byteOffset<glyph.offset+glyph.length) {
            // Keyboard editing still follows the original UTF-8 text. Interior
            // positions in a shortcode or joined emoji share its visual cell.
            size_t points=0,before=0,cursor=glyph.offset;
            while(cursor<glyph.offset+glyph.length) {
                const size_t start=cursor;uint32_t point;
                if(!ReadPoint(text,cursor,point)) cursor=start+1;
                ++points;if(cursor<=byteOffset) ++before;
            }
            return glyph.x+glyph.width*(points?static_cast<float>(before)/points:0);
        }
    }
    return layout.width;
}
size_t HitRichText(const RichTextLayout& layout,float x,size_t textLength) {
    for(const RichGlyph& glyph:layout.glyphs) {
        if(x<glyph.x+glyph.width*0.5f) return glyph.offset;
        if(x<glyph.x+glyph.width) return glyph.offset+glyph.length;
    }
    return textLength;
}
void DrawRichText(ImDrawList* draw,const std::string& text,const RichTextLayout& layout,ImFont* font,
    float fontSize,float x,float y,uint32_t color,const cLuxMultiplayerChatEmoji* emoji,bool outline,
    size_t nameBytes,uint32_t nameColor) {
#if USE_SDL2
    const std::string& display=layout.displayText;
    const unsigned alpha=(color>>IM_COL32_A_SHIFT)&255;
    const ImVec2 clipMin=draw->GetClipRectMin(),clipMax=draw->GetClipRectMax();
    for(size_t i=0;i<layout.glyphs.size();) {
        const RichGlyph& glyph=layout.glyphs[i];
        if(y+glyph.y+fontSize+1<clipMin.y || y+glyph.y-1>clipMax.y) {++i;continue;}
        if(glyph.emoji>=0) {
            if(emoji) emoji->DrawGlyph(draw,glyph.emoji,x+glyph.x,y+glyph.y,fontSize,alpha);
            ++i;continue;
        }
        if(luxnet::ChatCodePointIsFormat(glyph.codePoint)) {++i;continue;}
        const size_t start=glyph.displayOffset;size_t end=start+glyph.displayLength;
        size_t next=i+1;
        while(next<layout.glyphs.size() && layout.glyphs[next].emoji<0 &&
            !luxnet::ChatCodePointIsFormat(layout.glyphs[next].codePoint) &&
            (layout.glyphs[next].offset<nameBytes)==(glyph.offset<nameBytes) &&
            layout.glyphs[next].y==glyph.y && layout.glyphs[next].displayOffset==end) {
            end+=layout.glyphs[next].displayLength;++next;
        }
        const ImVec2 position(x+glyph.x,y+glyph.y);
        if(outline) for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy)
            draw->AddText(font,fontSize,ImVec2(position.x+dx,position.y+dy),IM_COL32(0,0,0,alpha),
                display.c_str()+start,display.c_str()+end);
        draw->AddText(font,fontSize,position,glyph.offset<nameBytes?nameColor:color,display.c_str()+start,display.c_str()+end);
        i=next;
    }
#endif
}
}
