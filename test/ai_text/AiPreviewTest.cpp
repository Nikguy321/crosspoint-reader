// Apps > AI's answer page on the host, with the real renderer and fonts (SleepCardHost) and the
// survival guide's own layout and drawing (lib/Guide, GuideDraw, GuideMetrics), as AiPageActivity
// draws it: writes build/ai/*.png and checks that the page's text stays in the text column and the
// content band, nothing lands in the bezel columns (x 0..7, 472..479: the left-edge Back swipe), and
// the longest answer the app keeps (GEM_A_MAX, with a cut question and a note) lays out in full.
#include <I18n.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "CardPreview.h"
#include "GuideTestUtil.h"
#include "gemini.h"
#include "src/activities/apps/AiText.h"
#include "src/activities/apps/GuideDraw.h"
#include "src/activities/apps/GuideMetrics.h"

using sleepcards::preview::renderer;

namespace {

std::string outDir() {
  const std::string dir = std::string(CARD_PREVIEW_REPO_ROOT) + "/build/ai";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::string page(const char* question, const char* note, const char* body) {
  const size_t n = aitext::buildPage(question, note, body, nullptr, 0);
  std::string out(n + 1, '\0');
  aitext::buildPage(question, note, body, out.data(), out.size());
  out.resize(n);
  return out;
}

struct Shot {
  int screens = 0;
  bool truncated = false;
};

// Ink in [x0, x1] x [y0, y1].
bool inkIn(const int x0, const int y0, const int x1, const int y1) {
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      if (renderer().readPixel(x, y)) return true;
    }
  }
  return false;
}

// AiPageActivity's render, screen `sub` of the page, saved as build/ai/<name>.png.
Shot draw(const std::string& text, const char* lead, const char* section, const int sub, const std::string& name) {
  Shot shot;
  guidetest::Buf buf(text);
  auto topic = std::make_unique<gd::TopicText>();
  EXPECT_EQ(topic->parse(buf.data(), buf.len), gd::GpError::None) << text;
  auto layout = std::make_unique<gd::PageLayout>();
  GfxRenderer& r = renderer();
  const gd::RendererMetrics metrics(r, gd::pageFonts());
  shot.screens =
      gd::layoutPage(*topic, 0, lead && lead[0] ? lead : nullptr, metrics, gd::Geometry{}, nullptr, layout.get());
  shot.truncated = layout->truncated();
  char crumb[96];
  const char* parts[2] = {tr(STR_AI), section};
  gd::breadcrumb(parts, 2, metrics, gd::Font::Crumb, gd::draw::crumbWidth(), crumb, sizeof(crumb));

  r.clearScreen();
  gd::draw::drawCrumb(r, crumb, false);
  gd::draw::PageView view;
  view.layout = layout.get();
  view.screen = sub;
  view.fonts = &gd::pageFonts();
  view.missingLabel = "";
  gd::draw::drawPageBody(r, view);
  gd::draw::BarView bar;
  char count[16];
  std::snprintf(count, sizeof(count), "%d/%d", sub + 1, shot.screens);
  bar.prevLabel = tr(STR_GD_PREV);
  bar.prevEnabled = sub > 0;
  bar.middleLabel = tr(STR_GD_BACK);
  bar.middleDetail = count;
  bar.nextLabel = tr(STR_GD_NEXT);
  bar.nextEnabled = sub + 1 < shot.screens;
  gd::draw::drawBar(r, bar);
  EXPECT_TRUE(sleepcards::preview::writeFramePng(outDir() + "/" + name + ".png")) << name;

  // The content band's ink stays right of the left bezel (a two-digit step number and a glyph's
  // overhang reach a little left of the text column, never into the bezel) and inside the column on
  // the right; both bezel columns stay clear below the strip.
  const gd::Geometry g;
  EXPECT_TRUE(inkIn(g.left, g.top, g.left + g.width - 1, g.bottom - 1)) << name << ": an empty page";
  EXPECT_FALSE(inkIn(g.left + g.width, g.top, 479, g.bottom - 1)) << name << ": ink right of the text column";
  EXPECT_FALSE(inkIn(0, gd::draw::CRUMB_H, gd::draw::BEZEL - 1, 799)) << name << ": ink in the left bezel";
  EXPECT_FALSE(inkIn(480 - gd::draw::BEZEL, gd::draw::CRUMB_H, 479, 799)) << name << ": ink in the right bezel";
  return shot;
}

constexpr char ANSWER[] =
    "Not yet. Tomb Raider: Legacy of Atlantis is an upcoming game: it was revealed at The Game Awards 2025 and "
    "has no release date beyond 2027.\n\n"
    "What is known so far:\n"
    "- It is the second remake of the 1996 Tomb Raider, by Crystal Dynamics and Flying Wild Hog.\n"
    "- Amazon Game Studios publishes it.\n"
    "- Alix Wilton Regan plays Lara Croft.\n\n"
    "Tomb Raider: Catalyst, a new main entry, is due in 2028.\n\n"
    "Wikipedia: Tomb Raider: Legacy of Atlantis; Tomb Raider: Catalyst";

}  // namespace

TEST(AiPreview, AnswerPage) {
  const Shot shot = draw(page("Has the new Tomb Raider Legacy of Atlantis come out?", nullptr, ANSWER),
                         "Answered by Flash", "Flash", 0, "answer");
  EXPECT_EQ(shot.screens, 1);
  EXPECT_FALSE(shot.truncated);
}

TEST(AiPreview, FailedQuestion) {
  const Shot shot = draw(page("Who won the 2026 World Cup?", "Gemini did not answer in time", ""), "No answer",
                         "No answer", 0, "failed");
  EXPECT_EQ(shot.screens, 1);
}

TEST(AiPreview, LongestAnswerAndQuestion) {
  std::string body;
  for (int n = 0; body.size() < GEM_A_MAX - 200; n++) {
    char line[160];
    std::snprintf(line, sizeof(line),
                  "%s%d. Paragraph %d of a long answer, with enough words in it to wrap over two lines of the "
                  "reader's NotoSans 12.\n",
                  n % 4 == 3 ? "\n" : "", n + 1, n + 1);
    body += line;
  }
  body.resize(GEM_A_MAX - 120);
  body += " (cut short)\n\nWikipedia: Tomb Raider: Legacy of Atlantis; Tomb Raider: Catalyst";
  std::string q;
  while (q.size() < GEM_Q_CAP - 20) q += "and what about the one after that, and when is it due? ";
  const std::string text = page(q.c_str(), "a note in a box", body.c_str());
  const Shot first = draw(text, "Answered by Flash-Lite - new topic", "Flash-Lite", 0, "long_first");
  std::printf("[ai] the longest answer: %d screens\n", first.screens);
  EXPECT_GE(first.screens, 3);
  EXPECT_LE(first.screens, gd::PageLayout::MAX_SCREENS);
  EXPECT_FALSE(first.truncated);
  draw(text, "Answered by Flash-Lite - new topic", "Flash-Lite", first.screens - 1, "long_last");
}

TEST(AiPreview, HelpPage) {
  // AiPageActivity::forHelp's page with no key on the card (the device builds it from AiRun's key info).
  std::string body;
  const auto add = [&](const char* s) {
    body += s;
    body += '\n';
  };
  add(tr(STR_AI_KEY_LINE_NONE));
  char line[320];
  std::snprintf(line, sizeof(line), tr(STR_AI_MODEL_LINE), "Flash", GEM_DEFAULT_MODEL, "Flash-Lite", GEM_LITE_MODEL);
  add(line);
  add(tr(STR_AI_LOOKUP_ON));
  add(tr(STR_AI_RETRY_ON));
  add(tr(STR_AI_HELP_INTRO));
  const char* steps[] = {tr(STR_AI_HELP_1), tr(STR_AI_HELP_2), tr(STR_AI_HELP_3),
                         tr(STR_AI_HELP_4), tr(STR_AI_HELP_5), tr(STR_AI_HELP_6)};
  for (int i = 0; i < 6; i++) {
    std::snprintf(line, sizeof(line), "%d. %s", i + 1, steps[i]);
    add(line);
  }
  add(tr(STR_AI_HELP_MODELS));
  add(tr(STR_AI_HELP_WIFI));
  add(tr(STR_AI_HELP_PRIVACY));
  const std::string text = page(tr(STR_AI_HELP_TITLE), nullptr, body.c_str());
  const Shot first = draw(text, "", tr(STR_AI_KEY_HELP), 0, "help_1");
  std::printf("[ai] help: %d screens\n", first.screens);
  EXPECT_LE(first.screens, 3);
  EXPECT_FALSE(first.truncated);
  if (first.screens > 1) draw(text, "", tr(STR_AI_KEY_HELP), first.screens - 1, "help_last");
}
