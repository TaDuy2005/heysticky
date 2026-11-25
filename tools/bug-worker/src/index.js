// HeySticky bug reports by email, at sticky.davidtalab.cc (Figma 08a-08c). No sign-in: the email link proves the sender.
//   GET  /r?id&v&l&t&b  08a form, filled in from the Sticky's QR (04q) or the phone page (06u)
//   POST /r             checks the address, keeps the report 30 min in KV, emails a confirm link -> 08b
//   POST /again         08b Send again: the same link
//   GET  /c?t=…         opens the public GitHub issue (bot GitHub App), emails the owner (Reply-To: the sender) -> 08c
// Bindings (wrangler.jsonc): PENDING (KV), EMAIL (send_email), LIMIT (rate limit).
// Secrets: OWNER_EMAIL, GH_APP_ID, GH_APP_KEY (the App's .pem). Without the GitHub ones the owner still gets the email.
import { esc, readForm, issueBody, newId, ID_RE, appJwt, TITLE_MAX, DESC_MAX } from './lib.js';

const TTL = 30 * 60;  // seconds a confirm link works

const T = {
  en: {
    head: 'Report by email', email: 'Your email', title: 'Title', desc: 'What went wrong?', send: 'Send', report: 'Report',
    note: 'We email you a link to confirm it’s you. Your email stays private; the report is posted publicly on GitHub.',
    temp: 'Temporary email addresses can’t be used. Try another one.', nomx: 'This email address can’t get mail. Try another one.',
    bademail: 'Check the email address.', badtitle: `Add a title (up to ${TITLE_MAX} characters).`, baddesc: 'Say what went wrong (up to 2,000 characters).',
    busy: 'Too many tries. Wait a minute and try again.', mailfail: 'Couldn’t send the email. Try again later.',
    inbox: 'Check your inbox', sent: e => `We sent a link to ${e}. Open it to send your report.`, works: 'The link works for 30 minutes.',
    again: 'Send again', againDone: 'Sent again.',
    ok: 'Report sent', thanks: id => `Report ${id}. Thanks! Follow it in My reports on your Sticky.`, done: 'Done', close: 'You can close this page.',
    expired: 'This link has expired', rescan: 'Scan the QR on your Sticky again.',
    fail: 'Couldn’t send the report', later: 'Open the link again in a minute.',
    subject: 'Confirm your HeySticky bug report',
    mail: (id, link) => `Open this link to send your report ${id}:\n\n${link}\n\nThe link works for 30 minutes. If you didn’t ask for this, ignore this email.`,
  },
  vi: {
    head: 'Báo lỗi qua email', email: 'Email của bạn', title: 'Tiêu đề', desc: 'Lỗi gì vậy?', send: 'Gửi', report: 'Báo cáo',
    note: 'Chúng tôi gửi liên kết để xác nhận đó là bạn. Email được giữ kín; báo cáo được đăng công khai trên GitHub.',
    temp: 'Không dùng được email tạm thời. Hãy thử email khác.', nomx: 'Email này không nhận được thư. Hãy thử email khác.',
    bademail: 'Kiểm tra lại địa chỉ email.', badtitle: `Thêm tiêu đề (tối đa ${TITLE_MAX} ký tự).`, baddesc: 'Hãy nói lỗi gặp phải (tối đa 2.000 ký tự).',
    busy: 'Thử quá nhiều lần. Chờ một phút rồi thử lại.', mailfail: 'Không gửi được email. Hãy thử lại sau.',
    inbox: 'Kiểm tra hộp thư', sent: e => `Đã gửi liên kết tới ${e}. Mở nó để gửi báo cáo.`, works: 'Liên kết dùng được trong 30 phút.',
    again: 'Gửi lại', againDone: 'Đã gửi lại.',
    ok: 'Đã gửi báo cáo', thanks: id => `Báo cáo ${id}. Cảm ơn bạn! Theo dõi ở Báo cáo của tôi trên Sticky.`, done: 'Xong', close: 'Bạn có thể đóng trang này.',
    expired: 'Liên kết đã hết hạn', rescan: 'Hãy quét lại mã QR trên Sticky.',
    fail: 'Không gửi được báo cáo', later: 'Hãy mở lại liên kết sau một phút.',
    subject: 'Xác nhận báo cáo lỗi HeySticky',
    mail: (id, link) => `Mở liên kết này để gửi báo cáo ${id}:\n\n${link}\n\nLiên kết dùng được trong 30 phút. Nếu bạn không yêu cầu, hãy bỏ qua email này.`,
  },
};

const CSS = `*{box-sizing:border-box}body{margin:0;background:#fff;color:#000;font:16px/1.3 "JetBrains Mono",ui-monospace,Menlo,monospace}
header{background:#000;color:#fff;height:90px;padding:0 20px;display:flex;align-items:center}header b{font-size:26px}
main{padding:20px;max-width:560px}h1{font-size:22px;margin:4px 0 14px}label{display:block;font-size:14px;font-weight:700;margin:16px 0 8px}
input,textarea{width:100%;font:inherit;padding:10px 14px;border:2px solid #000;border-radius:8px;background:#fff;color:#000}input{height:47px}
textarea{height:130px;resize:vertical}.err{font-size:14px;font-weight:700;margin:8px 0 0}.bad{border-width:3px}
.sm{font-size:13px;line-height:1.4;margin:16px 0}.btn{display:block;width:100%;height:54px;font:inherit;font-size:18px;font-weight:700;border:2px solid #000;border-radius:8px;background:#000;color:#fff;cursor:pointer}
.btn.o{background:#fff;color:#000}p{margin:0 0 14px}.mid{min-height:calc(100vh - 90px);display:flex;flex-direction:column;justify-content:center;text-align:center;padding:20px;max-width:560px}
.ok{width:96px;height:96px;border-radius:50%;background:#000;margin:0 auto 18px;display:flex;align-items:center;justify-content:center}.mid h1{margin:0 0 12px}`;

function page(l, body, status = 200) {
  return new Response(`<!doctype html><html lang="${l}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>HeySticky</title><style>${CSS}</style></head><body><header><b>HeySticky</b></header>${body}</body></html>`, {
    status, headers: {
      'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store', 'referrer-policy': 'no-referrer',
      'content-security-policy': "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'",
    },
  });
}

// 08a; err: the key of the message shown under its field.
function form(r, err = '') {
  const t = T[r.l], e = k => err && ({ email: ['temp', 'nomx', 'bademail', 'busy', 'mailfail'], title: ['badtitle'], desc: ['baddesc'] }[k]).includes(err)
    ? `<p class="err">${esc(t[err])}</p>` : '';
  const bad = k => e(k) ? ' class="bad"' : '';
  return page(r.l, `<main><h1>${esc(t.head)}</h1><form method="post" action="/r">
<label for="email">${esc(t.email)}</label><input id="email" name="email" type="email" required maxlength="254" autocomplete="email" placeholder="you@gmail.com" value="${esc(r.email)}"${bad('email')}>${e('email')}
<label for="title">${esc(t.title)}</label><input id="title" name="title" required maxlength="${TITLE_MAX}" value="${esc(r.title)}"${bad('title')}>${e('title')}
<label for="desc">${esc(t.desc)}</label><textarea id="desc" name="desc" required maxlength="${DESC_MAX}"${bad('desc')}>${esc(r.desc)}</textarea>${e('desc')}
<input type="hidden" name="id" value="${esc(r.id)}"><input type="hidden" name="v" value="${esc(r.v)}"><input type="hidden" name="l" value="${r.l}">
<p class="sm">${esc(t.note)}</p><button class="btn">${esc(t.send)}</button></form>
<p class="sm">${esc(t.report)} ${esc(r.id)}${r.v ? ' · HeySticky ' + esc(r.v) : ''}</p></main>`, err ? 400 : 200);
}

function inbox(l, email, token, again = false) {  // 08b
  const t = T[l];
  return page(l, `<main><h1>${esc(t.inbox)}</h1><p>${esc(t.sent(email))}</p><p>${esc(t.works)}</p>
<form method="post" action="/again"><input type="hidden" name="t" value="${esc(token)}"><button class="btn o">${esc(t.again)}</button></form>
${again ? `<p class="sm">${esc(t.againDone)}</p>` : ''}</main>`);
}

function message(l, title, text, ok = false, status = 200) {  // 08c, and the expired / failed pages
  const t = T[l];
  const tick = ok ? '<div class="ok"><svg width="52" height="52" viewBox="0 0 24 24" fill="none" stroke="#fff" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12.5l4.5 4.5L19 7.5"/></svg></div>' : '';
  const done = ok ? `<button class="btn" onclick="window.close();setTimeout(()=>{this.outerHTML='<p>${esc(t.close)}</p>'},300)">${esc(t.done)}</button>` : '';
  return page(l, `<div class="mid">${tick}<h1>${esc(title)}</h1><p>${esc(text)}</p>${done}</div>`, status);
}

async function allowed(env, key) {
  return !env.LIMIT || (await env.LIMIT.limit({ key })).success;
}

async function hasMail(domain) {  // an MX record that isn't the "no mail" one; DNS trouble doesn't block anyone
  try {
    const r = await fetch(`https://cloudflare-dns.com/dns-query?name=${encodeURIComponent(domain)}&type=MX`, { headers: { accept: 'application/dns-json' } });
    const j = await r.json();
    return j.Status === 0 && (j.Answer || []).some(a => a.type === 15 && !/^0 \.$/.test(a.data));
  } catch { return true; }
}

async function sendConfirm(env, origin, p, token) {
  const t = T[p.l], link = `${origin}/c?t=${token}`;
  await env.EMAIL.send({
    from: { email: env.FROM, name: 'HeySticky' }, to: p.email, subject: `${t.subject} ${p.id}`,
    text: t.mail(p.id, link),
    html: `<p>${esc(t.mail(p.id, '')).split('\n\n')[0]}</p><p><a href="${esc(link)}">${esc(link)}</a></p><p>${esc(t.works)}</p>`,
  });
}

function gh(path, token, init = {}) {
  return fetch('https://api.github.com' + path, {
    ...init, headers: {
      authorization: `Bearer ${token}`, accept: 'application/vnd.github+json', 'user-agent': 'heysticky-bug-worker',
      'x-github-api-version': '2022-11-28', ...(init.body ? { 'content-type': 'application/json' } : {}),
    },
  });
}
async function openIssue(env, p) {  // the bot App: Issues read/write, installed on the repo only
  const jwt = await appJwt(env.GH_APP_ID, env.GH_APP_KEY);
  const inst = await gh(`/repos/${env.REPO}/installation`, jwt);
  if (!inst.ok) throw new Error(`installation ${inst.status}`);
  const tok = await gh(`/app/installations/${(await inst.json()).id}/access_tokens`, jwt, { method: 'POST' });
  if (!tok.ok) throw new Error(`token ${tok.status}`);
  const res = await gh(`/repos/${env.REPO}/issues`, (await tok.json()).token, { method: 'POST', body: JSON.stringify({ title: p.title, body: issueBody(p) }) });
  if (!res.ok) throw new Error(`issue ${res.status} ${await res.text()}`);
  return (await res.json()).html_url;
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url), q = url.searchParams, ip = req.headers.get('cf-connecting-ip') || '';
    const lang = (q.get('l') === 'vi') ? 'vi' : 'en';

    if (req.method === 'GET' && (url.pathname === '/r' || url.pathname === '/')) {
      const id = ID_RE.test(q.get('id') || '') ? q.get('id') : newId(), v = /^\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(q.get('v') || '') ? q.get('v') : '';
      return form({ email: '', title: (q.get('t') || '').slice(0, TITLE_MAX), desc: (q.get('b') || '').slice(0, DESC_MAX), id, v, l: lang });
    }

    if (req.method === 'POST' && url.pathname === '/r') {
      const { r, bad } = readForm(await req.formData());
      if (bad) return form(r, { email: 'bademail', temp: 'temp', title: 'badtitle', desc: 'baddesc' }[bad]);
      if (!(await allowed(env, 'ip:' + ip)) || !(await allowed(env, 'to:' + r.email))) return form(r, 'busy');
      if (!(await hasMail(r.email.split('@')[1]))) return form(r, 'nomx');
      const token = crypto.randomUUID().replace(/-/g, '');
      await env.PENDING.put(token, JSON.stringify(r), { expirationTtl: TTL });
      try { await sendConfirm(env, url.origin, r, token); } catch (e) { console.log('confirm mail', e.message); return form(r, 'mailfail'); }
      return inbox(r.l, r.email, token);
    }

    if (req.method === 'POST' && url.pathname === '/again') {
      const token = String((await req.formData()).get('t') || '');
      const p = /^[0-9a-f]{32}$/.test(token) && await env.PENDING.get(token, 'json');
      if (!p || p.done) return message(lang, T[lang].expired, T[lang].rescan, false, 410);
      if (!(await allowed(env, 'ip:' + ip)) || !(await allowed(env, 'to:' + p.email))) return message(p.l, T[p.l].busy, '', false, 429);
      try { await sendConfirm(env, url.origin, p, token); } catch (e) { console.log('again mail', e.message); return message(p.l, T[p.l].mailfail, '', false, 502); }
      return inbox(p.l, p.email, token, true);
    }

    if (req.method === 'GET' && url.pathname === '/c') {
      const token = q.get('t') || '';
      const p = /^[0-9a-f]{32}$/.test(token) && await env.PENDING.get(token, 'json');
      if (!p) return message(lang, T[lang].expired, T[lang].rescan, false, 410);
      const t = T[p.l];
      if (p.done) return message(p.l, t.ok, t.thanks(p.id), true);  // opened twice: still sent once
      // ponytail: two opens in the same second can both get here (KV isn't a lock); a Durable Object would make it exact.
      let issue = '';
      if (env.GH_APP_ID && env.GH_APP_KEY) {
        try { issue = await openIssue(env, p); } catch (e) { console.log('github', e.message); return message(p.l, t.fail, t.later, false, 502); }
      }
      await env.PENDING.put(token, JSON.stringify({ done: true, id: p.id, l: p.l }), { expirationTtl: TTL });
      if (env.OWNER_EMAIL) {
        try {
          await env.EMAIL.send({
            from: { email: env.FROM, name: 'HeySticky bug report' }, to: env.OWNER_EMAIL, replyTo: p.email,
            subject: `[HeySticky] ${p.id}: ${p.title}`,
            text: `${p.desc}\n\n---\nReport ${p.id}${p.v ? ', HeySticky ' + p.v : ''}\nFrom: ${p.email}\nIssue: ${issue || '(none: the GitHub App is not set up)'}`,
          });
        } catch (e) { console.log('owner mail', e.message); }
      }
      return message(p.l, t.ok, t.thanks(p.id), true);
    }

    return new Response('Not found', { status: 404 });
  },
};
