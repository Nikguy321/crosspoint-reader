#pragma once

class GfxRenderer;

namespace sleepcards {

// The X4 Pro logo screen (boot, the Dark/Light sleep screens, and every card's fallback): the
// X4 Pro mark (src/images/X4ProLogo.h, drawn by scripts/gen_x4pro_logo.py) centred, "X4 Pro"
// under it and `caption` (tr(STR_SLEEPING), tr(STR_BOOTING)) under that. Clears the frame
// first; no refresh, no inversion - the caller decides both.
void drawX4ProLogoScreen(GfxRenderer& renderer, const char* caption);

}  // namespace sleepcards
