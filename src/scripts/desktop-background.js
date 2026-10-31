import {Chess} from '../../lib/chess.js';

const endpoint = 'http://127.0.0.1:17864/state';
let inFlight = false;
let variant = 'chess', configuredStart = '';
let confirmation = null;
const starts = new Map();
chrome.runtime.onMessage.addListener((message, sender, respond) => {
  if (message.type !== 'desktop-state' || !sender.tab) return;
  if (inFlight) { respond({ok:false}); return; }
  inFlight = true;
  (async () => {
    const tabs = await chrome.tabs.query({active:true, lastFocusedWindow:true});
    if (tabs[0]?.id !== sender.tab.id) throw Error('Inactive tab');
    const s = message.state;
    let fen = '', reliable = false, historyLan = [], legalCount=0;
    try {
      const key = `${s.session}/${variant}`;
      if (variant === 'fischerandom' && s.moves.length === 0 &&
          s.placement.split('/').slice(2,6).join('/') === '8/8/8/8' &&
          s.placement.split('/')[1] === 'pppppppp' && s.placement.split('/')[6] === 'PPPPPPPP') {
        starts.set(key, `${s.placement} w KQkq - 0 1`);
      }
      if (starts.size > 32) starts.delete(starts.keys().next().value);
      const start = configuredStart || starts.get(key);
      if (variant === 'fischerandom' && !start) throw Error('Chess960 starting FEN required');
      const chess = new Chess(variant, start);
      for (const san of s.moves) { const receipt=chess.move(san); if (!receipt) throw Error('Invalid SAN'); historyLan.push(receipt.lan); }
      if (chess.fen().split(' ')[0].replace(/\[.*\]/, '').replaceAll('~','') === s.placement) { fen = chess.fen(); reliable = true;legalCount=chess.moves().length; }
    } catch (e) { /* DOM may contain an incomplete or non-starting-position move list. */ }
    if (!fen && /^[wb]$/.test(s.turn)) fen = `${s.placement} ${s.turn} - - 0 1`;
    const response = await fetch(endpoint, {method:'POST', headers:{'Content-Type':'application/json'},
      body:JSON.stringify({...s, fen, reliable, legalCount, historyLan:reliable?historyLan:[], confirmation, variant, startFen:configuredStart, tabId:sender.tab.id}), signal:AbortSignal.timeout(1500)});
    if (!response.ok) throw Error('Desktop unavailable');
    const options = await response.json();
    if (options.variant) variant = options.variant;
    configuredStart = options.startFen || '';
    if (options.confirmMove) {
      const request = options.confirmMove;
      if (confirmation?.id !== request.id) {
        try {
          const chess = new Chess(request.variant, request.fen);
          const receipt=chess.move(request.move);
          if (!receipt) throw Error('Illegal move');
          confirmation={id:request.id, placement:chess.fen().split(' ')[0].replace(/\[.*\]/,'').replaceAll('~','')};
        } catch(e) { confirmation={id:request.id,error:e.message}; }
      }
    } else confirmation=null;
    return {ok:true};
  })().then(result => { inFlight = false; respond(result); },
    e => { inFlight = false; respond({ok:false, error:e.message}); });
  return true;
});
