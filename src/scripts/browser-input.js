// DOM input stays inside the extension's target tab; it never moves the OS cursor.
globalThis.MephistoBrowserInput = {
  async perform(command, state, document, window, currentState=()=>state) {
    const fail = reason => ({id:command.id,status:'rejected',reason});
    if (command.session !== state.session || command.placement !== state.placement || command.orientation !== state.orientation)
      return fail('Position or orientation changed');
    if (!/^[a-h][1-8][a-h][1-8][qrbn]?$/.test(command.move)) return fail('Unsupported move');
    const cc=window.location.hostname.endsWith('chess.com');
    const board=document.querySelector(cc?'wc-chess-board, chess-board, .board':'.main-board cg-board, .cg-wrap cg-board, cg-board, .chessground-board');
    if(!board) return fail('Board not found');
    const bounds=board.getBoundingClientRect();
    if(bounds.width<100||bounds.height<100)return fail('Board has no layout');
    const center=square=>{
      const file=square.charCodeAt(0)-97,rank=Number(square[1])-1,black=state.orientation==='black';
      return {x:bounds.left+((black?7-file:file)+.5)*bounds.width/8,y:bounds.top+((black?rank:7-rank)+.5)*bounds.height/8};
    };
    const click=point=>{
      const target=document.elementFromPoint(point.x,point.y)||board;
      if(target!==board&&!board.contains(target))throw Error('Board is covered by another element');
      const options={view:window,bubbles:true,cancelable:true,composed:true,clientX:point.x,clientY:point.y,button:0};
      const pointer=(type,buttons)=>{
        if(window.PointerEvent)target.dispatchEvent(new window.PointerEvent(type,{...options,buttons,pointerId:1,pointerType:'mouse',isPrimary:true}));
      };
      target.dispatchEvent(new window.MouseEvent('mousemove',{...options,buttons:0}));
      pointer('pointerdown',1);target.dispatchEvent(new window.MouseEvent('mousedown',{...options,buttons:1}));
      pointer('pointerup',0);target.dispatchEvent(new window.MouseEvent('mouseup',{...options,buttons:0}));
      target.dispatchEvent(new window.MouseEvent('click',{...options,buttons:0}));
    };
    try {
      if(!command.destinationOnly)click(center(command.move.slice(0,2)));
      await new Promise(resolve=>window.setTimeout(resolve,Math.max(80,Math.min(1000,command.delay||180))));
      if(!board.isConnected)return fail('Board was replaced');
      const current=currentState(),nowBounds=board.getBoundingClientRect();
      if(!current||current.placement!==command.placement||current.orientation!==command.orientation)return fail('Position changed before destination click');
      if(['left','top','width','height'].some(key=>Math.abs(nowBounds[key]-bounds[key])>1))return fail('Board moved during input');
      click(center(command.move.slice(2,4)));
      if(command.move.length===5) {
        const type=command.move[4],names={q:'queen',r:'rook',b:'bishop',n:'knight'};
        for(let i=0;i<20;i++){
          const option=document.querySelector(cc?`.promotion-piece.w${type}, .promotion-piece.b${type}`:`#promotion-choice piece.${names[type]}`);
          if(option){const r=option.getBoundingClientRect();option.dispatchEvent(new window.MouseEvent('click',{view:window,bubbles:true,cancelable:true,clientX:r.left+r.width/2,clientY:r.top+r.height/2}));break;}
          await new Promise(resolve=>window.setTimeout(resolve,80));
        }
      }
      return {id:command.id,status:'sent'}; // Success still requires board/history confirmation.
    } catch(error){return {id:command.id,status:'error',reason:error.message};}
  }
};
