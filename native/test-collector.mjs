import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const baseDirectory=process.env.MEPHISTO_EXTENSION_DIR ? new URL('../'+process.env.MEPHISTO_EXTENSION_DIR+'/',import.meta.url) : new URL('../',import.meta.url);
const source = fs.readFileSync(new URL('src/scripts/desktop-core.js',baseDirectory),'utf8');
const classes = (...items) => Object.assign(items,{contains:c => items.includes(c)});
function fixture({black=false,lichess=false,firefox=false,scale=1}={}) {
  let tick, now=0, payload, visible=true, focus=true, mouseMove;
  const pieces=[];
  const ranks=['RNBQKBNR','PPPPPPPP','','','','','pppppppp','rnbqkbnr'];
  const names={p:'pawn',n:'knight',b:'bishop',r:'rook',q:'queen',k:'king'};
  for(let rank=0;rank<8;rank++)for(let file=0;file<ranks[rank].length;file++) {
    const type=ranks[rank][file],color=type===type.toUpperCase()?'w':'b';
    pieces.push({classList:lichess?classes(color==='w'?'white':'black',names[type.toLowerCase()]):
      classes('piece','unrelated',`square-${file+1}${rank+1}`,color+type.toLowerCase()),
      getBoundingClientRect:()=>({left:20+(black?7-file:file)*80,top:30+(black?rank:7-rank)*80,width:80,height:80})});
  }
  const rect={left:20,top:30,width:640,height:640,right:660,bottom:670};
  const board={classList:classes(...(black?['flipped']:[])),
    closest:()=>black?{}:null, getAttribute:()=>null,
    querySelector:()=>null,getBoundingClientRect:()=>rect,
    querySelectorAll:selector=>selector==='piece'||selector==='.piece'?pieces:[]};
  const document={addEventListener:(name,handler)=>{if(name==='mousemove')mouseMove=handler;},get visibilityState(){return visible?'visible':'hidden';},hasFocus:()=>focus,title:'test',
    querySelector:selector=>selector.includes('cg-board')||selector.includes('wc-chess-board')?board:null,
    querySelectorAll:()=>[]};
  vm.runInNewContext(source,{crypto:{randomUUID:()=> 'session'},performance:{now:()=>now},
    location:{hostname:lichess?'lichess.org':'www.chess.com',href:'https://test/'},document,window:firefox?{mozInnerScreenX:400,mozInnerScreenY:90}:{},
    innerWidth:1000,innerHeight:800,outerWidth:1016,outerHeight:896,screenX:100,screenY:50,devicePixelRatio:scale,
    setInterval:fn=>tick=fn,[source.includes('browser.runtime')?'browser':'chrome']:{runtime:{sendMessage:async data=>{payload=data;}}}});
  return {tick:async()=>{payload=undefined;now+=250;await tick();return payload;},hide:()=>visible=false,blur:()=>focus=false,
    mouse:(x,y,trusted=true)=>mouseMove({clientX:x,clientY:y,isTrusted:trusted})};
}
const placement='rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR';
for(const options of [{},{black:true},{lichess:true},{lichess:true,black:true}]) {
  const f=fixture(options);assert.equal(await f.tick(),undefined);
  const {state}=await f.tick();assert.equal(state.placement,placement);
  assert.equal(state.orientation,options.black?'black':'white');
  assert.equal(state.left,128);assert.equal(state.top,168);
  assert.equal((await f.tick()).state.sequence,2);
  f.hide();assert.equal((await f.tick()).state.visible,false);
}
const unfocused=fixture();await unfocused.tick();unfocused.blur();assert.equal((await unfocused.tick()).state.focused,false);
const zen=fixture({firefox:true,scale:1.5});await zen.tick();zen.mouse(60,70);
let report=(await zen.tick()).state;
assert.equal(report.left,630);assert.equal(report.top,180);assert.equal(report.scale,1.5);
assert.equal(report.clientLeft,20);assert.equal(report.clientTop,30);assert.equal(report.clientWidth,640);
assert.equal(report.pointer.sequence,1);assert.equal(report.pointer.x,60);assert.equal(report.pointer.y,70);
zen.mouse(900,900,false);report=(await zen.tick()).state;assert.equal(report.pointer.sequence,1,'Synthetic DOM events must not confirm native input');
console.log('Collector tests passed: both sites/orientations, unordered CSS classes, geometry, stability, visibility/focus.');
