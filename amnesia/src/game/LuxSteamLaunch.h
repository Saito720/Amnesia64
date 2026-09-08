#ifndef LUX_STEAM_LAUNCH_H
#define LUX_STEAM_LAUNCH_H

#include <cstdint>
#include <string>
#include <vector>

namespace luxsteam {
inline bool Space(char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n'; }
inline std::string Trim(const std::string& text) {
    size_t a=0,b=text.size();
    while(a<b && Space(text[a])) ++a;
    while(b>a && Space(text[b-1])) --b;
    return text.substr(a,b-a);
}
inline bool ParseLobbyCode(const std::string& text,uint64_t& lobby) {
    lobby=0;
    const std::string code=Trim(text);
    if(code.empty() || code.size()>20) return false;
    uint64_t value=0;
    for(char c:code) {
        if(c<'0' || c>'9' || value>(UINT64_MAX-static_cast<unsigned>(c-'0'))/10) return false;
        value=value*10+static_cast<unsigned>(c-'0');
    }
    if(!value) return false;
    lobby=value;return true;
}

// Keep the existing config-file argument intact when Steam appends its launch switch.
// Never interpret content inside a quoted config path as a Steam command.
inline bool ExtractLobbyLaunch(const std::string& command,std::string& remaining,uint64_t& lobby) {
    struct Token { size_t begin,end;bool quoted; };
    std::vector<Token> tokens;
    size_t p=0;
    while(p<command.size()) {
        while(p<command.size() && Space(command[p])) ++p;
        if(p==command.size()) break;
        const size_t start=p;
        bool quote=false,quoted=false;
        while(p<command.size()) {
            if(command[p]=='"') {quote=!quote;quoted=true;}
            else if(!quote && Space(command[p])) break;
            ++p;
        }
        tokens.push_back({start,p,quoted});
    }
    lobby=0;remaining=command;
    size_t begin=std::string::npos,end=0;
    for(size_t i=0;i<tokens.size();++i) {
        const Token& token=tokens[i];
        if(token.quoted || command.substr(token.begin,token.end-token.begin)!="+connect_lobby") continue;
        if(begin!=std::string::npos || i+1==tokens.size()) return false;
        const Token& value=tokens[++i];
        std::string code=command.substr(value.begin,value.end-value.begin);
        if(code.size()>=2 && code.front()=='"' && code.back()=='"') code=code.substr(1,code.size()-2);
        if(!ParseLobbyCode(code,lobby)) return false;
        begin=token.begin;end=value.end;
    }
    if(begin!=std::string::npos) remaining=Trim(command.substr(0,begin)+command.substr(end));
    return true;
}
}
#endif
