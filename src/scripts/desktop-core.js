// Collector only: no engine, synthetic events, settings UI or move execution.
(() => {
  const session = crypto.randomUUID();
  let sequence = 0, busy = false, previous = '', stableSince = 0;
  let pointer = null, pointerSequence = 0;
  let commandResult=null, lastCommandId='';
  document.addEventListener('mousemove', event => {
    if (event.isTrusted) pointer = {sequence: ++pointerSequence, x:event.clientX, y:event.clientY};
  }, {passive:true});
  function collect() {
    const cc = location.hostname.endsWith('chess.com');
    const board = document.querySelector(cc ? 'wc-chess-board, chess-board, .board' : '.main-board cg-board, .cg-wrap cg-board, cg-board, .chessground-board');
    if (!board) return null;
    const rect = board.getBoundingClientRect();
    if (rect.width < 100 || rect.height < 100 || rect.left < 0 || rect.top < 0 ||
        rect.right > innerWidth || rect.bottom > innerHeight) return null;
    if (board.querySelector('.anim, .dragging') || board.getAttribute('data-test-animating') === 'true') return null;
    const black = cc ? board.classList.contains('flipped') :
      !!board.closest('.orientation-black') || !!document.querySelector('.files.black');
    const pieces = {}, names = {pawn:'p', knight:'n', bishop:'b', rook:'r', queen:'q', king:'k'};
    for (const piece of board.querySelectorAll(cc ? '.piece' : 'piece')) {
      if (piece.classList.contains('ghost')) continue;
      let square, symbol;
      if (cc) {
        const pos = [...piece.classList].find(c => /^square-[1-8][1-8]$/.test(c));
        const type = [...piece.classList].find(c => /^[wb][pnbrqk]$/.test(c));
        if (!pos || !type) return null;
        square = String.fromCharCode(96 + Number(pos[7])) + pos[8];
        symbol = type[0] === 'w' ? type[1].toUpperCase() : type[1];
      } else {
        const p = piece.getBoundingClientRect();
        const x = Math.round((p.left - rect.left) / (rect.width / 8));
        const y = Math.round((p.top - rect.top) / (rect.height / 8));
        const type = [...piece.classList].map(c => names[c]).find(Boolean);
        if (!type || x < 0 || x > 7 || y < 0 || y > 7) return null;
        square = String.fromCharCode(97 + (black ? 7-x : x)) + (black ? y+1 : 8-y);
        symbol = piece.classList.contains('white') ? type.toUpperCase() : type;
      }
      if (pieces[square]) return null;
      pieces[square] = symbol;
    }
    if (Object.keys(pieces).length < 1) return null;
    const ranks = [];
    for (let rank = 8; rank >= 1; rank--) {
      let row = '', empty = 0;
      for (let file = 0; file < 8; file++) {
        const p = pieces[String.fromCharCode(97+file) + rank];
        if (!p) empty++; else { if (empty) row += empty; empty = 0; row += p; }
      }
      ranks.push(row + (empty || ''));
    }
    let records;
    if (cc) {
      records = [...document.querySelectorAll('.node')].map(n => n.lastElementChild || n);
      if (!records.length) records = [...document.querySelectorAll('.move-text-component')];
      if (!records.length) records = [...document.querySelectorAll('.move-text')];
    } else {
      records = [...document.querySelectorAll('kwdb')];
      if (!records.length) records = [...document.querySelectorAll('.tview2 move, .analyse__moves move')];
    }
    const moves = records.map(n => {
      const figurine = n.querySelector('[data-figurine]')?.getAttribute('data-figurine') || '';
      return figurine + n.innerText.split('\n')[0].trim();
    }).filter(Boolean);
    // Fallback turn requires a last-move destination occupied by a piece.
    let turn = '';
    for (const h of board.querySelectorAll(cc ? '.highlight' : 'square.last-move, .move-to, .move-from')) {
      let square;
      if (cc) {
        const c = [...h.classList].find(c => /^square-[1-8][1-8]$/.test(c));
        if (c) square = String.fromCharCode(96+Number(c[7])) + c[8];
      } else {
        const r = h.getBoundingClientRect();
        const x = Math.round((r.left-rect.left)/(rect.width/8)), y = Math.round((r.top-rect.top)/(rect.height/8));
        square = String.fromCharCode(97+(black?7-x:x)) + (black?y+1:8-y);
      }
      if (pieces[square]) turn = pieces[square] === pieces[square].toUpperCase() ? 'b' : 'w';
    }
    const placement = ranks.join('/');
    if (placement !== previous) { previous = placement; stableSince = performance.now(); return null; }
    if (performance.now() - stableSince < 180) return null;
    const scale = devicePixelRatio;
    // Firefox exposes the viewport origin, including vertical browser sidebars.
    const viewportX = Number.isFinite(window.mozInnerScreenX) ? window.mozInnerScreenX : screenX + (outerWidth-innerWidth)/2;
    const viewportY = Number.isFinite(window.mozInnerScreenY) ? window.mozInnerScreenY : screenY + outerHeight-innerHeight-(outerWidth-innerWidth)/2;
    const left = (viewportX + rect.left) * scale;
    const top = (viewportY + rect.top) * scale;
    let clockSeconds=-1;
    const clockText=document.querySelector(cc?'.clock-bottom .clock-time-monospace, .clock-bottom .clock-time-component, .clock-bottom':'.rclock-bottom .time')?.textContent || '';
    const clockMatch=clockText.match(/(\d{1,2}):(\d{2})(?::(\d{2}))?/);
    if(clockMatch)clockSeconds=clockMatch[3]?Number(clockMatch[1])*3600+Number(clockMatch[2])*60+Number(clockMatch[3]):Number(clockMatch[1])*60+Number(clockMatch[2]);
    return {session, sequence: ++sequence, url: location.href, title: document.title,
      placement, moves, turn, orientation: black?'black':'white',
      left, top, width: rect.width*scale, height: rect.height*scale,
      pointer, commandResult, visible:document.visibilityState==='visible', scale, clockSeconds, clientLeft:rect.left, clientTop:rect.top, clientWidth:rect.width, clientHeight:rect.height,
      pockets: [...document.querySelectorAll('.pocket piece, .pocket .piece, .pocket-top piece, .pocket-bottom piece')].map(el => {
        const r = el.getBoundingClientRect();
        const type = names[el.getAttribute('data-role')] || [...el.classList].map(c => names[c]).find(Boolean);
        const color = el.classList.contains('white') || el.getAttribute('data-color') === 'white' ? 'w' :
          el.classList.contains('black') || el.getAttribute('data-color') === 'black' ? 'b' : '';
        if (!type || !color || !r.width || !r.height) return null;
        return {type, color, clientX:r.left+r.width/2, clientY:r.top+r.height/2, x:left+(r.left-rect.left+r.width/2)*scale, y:top+(r.top-rect.top+r.height/2)*scale};
      }).filter(Boolean),
      focused: document.hasFocus(), promotion: ['q','r','b','n'].map(type => {
        const name = Object.keys(names).find(n => names[n] === type);
        const el = cc ? document.querySelector(`.promotion-piece.w${type}, .promotion-piece.b${type}`)
          : document.querySelector(`#promotion-choice piece.${name}`);
        if (!el) return null;
        const r = el.getBoundingClientRect();
        return {type, clientX:r.left+r.width/2, clientY:r.top+r.height/2, x:left+(r.left-rect.left+r.width/2)*scale, y:top+(r.top-rect.top+r.height/2)*scale};
      }).filter(Boolean)};
  }
  setInterval(async () => {
    if (busy) return;
    busy = true;
    try {
      const state = collect();
      if (state) {
        const response=await chrome.runtime.sendMessage({type:'desktop-state', state});
        const command=response?.browserCommand;
        if(command&&command.id!==lastCommandId){
          lastCommandId=command.id;
          const current=collect();
          commandResult=current?await globalThis.MephistoBrowserInput.perform(command,current,document,window,collect):{id:command.id,status:'rejected',reason:'Board is changing'};
        }
      }
    } catch (e) { /* Extension reload/disconnected host: retry next heartbeat. */ }
    finally { busy = false; }
  }, 250);
})();
