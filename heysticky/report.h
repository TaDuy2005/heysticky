// Report a bug (Figma 04p-04t, 06u): the report ID and the GitHub and email links, pre-filled.
// No I/O here, so tools/report_test.cpp checks it on the Mac. The Sticky side is in settings_ui.h / screens.h,
// the phone side in phone.h. Email goes to REPORT_MAIL from the phone's mail app (a Gmail filter sends the auto reply).
#pragma once
#include <cctype>
#include "settings.h"

constexpr const char *REPORT_REPO = "TaDuy2005/heysticky";
constexpr const char *REPORT_MAIL = "heystickyreport@gmail.com";

// The report being sent: its ID, and the AI's title ("" when written on the phone).
struct Report {
  String id, title;
};

// "HS-7K2P": 4 characters with no 0/O, 1/I/L look-alikes, so it can be read off the screen.
constexpr char REPORT_ABC[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
inline String report_id(uint32_t r) {
  String s = "HS-";
  for (int i = 0; i < 4; i++, r /= 31) s += REPORT_ABC[r % 31];
  return s;
}

// Query-string value: spaces as "+" (shorter than %20, so the QR stays coarse), the rest of anything but
// A-Z a-z 0-9 - _ . ~ as %XX. mail: spaces as %20, since a mail app keeps "+" as a plus sign.
inline String q_enc(const String &s, bool mail = false) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    const uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += char(c);
    else if (c == ' ' && !mail) o += '+';
    else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); o += b; }
  }
  return o;
}

// What a blank report (no AI text) starts with, in the Sticky's language.
inline String report_guide(bool vi) {
  return vi ? "Lỗi gì đã xảy ra:\n\nCác bước để gặp lại lỗi:\n\nBạn mong đợi điều gì:\n\n(Bạn có thể đính kèm ảnh chụp màn hình.)"
            : "What happened:\n\nSteps to make it happen:\n\nWhat you expected:\n\n(You can attach a photo of the screen.)";
}

// 04p: GitHub's New issue page with the title and text (or the guide) filled in; the last line carries the ID.
// A blank report has no title, so GitHub asks for one.
inline String report_gh_url(const Report &r, const String &body, bool vi) {
  const String b = (body.length() ? body : report_guide(vi)) + "\n\nReport " + r.id + ", HeySticky " + FW_VERSION;
  return String("https://github.com/") + REPORT_REPO + "/issues/new?title=" + q_enc(r.title) + "&body=" + q_enc(b);
}

// 04q: the phone's mail app, to REPORT_MAIL; the subject carries the ID, the body the AI's title and text (or the guide).
inline String report_mail_url(const Report &r, const String &body, bool vi) {
  const String subj = "Confirmation Bug Report #" + r.id + ", HeySticky " + FW_VERSION;
  const String b = body.length() ? (r.title.length() ? r.title + "\n\n" : String("")) + body : report_guide(vi);
  return String("mailto:") + REPORT_MAIL + "?subject=" + q_enc(subj, true) + "&body=" + q_enc(b, true);
}
