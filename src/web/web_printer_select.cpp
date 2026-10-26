#include "web/web_printer_select.h"

// Last on purpose: T() is a macro, see the pages.
#include "lang.h"

// The lists of the chosen link and model, as the printer page's cards have
// them: under WLAN the Brother and its DK labels only.
void webPrinterModelOptions(String& h, const LabelPrinterConfig& c) {
  int count = 0;
  const LabelPrinterModel* models = labelPrinterModels(labelPrinterLink(), &count);
  for (int i = 0; i < count; i++) {
    const LabelPrinterModel m = models[i];
    const LabelPrinterProfile& p = labelPrinterProfile(m);
    h += F("<option value='");
    h += String((int)m);
    h += F("'");
    if (c.model == m) h += F(" selected");
    h += F(">");
    h += p.brand; h += ' '; h += p.name;
    if (p.experimental) { h += ' '; h += T(STR_PRN_EXPERIMENTAL); }
    h += F("</option>");
  }
}

void webPrinterMediaOptions(String& h, const LabelPrinterConfig& c) {
  int count = 0;
  const LabelMediaSize* sizes = labelPrinterMediaSizes(c.model, &count);
  bool listed = false;
  for (int i = 0; i < count; i++) {
    const LabelMediaSize& s = sizes[i];
    const bool cur = s.width_mm == c.media_width_mm && s.length_mm == c.media_length_mm;
    listed = listed || cur;
    h += F("<option value='");
    h += String(s.width_mm); h += 'x'; h += String(s.length_mm);
    h += F("'");
    if (cur) h += F(" selected");
    h += F(">");
    h += String(s.width_mm); h += F(" x "); h += String(s.length_mm); h += F(" mm</option>");
  }
  if (!listed) {
    // A size set elsewhere and not in the table: shown so it is not
    // silently replaced, inert so it cannot be picked again.
    h += F("<option value='' selected disabled>");
    h += String(c.media_width_mm); h += F(" x "); h += String(c.media_length_mm); h += F(" mm</option>");
  }
}
