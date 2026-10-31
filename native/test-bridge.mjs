import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const chessSource = fs.readFileSync(new URL('../lib/chess.js', import.meta.url), 'utf8');
const {Chess} = await import('data:text/javascript;base64,' + Buffer.from(chessSource).toString('base64'));
let listener, sent, activeId = 7, chosenVariant = 'chess', confirmRequest = null, selectedInputMode='mouse';
const baseDirectory=process.env.MEPHISTO_EXTENSION_DIR ? new URL('../'+process.env.MEPHISTO_EXTENSION_DIR+'/',import.meta.url) : new URL('../',import.meta.url);
const source = fs.readFileSync(new URL('src/scripts/desktop-background.js', baseDirectory), 'utf8')
  .replace("import {Chess} from '../../lib/chess.js';", '');
vm.runInNewContext(source, {Chess, AbortSignal,
  [source.includes('browser.runtime')?'browser':'chrome']:{runtime:{onMessage:{addListener:fn => listener = fn}},tabs:{query:async () => [{id:activeId}]}},
  fetch: async (url, options) => { sent = JSON.parse(options.body); return {ok:true,json:async()=>({variant:chosenVariant,startFen:'',confirmMove:confirmRequest,inputMode:selectedInputMode,browserCommand:{id:'background-input'}})}; }});
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
const repetitionGame=new Chess('chess');
const cycle=['Nf3','Nf6','Ng1','Ng8','Nf3','Nf6','Ng1'];
for(const san of cycle)repetitionGame.move(san);
await send({...base,moves:cycle,placement:repetitionGame.fen().split(' ')[0]});
assert.equal(sent.engineStartFen,initial.fen());assert.equal(sent.historyLan.length,7);
assert.ok(sent.repetitionMoves.includes('f6g8'),'The third occurrence must be detected before the move');
assert.equal(sent.threefold,false);assert.equal(sent.fen,repetitionGame.fen(),'Scanning candidates must preserve the position');
repetitionGame.move('Ng8');
await send({...base,moves:[...cycle,'Ng8'],placement:repetitionGame.fen().split(' ')[0]});
assert.equal(sent.threefold,true);
const screenshotMoves=['d4','d5','Nc3','Nf6','e4','Nxe4','Nxe4','dxe4','Bb5+','Nd7','Ne2','e6','Bf4','a6','Bxd7+','Bxd7','Nc3','Bc6','O-O','Qd7','Re1','O-O-O','Nxe4','Qxd4','Nd6+','Rxd6','Bxd6','Bxd6','c4','Qf4','b4','Bxb4','g3','Qf6','Rb1','Bxe1','Qxe1','Qf3','Kf1','Qg2+','Ke2','Qe4+','Kf1','Qg2+','Ke2','Qe4+','Kf1','Qh1+','Ke2'];
const screenshotGame=new Chess('chess');for(const move of screenshotMoves)screenshotGame.move(move);
await send({...base,moves:screenshotMoves,placement:screenshotGame.fen().split(' ')[0]});
assert.ok(sent.repetitionMoves.includes('h1e4'),'Regression: the screenshot game must detect 25...Qe4+ as repetition');
confirmRequest={id:'click-1',variant:'chess',fen:initial.fen(),move:'e2e4'};
await send(base);
await send({...base,placement:e4.fen().split(' ')[0],moves:['e4']});
assert.equal(sent.confirmation.id,'click-1');
assert.equal(sent.confirmation.placement,e4.fen().split(' ')[0]);
// A failed/illegal command must never manufacture a successful expected board.
confirmRequest={id:'illegal',variant:'chess',fen:initial.fen(),move:'e2e5'};
await send(base);await send(base);
assert.equal(sent.confirmation.id,'illegal');assert.ok(sent.confirmation.error);
confirmRequest=null;selectedInputMode='browser';
assert.equal((await send(base)).browserCommand.id,'background-input');
activeId=8;
assert.equal((await send(base,7)).ok,true,'Controlled game tab must keep working in the background');
assert.equal((await send(base,9)).ok,false,'An unrelated inactive game tab must not take control');
console.log('Bridge tests passed: replay, FEN metadata, incomplete history, unknown turn, inactive tab.');
