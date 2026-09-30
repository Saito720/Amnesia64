// Regenerate the implementation catalog and searchable master reference.
// Dependency-free: node tools/generate-scripting-reference.cjs [--check]
const fs = require('fs');
const path = require('path');
const root = path.resolve(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/^\uFEFF/, '');
const source = read('amnesia/src/game/LuxScriptHandler.cpp');
const runtime = read('amnesia/src/game/LuxScriptRuntime.cpp');
const protocol = read('amnesia/src/game/LuxMultiplayerProtocol.h');
const groups = JSON.parse(read('tools/scripting-api-groups.json'));
const registrations = new Map();
for (const [index, line] of source.split(/\r?\n/).entries()) {
    const match = line.match(/^\s*AddFunc\("([^"\n]+)"\s*,\s*\(void\s*\*\)\s*(\w+)\)/);
    if (!match) continue;
    const name = match[1].match(/(\w+)\(/)[1];
    if (!registrations.has(name)) registrations.set(name, []);
    registrations.get(name).push({ declaration: match[1], binding: match[2], line: index + 1 });
}
const allow = runtime.match(/static const std::set<tString> allowed=\{([\s\S]*?)\};/);
if (!allow) throw Error('Client capability list not found. Update the generator for its new format.');
const clientNames = new Set([...allow[1].matchAll(/"(\w+)"/g)].map(match => match[1]));
for (const name of clientNames) if (!registrations.has(name)) throw Error('Unknown client API: ' + name);

// Find a method body while ignoring braces inside comments/quoted text.
function body(name) {
    const match = source.match(new RegExp('cLuxScriptHandler::' + name + '\\([^;{}]*\\)\\s*\\{'));
    if (!match) throw Error('Native implementation not found: ' + name);
    const start = match.index + match[0].length;
    let depth = 1, quote = '', comment = '';
    for (let i = start; i < source.length; i++) {
        const c = source[i], next = source[i + 1];
        if (comment === '//') { if (c === '\n') comment = ''; continue; }
        if (comment === '/*') { if (c === '*' && next === '/') { comment = ''; i++; } continue; }
        if (quote) { if (c === '\\') i++; else if (c === quote) quote = ''; continue; }
        if (c === '/' && (next === '/' || next === '*')) { comment = c + next; i++; continue; }
        if (c === '"' || c === "'") { quote = c; continue; }
        if (c === '{') depth++;
        else if (c === '}' && --depth === 0) return source.slice(start, i);
    }
    throw Error('Unterminated native method: ' + name);
}
const directed = protocol.match(/inline bool IsPlayerScriptCommand\([^)]*\)\s*\{[\s\S]*?return ([\s\S]*?);\s*\}/);
if (!directed) throw Error('Directed native capability predicate not found.');
const directedRanges = directed[1].trim().split(/\s*\|\|\s*/).map(clause => {
    const range = clause.match(/^\(command\s*>=\s*(\d+)\s*&&\s*command\s*<=\s*(\d+)\)$/);
    const singleton = clause.match(/^command\s*==\s*(\d+)$/);
    if (range) return [Number(range[1]), Number(range[2])];
    if (singleton) return [Number(singleton[1]), Number(singleton[1])];
    throw Error('Update the generator for the directed predicate: ' + clause);
});
const personal = id => directedRanges.some(([min, max]) => id >= min && id <= max);
const special = {
    SelectScriptPlayer: 'Select an available actor for the current authority dispatch. Invalid actors raise a script error; nested calls restore the caller.',
    GetScriptPlayerId: 'Return the current actor, or -1 in an unattributed authority context. Client modules return their owning local player.',
    GetScriptPlayerCount: 'Authority-only enumeration of the hosting player and available remote actors.',
    GetScriptPlayerIdAt: 'Authority-only enumeration, local actor first and remote IDs in order. An invalid index returns -1.',
    RunClientCallback: 'Authority-only typed string callback to the selected actor, in the matching initialized client module.',
    AddPlayerCollideCallback: 'Authority registration of an attributed per-player observer. Optional once-per-participant delivery; separate from combined occupancy.',
    RemovePlayerCollideCallback: 'Remove the explicit per-player observer for this child. Does not remove the combined observer.',
    PublishPlayerVar: 'Authority-only publication of a copied string view to the selected actor. The private store is not exposed.',
    GetPublishedScriptVar: 'Return a fresh string handle from the current actor/module published view. No write-through access.',
    GiveItem: 'With an authority actor, grant to that actor. Without one, retain existing shared script-grant policy; combination routing takes precedence.',
    GiveItemFromFile: 'With an authority actor, grant to that actor. Without one, retain existing shared script-grant policy; combination routing takes precedence.',
    RemoveItem: 'With an authority actor, remove from that actor; without one, retain shared removal. Logical shared grants retain their original shared-removal policy. Combination routing takes precedence.',
    HasItem: 'Require an authority actor in Version 2. Remote results use the supported progression/hand-object ledger, not a complete consumable mirror.',
    UnlockAchievement: 'Target the selected account, or the host and initialized participants without an actor. Transient, excluded from initial/history replay.',
    AddTimer: 'Authority: capture module and optional actor/session/character life. Client: private module timer with typed string callback.',
    RemoveTimer: 'Remove timers owned by the current authority context or current client module.',
    GetTimerTimeLeft: 'Read the timer owned by the current authority context or current client module; absent timers return zero.',
    SetEffectVoiceOverCallback: 'Authority actor required. Bind a one-shot host-issued voice-completion token to actor, module and character life.',
    SetLanternLitCallback: 'Authority actor required. Bind a persistent host-issued lantern token; empty callback clears the binding.',
    StartPlayerLookAt: 'Authority actor required. Local controller execution with a one-shot host-issued completion token when a callback is supplied.',
    AddEntityCollideCallback: 'Authority combined occupancy for parent Player: first entrance/final exit; one-shot removal applies to everyone. Other parents retain native policy.',
    RemoveEntityCollideCallback: 'Remove the native combined/entity registration. Does not remove the explicit per-player observer.',
    AutoSave: 'The native offline save handler suppresses autosaves in multiplayer/session worlds. No multiplayer disk-save format.',
    CheckPoint: 'Retain the shared native checkpoint policy. Multiplayer respawn does not replay checkpoint callbacks or reset other players/enemies.',
    ChangeMap: 'Use the coordinated authority map transition. Existing settings gate remote level-door requests and attributed remote callbacks.',
    ClearSavedMaps: 'Clear the authority saved-map collection. Does not clear the downloaded map resource cache.',
    StringSub: 'Return a bounded substring. Client declaration returns a fresh string@; authority keeps its legacy string& declaration. See string ownership below.'
};
function handling(name, entries) {
    if (special[name]) return special[name];
    if (/^(Set|Get)PlayerVar(Int|Float|String)$/.test(name)) return 'Authority-only selected-actor store, isolated by module. Short overload: map lifetime; campaign=true: current participant session across maps.';
    if (/^(Set|Add|Get)(Local|Global)Var/.test(name)) return 'Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage.';
    const text = entries.map(entry => body(entry.binding)).join('\n');
    if (text.includes('RequireLocalPresentationState')) return 'Selected actor required. Authority can query its local presentation; remote presentation queries fail explicitly. Client modules read their own subsystem.';
    if (text.includes('GetScriptRemotePlayer')) return 'Read the selected actor on authority, or the owning local actor on a client. Replicated remote data can lag; missing actors do not fall back to the host.';
    const target = text.match(/cLuxScriptPlayerCommandScope\s+\w+\((\d+)/);
    if (target && personal(Number(target[1]))) return 'Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history.' + (clientNames.has(name) ? ' Client calls affect their own local subsystem without explicit selection.' : '');
    if (/cLuxMultiplayerScriptScope\s+\w+\(/.test(text)) return 'Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation.';
    return 'Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly.';
}
const seen = new Set();
const functions = groups.flatMap(group => group.names.map(name => {
    if (seen.has(name)) throw Error('Duplicate catalog name: ' + name);
    seen.add(name);
    const entries = registrations.get(name);
    if (!entries) throw Error('Catalog function no longer registered: ' + name);
    const authority = entries.map(entry => entry.declaration + ';').join('\n');
    let client = clientNames.has(name) ? authority : '';
    if (name === 'StringSub' && client) {
        const override = source.match(/"(string@ StringSub\([^"\n]+\))"/);
        if (!override) throw Error('Client StringSub override not found.');
        client = override[1] + ';';
    }
    return {name, group:group.id, groupTitle:group.title, authority, client,
        handling:handling(name, entries), line:entries[0].line};
}));
if (seen.size !== registrations.size) throw Error('Add new registrations to tools/scripting-api-groups.json: ' + [...registrations.keys()].filter(name => !seen.has(name)).join(', '));
const mark = '<!-- BEGIN GENERATED NATIVE API -->';
const masterPath = 'docs/multiplayer-scripting.md';
const guide = read(masterPath).split(mark)[0].trimEnd() + '\n\n';
const overloads = [...registrations.values()].reduce((count, entries) => count + entries.length, 0);
let appendix = mark + '\n\n## Complete registered native API\n\n';
appendix += `This catalog is generated from the current registrations and client capability list: **${functions.length} names**, **${overloads} authority declarations**, and **${clientNames.size} client names**. Declarations are copied verbatim from code. They are signatures for functions scripts call, not lifecycle functions scripts implement.\n\n`;
appendix += 'Every listed native is registered in the authority engine. Registration does not guarantee complete multiplayer replication of an operation. The handling column records implemented routing; names without directed routing retain their existing native handling. **Client: Yes** means the separate client engine registers that native. **Client: No** means client source cannot call it.\n\n';
appendix += 'Client StringSub uses a different, copied string-handle declaration. The searchable HTML edition shows authority and client declarations separately. Regenerate this section and HTML with `node tools/generate-scripting-reference.cjs`; `--check` checks for drift. The generator reads routing from implementations and fails for missing/unclassified registrations.\n\n';
for (const group of groups) {
    appendix += `### ${group.title}\n\n| Declaration | Client | Implemented handling |\n|---|---|---|\n`;
    for (const fn of functions.filter(fn => fn.group === group.id)) {
        for (const declaration of registrations.get(fn.name)) {
            appendix += '| `' + declaration.declaration + '` | ' + (fn.client ? 'Yes' : 'No') + ' | ' + fn.handling + ' |\n';
        }
    }
    appendix += '\n';
}
appendix += '<!-- END GENERATED NATIVE API -->\n';
const esc = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const slug = value => value.toLowerCase().replace(/[^a-z0-9\s-]/g, '').trim().replace(/\s+/g,'-');
const inline = value => esc(value).replace(/`([^`]+)`/g,'<code>$1</code>').replace(/\*\*([^*]+)\*\*/g,'<strong>$1</strong>').replace(/\[([^\]]+)\]\(([^)]+)\)/g,'<a href="$2">$1</a>');
function render(markdown) {
    const lines = markdown.split(/\r?\n/); let html='', i=0;
    while (i < lines.length) {
        const line = lines[i];
        if (!line.trim() || line.startsWith('<!--')) { i++; continue; }
        if (line.startsWith('```')) { const code=[]; i++; while(i<lines.length&&!lines[i].startsWith('```')) code.push(lines[i++]); i++; html+='<pre><code>'+esc(code.join('\n'))+'</code></pre>'; continue; }
        const heading = line.match(/^(#{1,6}) (.+)$/);
        if (heading) { html+=`<h${heading[1].length} id="${slug(heading[2])}">${inline(heading[2])}</h${heading[1].length}>`; i++; continue; }
        if (line.startsWith('|')) { const rows=[]; while(i<lines.length&&lines[i].startsWith('|')) { const row=lines[i++].split('|').slice(1,-1).map(cell=>cell.trim()); if(!row.every(cell=>/^:?-+:?$/.test(cell))) rows.push(row); } html+='<div class="table-scroll"><table>'+rows.map((row,n)=>'<tr>'+row.map(cell=>`<${n?'td':'th'}>${inline(cell)}</${n?'td':'th'}>`).join('')+'</tr>').join('')+'</table></div>'; continue; }
        if (/^(- |\d+\. )/.test(line)) { const ordered=/^\d/.test(line), tag=ordered?'ol':'ul', pattern=ordered?/^\d+\. /:/^- /; html+='<'+tag+'>'; while(i<lines.length&&pattern.test(lines[i])) html+='<li>'+inline(lines[i++].replace(pattern,''))+'</li>'; html+='</'+tag+'>'; continue; }
        const paragraph=[line]; i++; while(i<lines.length&&lines[i].trim()&&!/^(#|\||- |\d+\. |```|<!--)/.test(lines[i])) paragraph.push(lines[i++]); html+='<p>'+inline(paragraph.join(' '))+'</p>';
    }
    return html;
}
const css = `:root{--paper:#f5f3ed;--panel:#fffefa;--ink:#202c31;--muted:#59666a;--line:#d9ded7;--accent:#146861}*{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font:15px/1.65 'Segoe UI',Arial,sans-serif}a{color:var(--accent)}header{background:#173b38;color:#fff;padding:34px max(5vw,calc((100vw - 1200px)/2))}header h1{margin:0;font-size:38px;line-height:1.2}header p{max-width:850px;color:#dde6dc}main{max-width:1200px;margin:auto;padding:25px 5vw 60px}h2{margin-top:45px;padding-top:15px;border-top:1px solid var(--line)}h3{margin-top:27px}pre{background:#e9eee6;border-radius:7px;padding:16px;overflow:auto}code{font:12px/1.65 Consolas,monospace}p code,li code,td code{background:#e9eee6;padding:2px 4px;border-radius:3px}.table-scroll{overflow:auto}table{border-collapse:collapse;width:100%;font-size:13px}th,td{text-align:left;vertical-align:top;padding:11px;border-bottom:1px solid var(--line)}th{background:#e9eee6}.filters{display:flex;gap:12px;flex-wrap:wrap;position:sticky;top:0;background:var(--paper);padding:15px 0;border-bottom:1px solid var(--line);z-index:2}input,select,button{font:inherit;background:var(--panel);border:1px solid #b7c7bc;border-radius:7px;padding:10px}input{flex:1;min-width:220px}button{cursor:pointer}.count{color:var(--muted);font-size:13px}.api-card{margin:9px 0;border:1px solid var(--line);border-radius:8px;background:var(--panel)}summary{cursor:pointer;padding:13px 16px;font:600 13px Consolas,monospace}.api-body{padding:0 16px 13px}.client{float:right;font:12px 'Segoe UI',sans-serif;color:var(--accent)}.source{font-size:12px;color:var(--muted)}[hidden]{display:none!important}@media(max-width:700px){header h1{font-size:28px}main{padding:15px 5vw}th,td{padding:8px}select{max-width:100%}}@media print{header{background:white;color:#173b38;padding:0}.filters{display:none}main{padding:0}details .api-body{display:block}.api-card{break-inside:avoid}}`;
const cards = groups.map(group=>`<section class="api-group" data-group="${group.id}" id="api-${group.id}"><h3>${esc(group.title)}</h3>${functions.filter(fn=>fn.group===group.id).map(fn=>`<details class="api-card" data-name="${fn.name}" data-client="${Boolean(fn.client)}" data-group="${group.id}"><summary>${fn.name}<span class="client">${fn.client?'Client API':'Authority only'}</span></summary><div class="api-body"><p><strong>Authority declaration</strong></p><pre><code>${esc(fn.authority)}</code></pre>${fn.client?'<p><strong>Client declaration</strong></p><pre><code>'+esc(fn.client)+'</code></pre>':''}<p>${esc(fn.handling)}</p><p class="source">Registration: LuxScriptHandler.cpp:${fn.line}</p></div></details>`).join('')}</section>`).join('');
const html = `<!doctype html>\n<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Amnesia64 multiplayer scripting</title><style>${css}</style></head><body><header><h1>Multiplayer scripting · Version 2</h1><p>The master implementation reference: authoring, ownership, lifecycle, protocol, limits and the complete native API.</p><p><a style="color:#d1ebe2" href="multiplayer-scripting.md">Markdown source</a></p></header><main>${render(guide.replace(/^# .+\n/,''))}<h2 id="complete-registered-native-api">Complete registered native API</h2><p>${functions.length} names · ${overloads} authority declarations · ${clientNames.size} client names. Client availability is explicit; authority registration alone does not imply full multiplayer synchronization.</p><div class="filters"><input id="query" type="search" placeholder="Find a function, parameter or behavior" aria-label="Search native API"><select id="client" aria-label="Execution domain"><option value="">All natives</option><option value="true">Client API</option><option value="false">Authority only</option></select><select id="group" aria-label="Native group"><option value="">All groups</option>${groups.map(group=>`<option value="${group.id}">${esc(group.title)}</option>`).join('')}</select><button id="expand" type="button">Expand visible</button></div><p id="count" class="count" role="status" aria-live="polite"></p><p id="empty" hidden>No matching natives.</p>${cards}</main><script id="api-data" type="application/json">${JSON.stringify(functions).replace(/</g,'\\u003c')}</script><script>
const data=JSON.parse(document.getElementById('api-data').textContent),search=new Map(data.map(fn=>[fn.name,JSON.stringify(fn).toLowerCase()]));
const cards=[...document.querySelectorAll('.api-card')],groups=[...document.querySelectorAll('.api-group')],query=document.getElementById('query'),client=document.getElementById('client'),group=document.getElementById('group');
function filter(){const words=query.value.toLowerCase().trim().split(/\\s+/).filter(Boolean);let count=0;for(const card of cards){card.hidden=Boolean((client.value&&card.dataset.client!==client.value)||(group.value&&card.dataset.group!==group.value)||!words.every(word=>search.get(card.dataset.name).includes(word)));if(!card.hidden)count++;}for(const section of groups)section.hidden=![...section.querySelectorAll('.api-card')].some(card=>!card.hidden);document.getElementById('count').textContent='Showing '+count+' of '+cards.length+' natives';document.getElementById('empty').hidden=count!==0;}
for(const field of [query,client,group])field.addEventListener('input',filter);document.getElementById('expand').addEventListener('click',()=>{const visible=cards.filter(card=>!card.hidden),open=!visible.every(card=>card.open);visible.forEach(card=>card.open=open);document.getElementById('expand').textContent=open?'Collapse visible':'Expand visible';});filter();
</script></body></html>\n`;
function output(file, content) {
    if (process.argv.includes('--check')) { if (read(file).replace(/\r\n/g,'\n') !== content.replace(/\r\n/g,'\n')) throw Error('Generated reference differs: '+file); }
    else fs.writeFileSync(path.join(root,file),content);
}
output(masterPath, guide + appendix);
output('docs/multiplayer-scripting.html', html);
console.log(JSON.stringify({names:functions.length,authorityDeclarations:overloads,clientNames:clientNames.size,checked:process.argv.includes('--check')}));
