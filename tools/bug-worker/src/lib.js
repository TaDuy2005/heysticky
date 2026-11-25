// Pure helpers for the bug-report Worker (no I/O), checked by test.mjs.

export const ID_RE = /^HS-[2-9A-HJKMNP-Z]{4}$/;  // report.h REPORT_ABC
export const TITLE_MAX = 120, DESC_MAX = 2000;

const ABC = '23456789ABCDEFGHJKMNPQRSTUVWXYZ';
export function newId() {
  const b = crypto.getRandomValues(new Uint32Array(1))[0];
  let s = 'HS-';
  for (let i = 0, r = b; i < 4; i++, r = Math.floor(r / 31)) s += ABC[r % 31];
  return s;
}

export const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);

export function emailOk(e) {
  return e.length <= 254 && /^[^\s@<>()",;:]{1,64}@[a-z0-9-]+(\.[a-z0-9-]+)*\.[a-z]{2,}$/i.test(e);
}

// ponytail: a short list of the big throwaway-mail services; the "no mail server" check catches many others.
// Upgrade path: load a maintained list (github.com/disposable-email-domains) into KV.
const THROWAWAY = new Set(`10minutemail.com 10minutemail.net 20minutemail.com 33mail.com burnermail.io discard.email dispostable.com
emailondeck.com fakeinbox.com fakemail.net getairmail.com getnada.com guerrillamail.com guerrillamail.net guerrillamail.org
guerrillamailblock.com sharklasers.com grr.la harakirimail.com inboxkitten.com maildrop.cc mailinator.com mailinator.net
mailnesia.com mailpoof.com mintemail.com moakt.com mohmal.com mytemp.email nada.email spam4.me spamgourmet.com temp-mail.org
temp-mail.io tempail.com tempmail.com tempmail.net tempmailo.com tempr.email throwawaymail.com trashmail.com trashmail.de
yopmail.com yopmail.fr yopmail.net emailfake.com tmpmail.org tmpmail.net mail.tm mailto.plus 1secmail.com 1secmail.org
1secmail.net dropmail.me minuteinbox.com`.split(/\s+/));
export function throwaway(domain) {
  const d = domain.toLowerCase();
  for (const t of THROWAWAY) if (d === t || d.endsWith('.' + t)) return true;
  return false;
}

// The report as it is posted: a public issue, so @names don't ping anyone and the ID line is what 04u searches for.
export function issueBody(r) {
  const text = r.desc.replace(/@/g, '@​');
  return `${text}\n\n---\nReport ${r.id}${r.v ? ', HeySticky ' + r.v : ''}, sent by email`;
}

// Form fields -> a clean report, or the name of the field that is wrong.
export function readForm(f) {
  const s = k => String(f.get(k) || '').trim();
  const r = { email: s('email').toLowerCase(), title: s('title').replace(/\s+/g, ' '), desc: s('desc').replace(/\r\n?/g, '\n'), id: s('id'), v: s('v'), l: s('l') === 'vi' ? 'vi' : 'en' };
  if (!ID_RE.test(r.id)) r.id = newId();
  if (!/^\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(r.v)) r.v = '';
  if (!emailOk(r.email)) return { r, bad: 'email' };
  if (throwaway(r.email.split('@')[1])) return { r, bad: 'temp' };
  if (!r.title || r.title.length > TITLE_MAX) return { r, bad: 'title' };
  if (!r.desc || r.desc.length > DESC_MAX) return { r, bad: 'desc' };
  return { r, bad: '' };
}

// GitHub App keys come as PKCS#1 ("BEGIN RSA PRIVATE KEY"); WebCrypto reads PKCS#8, so wrap it.
export function pemToPkcs8(pem) {
  const der = Uint8Array.from(atob(pem.replace(/-----[^-]+-----|\s/g, '')), c => c.charCodeAt(0));
  if (/BEGIN PRIVATE KEY/.test(pem)) return der;
  const len = n => n < 128 ? [n] : n < 256 ? [0x81, n] : n < 65536 ? [0x82, n >> 8, n & 255] : [0x83, n >> 16, (n >> 8) & 255, n & 255];
  const algo = [0x02, 0x01, 0x00, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00];
  const oct = [0x04, ...len(der.length)];
  const body = algo.length + oct.length + der.length;
  return Uint8Array.from([0x30, ...len(body), ...algo, ...oct, ...der]);
}

const b64url = b => btoa(String.fromCharCode(...new Uint8Array(b))).replace(/=+$/, '').replace(/\+/g, '-').replace(/\//g, '_');
export async function appJwt(appId, pem, now = Math.floor(Date.now() / 1000)) {
  const key = await crypto.subtle.importKey('pkcs8', pemToPkcs8(pem), { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' }, false, ['sign']);
  const enc = o => b64url(new TextEncoder().encode(JSON.stringify(o)));
  const data = enc({ alg: 'RS256', typ: 'JWT' }) + '.' + enc({ iat: now - 60, exp: now + 540, iss: String(appId) });
  return data + '.' + b64url(await crypto.subtle.sign('RSASSA-PKCS1-v1_5', key, new TextEncoder().encode(data)));
}
