// node tools/bug-worker/test.mjs
import assert from 'node:assert/strict';
import { generateKeyPairSync, verify } from 'node:crypto';
import { esc, emailOk, throwaway, readForm, issueBody, newId, ID_RE, pemToPkcs8, appJwt } from './src/lib.js';

const fd = o => { const f = new FormData(); for (const [k, v] of Object.entries(o)) f.set(k, v); return f; };
const ok = { email: ' You@Gmail.com ', title: ' Clock   stops ', desc: 'Line 1\r\nLine 2', id: 'HS-7K2P', v: '1.2.0', l: 'vi' };

assert.equal(esc(`<a href="x">'&'</a>`), '&lt;a href=&quot;x&quot;&gt;&#39;&amp;&#39;&lt;/a&gt;');
assert(emailOk('a.b+c@mail.example.co') && !emailOk('a@b') && !emailOk('a b@c.com') && !emailOk('<a>@c.com'));
assert(throwaway('mailinator.com') && throwaway('X.YopMail.com') && !throwaway('gmail.com') && !throwaway('notmailinator.com'));

let { r, bad } = readForm(fd(ok));
assert.equal(bad, '');
assert.deepEqual(r, { email: 'you@gmail.com', title: 'Clock stops', desc: 'Line 1\nLine 2', id: 'HS-7K2P', v: '1.2.0', l: 'vi' });
assert.equal(readForm(fd({ ...ok, email: 'abc@10minutemail.com' })).bad, 'temp');
assert.equal(readForm(fd({ ...ok, email: 'nope' })).bad, 'email');
assert.equal(readForm(fd({ ...ok, title: '' })).bad, 'title');
assert.equal(readForm(fd({ ...ok, desc: 'x'.repeat(2001) })).bad, 'desc');
({ r } = readForm(fd({ ...ok, id: 'HS-0OIL', v: '1.2', l: 'fr' })));  // made-up ID / version / language: replaced
assert(ID_RE.test(r.id) && r.id !== 'HS-0OIL' && r.v === '' && r.l === 'en');
for (let i = 0; i < 50; i++) assert(ID_RE.test(newId()));

assert.equal(issueBody({ desc: 'ping @octocat', id: 'HS-7K2P', v: '1.2.0' }), 'ping @​octocat\n\n---\nReport HS-7K2P, HeySticky 1.2.0, sent by email');

// a GitHub App key (PKCS#1) signs a JWT GitHub can check
const { privateKey, publicKey } = generateKeyPairSync('rsa', { modulusLength: 2048 });
const pem1 = privateKey.export({ type: 'pkcs1', format: 'pem' }), pem8 = privateKey.export({ type: 'pkcs8', format: 'pem' });
assert.deepEqual(Buffer.from(pemToPkcs8(pem1)), Buffer.from(pem8.replace(/-----[^-]+-----|\s/g, ''), 'base64'));
const jwt = await appJwt(123, pem1, 1_700_000_000);
const [h, p, s] = jwt.split('.');
assert.deepEqual(JSON.parse(Buffer.from(p, 'base64url')), { iat: 1_699_999_940, exp: 1_700_000_540, iss: '123' });
assert(verify('sha256', Buffer.from(h + '.' + p), publicKey, Buffer.from(s, 'base64url')));

// the routes, with a fake KV, mailer and DNS: form -> confirm mail -> link -> owner mail, once
const kv = new Map(), sent = [];
const env = { FROM: 'report@sticky.test', REPO: 'o/r', OWNER_EMAIL: 'owner@test.dev',
  PENDING: { put: async (k, v) => kv.set(k, v), get: async (k, t) => kv.has(k) ? JSON.parse(kv.get(k)) : null },
  EMAIL: { send: async m => sent.push(m) } };
globalThis.fetch = async u => new Response(JSON.stringify(String(u).includes('nomail') ? { Status: 0 } : { Status: 0, Answer: [{ type: 15, data: '5 mx.gmail.com.' }] }));
const { default: w } = await import('./src/index.js');
const H = 'https://sticky.test';
let res = await w.fetch(new Request(H + '/r?id=HS-7K2P&v=1.2.0&l=vi&t=Clock+stops&b=%3Cb%3Ehi'), env);
let html = await res.text();
assert(res.ok && html.includes('value="Clock stops"') && html.includes('&lt;b&gt;hi') && html.includes('Báo lỗi qua email'));
const post = (path, o) => w.fetch(new Request(H + path, { method: 'POST', body: fd(o) }), env);
res = await post('/r', { ...ok, email: 'x@nomail.example' });
assert.equal(res.status, 400);
assert((await res.text()).includes('không nhận được thư') && sent.length === 0);
res = await post('/r', ok);
assert(res.ok && (await res.text()).includes('you@gmail.com') && sent.length === 1 && sent[0].to === 'you@gmail.com');
const link = sent[0].text.match(/https:\S+/)[0];
assert(link.startsWith(H + '/c?t='));
res = await w.fetch(new Request(link), env);
assert(res.ok && (await res.text()).includes('Đã gửi báo cáo'));
assert(sent.length === 2 && sent[1].to === 'owner@test.dev' && sent[1].replyTo === 'you@gmail.com' && sent[1].subject === '[HeySticky] HS-7K2P: Clock stops');
res = await w.fetch(new Request(link), env);  // opened again: no second report
assert(res.ok && sent.length === 2);
assert.equal((await w.fetch(new Request(H + '/c?t=' + 'f'.repeat(32)), env)).status, 410);
console.log('bug-worker: all passed');
