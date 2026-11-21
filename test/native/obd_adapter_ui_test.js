// UI logic regression test without a browser, framework or external packages.
// The embedded script is the source of truth: test only its adapter form helpers.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const raw = fs.readFileSync('src/web_ui.h','utf8');
const anchor = '// ---------- OBD adapter registry (M2c; configuration only) ----------';
const end = '// ---------- Einstellungen ----------';
assert.equal(raw.split(anchor).length, 2);
const block = raw.split(anchor)[1].split(end)[0];
const ctx = vm.createContext({});
vm.runInContext('function t(v){return v;}\n' + block +
 '\nglobalThis.__validate=obdValidate;' +
 '\nglobalThis.__equal=obdEqual;' +
 '\nglobalThis.__profiles=(p)=>{obdProfiles=p;};',ctx);
ctx.__profiles(['ioniq5','fiat_tipo']);
const nissan = {
 id:'nissan',vehicle_id:'nissan_qashqai',mac:'D2:E0:2F:8D:52:48',
 profile_id:'ioniq5',protocol:'profile',addr_type:'auto',enabled:true
};
const fiat = {
 id:'fiat',vehicle_id:'fiat_tipo',mac:'A1:B2:C3:D4:E5:F6',
 profile_id:'fiat_tipo',protocol:'6',addr_type:'auto',enabled:true
};
assert.equal(ctx.__validate([nissan]),'');
assert.equal(ctx.__validate([nissan,fiat]),'');
assert.equal(ctx.__validate([]),'');
assert.ok(ctx.__validate([nissan,{...nissan,id:'other'}]).includes('Duplicate'));
assert.ok(ctx.__validate([nissan,{...fiat,mac:nissan.mac}]).includes('Duplicate'));
assert.ok(ctx.__validate([{...nissan,vehicle_id:'BAD NAME'}]));
assert.ok(ctx.__validate([{...nissan,id:'current'}]));
assert.ok(ctx.__validate([{...nissan,mac:'AA:BB:CC:??:22:33'}]));
assert.ok(ctx.__validate([{...nissan,profile_id:'missing'}]));
assert.ok(ctx.__validate(Array.from({length:5},(_,i)=>({
 ...nissan,id:'car'+i,vehicle_id:'vehicle'+i,mac:'AA:BB:CC:DD:EE:0'+i
}))));
assert.equal(ctx.__equal([nissan,fiat],[{...nissan},{...fiat}]),true);
assert.equal(ctx.__equal([nissan],[{...nissan,enabled:false}]),false);
assert.equal(ctx.__equal([nissan],[{...nissan,protocol:'7'}]),false);
console.log('OBD adapter Web UI validation: PASS');
