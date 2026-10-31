import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const source=fs.readFileSync(new URL('../src/scripts/browser-input.js',import.meta.url),'utf8');
function fixture(orientation='white') {
  const events=[];
  class Event {constructor(type,options){this.type=type;Object.assign(this,options);this.isTrusted=false;}}
  const target={dispatchEvent:event=>{events.push(event);return true;}};
  const board={isConnected:true,contains:el=>el===target,getBoundingClientRect:()=>({left:20,top:30,width:640,height:640})};
  const document={querySelector:()=>board,elementFromPoint:()=>target};
  const window={location:{hostname:'www.chess.com'},MouseEvent:Event,PointerEvent:Event,setTimeout:fn=>fn()};
  const context={};vm.runInNewContext(source,context);
  const state={session:'tab-1',placement:'position',orientation};
  const command={id:'move-1',move:'e2e4',session:'tab-1',placement:'position',orientation,delay:180};
  return {events,board,document,window,state,command,input:context.MephistoBrowserInput};
}
for(const orientation of ['white','black']){
  const f=fixture(orientation);
  const result=await f.input.perform(f.command,f.state,f.document,f.window);
  assert.equal(result.status,'sent');
  const down=f.events.filter(e=>e.type==='mousedown');assert.equal(down.length,2);
  assert.equal(down[0].clientX,orientation==='white'?380:300);
  assert.equal(down[0].clientY,orientation==='white'?550:150);
  assert.equal(down[1].clientY,orientation==='white'?390:310);
  assert.ok(f.events.every(e=>e.isTrusted===false));
}
{
  const f=fixture();f.command.session='other-tab';
  assert.equal((await f.input.perform(f.command,f.state,f.document,f.window)).status,'rejected');assert.equal(f.events.length,0);
}
{
  const f=fixture();
  const result=await f.input.perform(f.command,f.state,f.document,f.window,()=>({...f.state,placement:'changed'}));
  assert.equal(result.status,'rejected');assert.equal(f.events.filter(e=>e.type==='mousedown').length,1);
}
{
  const f=fixture();f.command.destinationOnly=true;
  assert.equal((await f.input.perform(f.command,f.state,f.document,f.window)).status,'sent');
  assert.equal(f.events.filter(e=>e.type==='mousedown').length,1);
}
console.log('Browser input verified: orientation, event sequence, stale-session/position cancellation, destination retry.');
