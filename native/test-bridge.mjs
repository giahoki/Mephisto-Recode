import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const chessSource = fs.readFileSync(new URL('../lib/chess.js', import.meta.url), 'utf8');
const {Chess} = await import('data:text/javascript;base64,' + Buffer.from(chessSource).toString('base64'));
let listener, sent, activeId = 7, chosenVariant = 'chess', confirmRequest = null;
const baseDirectory=process.env.MEPHISTO_EXTENSION_DIR ? new URL('../'+process.env.MEPHISTO_EXTENSION_DIR+'/',import.meta.url) : new URL('../',import.meta.url);
const source = fs.readFileSync(new URL('src/scripts/desktop-background.js', baseDirectory), 'utf8')
  .replace("import {Chess} from '../../lib/chess.js';", '');
vm.runInNewContext(source, {Chess, AbortSignal,
  [source.includes('browser.runtime')?'browser':'chrome']:{runtime:{onMessage:{addListener:fn => listener = fn}},tabs:{query:async () => [{id:activeId}]}},
  fetch: async (url, options) => { sent = JSON.parse(options.body); return {ok:true,json:async()=>({variant:chosenVariant,startFen:'',confirmMove:confirmRequest})}; }});
const initial = new Chess('chess');
function send(state, tabId = 7) {
  sent = null;
  return new Promise(resolve => listener({type:'desktop-state',state},{tab:{id:tabId}},resolve));
}
const base = {session:'test',sequence:1,placement:initial.fen().split(' ')[0],moves:[],turn:'',promotion:[]};
assert.equal((await send(base)).ok,true);
assert.equal(sent.fen,initial.fen()); assert.equal(sent.reliable,true);
const e4 = new Chess('chess'); e4.move('e4');
assert.equal((await send({...base,placement:e4.fen().split(' ')[0],moves:['e4']})).ok,true);
assert.equal(sent.fen,e4.fen());
assert.deepEqual(Array.from(sent.historyLan),['e2e4']);
await send({...base,placement:e4.fen().split(' ')[0],moves:['bad'],turn:'b'});
assert.equal(sent.reliable,false); assert.match(sent.fen,/ b - - 0 1$/);
assert.equal((await send({...base,placement:e4.fen().split(' ')[0],moves:[]})).ok,true);
assert.equal(sent.fen,'','Unknown turn must not be analyzed as the starting position');
assert.equal((await send(base,8)).ok,false); assert.equal(sent,null);
for (const variant of ['crazyhouse','kingofthehill','3check','antichess','atomic','horde','racingkings','fischerandom']) {
  chosenVariant=variant;
  await send(base); // Synchronize desktop configuration.
  const game=new Chess(variant);
  await send({...base,placement:game.fen().split(' ')[0].replace(/\[.*\]/,'').replaceAll('~','')});
  assert.equal(sent.variant,variant);assert.equal(sent.reliable,true,variant+' initial position must replay');
  assert.equal(sent.fen,game.fen());
}
chosenVariant='chess';await send(base);
confirmRequest={id:'click-1',variant:'chess',fen:initial.fen(),move:'e2e4'};
await send(base);
await send({...base,placement:e4.fen().split(' ')[0],moves:['e4']});
assert.equal(sent.confirmation.id,'click-1');
assert.equal(sent.confirmation.placement,e4.fen().split(' ')[0]);
// A failed/illegal command must never manufacture a successful expected board.
confirmRequest={id:'illegal',variant:'chess',fen:initial.fen(),move:'e2e5'};
await send(base);await send(base);
assert.equal(sent.confirmation.id,'illegal');assert.ok(sent.confirmation.error);
console.log('Bridge tests passed: replay, FEN metadata, incomplete history, unknown turn, inactive tab.');
