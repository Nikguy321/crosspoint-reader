# Survival Guide - open questions for a human expert

The survival pack (`packs/guide/survival/`) was written from current public guidance and reviewed page by page **by AI reviewers, not by a medical professional or a field expert**. Each of the twelve category reviewers left the questions below for a person who knows the subject: a wilderness-medicine clinician or WFR for FIRST AID and the medical lines elsewhere, a SAR or fieldcraft expert for the rest. Until someone answers them, the pack keeps `status=reviewed-by-ai` and the app says so.

Every reviewer question is listed, grouped by category in the guide's order and numbered as the reviewer numbered it. A **Status** line says what the content pass of 2026-10-04 already did about it; a question without one is still fully open. Page and figure names refer to `topics/<category>/<NN>-<topic>.md` and `figures/figures.tsv`.

85 questions in 12 categories.

## EMERGENCY (7)

1. Tourniquet placement: two federal sources differ. ready.gov says "as high as possible"; DHS Stop the Bleed (2018) says 2 to 3 in above the wound and not over a joint. The page says "high on the limb, above the wound, not on a joint". A WFA/WFR or clinician should confirm this. They should also say whether the wilderness case, with evacuation taking many hours, needs a line about tourniquet conversion by trained providers. The page currently says only "Do not take it off", which matches ready.gov and AHA 2024.
2. The FCC pages (Wireless 911; Text-to-911) still return 403 to direct fetches. Their content was confirmed only through fcc.gov search excerpts, so someone should open them in a browser before public release. The tip 'location first, no abbreviations' is not confirmed by any excerpt I could retrieve.
3. PLB antenna handling: no federal page gives a generic rule, so the page now defers to the device label. A SAR expert could confirm the generic advice: open sky, antenna up, not lying on your body.
4. Rule-of-threes water line: "Drink before you are thirsty" follows CDC heat guidance and ATP (thirst drops in the cold). WMS hyponatremia guidance prefers 'drink to thirst' during long exertion. Over-drinking is unlikely when water is scarce, but an expert should confirm the wording.
5. Signal-fire WARNING: it follows NPS (no fire at high fire risk or in wildfire season). A human should decide how the FIRE and SHELTER sections handle a warming fire when hypothermia threatens during a burn ban, so the guide does not contradict itself.
6. CPR on the quick card is hands-only for adults. Child, drowning and hypothermia CPR, and CPR far from help, are left to the FIRST AID section; that section's reviewer must cover them.
7. Hug-a-Tree wording ("Never hide from searchers") is standard program advice, but the only federal source is the NPS PDF's mention of Hug-a-Tree. The fuller program text is on mra.org, which is not a federal site.

## PRIORITIES & KIT (6)

1. Three titles across the whole pack contain a colon: 'Clothing: your first shelter', 'If you are lost: STOP' and 'Wild plants: do not guess'. The builder must split `title:` on the first ': ' only, or the values need quoting. This is a builder decision for the whole pack, so I left the titles as they are.
   - **Status:** For the builder: the validator splits front-matter lines on the first ": " only, and the device-pack builder must do the same. The titles stay as they are.
2. Hydration conflict for a human expert. NPS 10 Essentials, CDC and NIOSH say to drink before you feel thirsty. The Wilderness Medical Society's low-sodium guidance says to drink to thirst. The page now follows the federal advice with the NIOSH cap of 1.5 quarts (1.4 L) an hour, and the low-sodium detail is left to FIRST AID. An expert should decide whether 'before you feel thirsty' should apply to heat only (as now) or be removed.
3. Trip plan in the vehicle: NPS (leave a copy for rangers) and USFS Region 5 (never on the windshield, thieves) disagree. The 'inside, out of sight' compromise is my own reconciliation; neither agency says it.
4. Pocket kit 'water treatment tablets': the usual chlorine doses do not reliably kill Cryptosporidium. The WATER category should carry that caveat, and someone should confirm it does.
   - **Status:** Checked: WATER > Making water safe says on its Bleach page and its Tablets page that chlorine does not work well on Cryptosporidium. Still worth an expert read.
5. The 1.5 quarts (1.4 L) an hour cap comes from NIOSH guidance for workers in heat; check that it is fine to apply it to hikers.
6. The USFS backcountry-safety page cited is the Pacific Southwest Region's page. It is a national agency's general advice, not a personal place, but a reviewer may prefer a national USFS page if one exists.

## FIRE (7)

1. Fit on screen: several text-only pages sit at or near 170 words and carry one or two boxed WARNING/NOTE lines: 01 p2 (170), 02 p1 (160, two WARNINGs), 03 p2 (161), 04 p2 (170). Boxes take more height than plain text. Please check these on the real X4 screen once the builder exists.
2. Escaped-fire advice: 'fire runs fastest uphill and downwind; do not flee that way; head for bare ground, rock or water'. This rests on the NPS fire-behavior facts plus ready.gov's 'call 911 with your location'. No federal page I fetched gives step-by-step escape directions for someone on foot, so a wildland-fire professional should confirm the wording.
3. The fire-ban emergency exception ('In an emergency a fire can save your life. Even then: ...') implies a life-threat fire during a ban is acceptable. Legally, restrictions rarely carve out such an exception; it is a necessity defence at best. The maintainer or a land-agency contact may want firmer wording.
4. Uniformed-soldier figures (bow drill atp-p131-1, hand drill atp-p132-1) are kept, matching the other categories. A neutral redraw is still owed. The parts diagram on the left of atp-p131-1 could be cropped into its own master as a cheap interim fix.
5. The Dakota fire hole page is now text-only. It needs a neutral redraw: open ground, no tree, no concealment label, and inch-first labels.
6. Steel-wool-and-9-volt fire starting is kept (it is in ATP and AFH) with the short-circuit and vehicle-battery WARNINGs. A reviewer may prefer to drop it from a family guide because of the fire risk from loose 9-volt batteries.
7. Hand drill wood 'yucca or elderberry' is from ATP 5-54. Elderberry stalks are pithy and their identification overlaps with toxic plants. Fire use is fine, but a botanist may prefer a more generic 'dry, straight, pithy stalks'.

## WATER (9)

1. The two drinking rules disagree. The CDC Yellow Book and WMS say to drink to thirst and not to force fluids; CDC outdoor-worker pages say not to wait until thirsty. The page compromises: small amounts often, not 'very thirsty', and no more than about 1.5 quarts (1.4 L) an hour. A clinician should confirm this wording for a non-worker survival audience.
2. Is 'Do not push plain water on a confused person' right for field first aid? It reflects the hyponatremia guidance to restrict fluid, but it could delay fluids for someone whose confusion comes from heat stroke and dehydration. A WFR or clinician should check it against the FIRST AID category's heat-stroke page.
3. Should the CDC Yellow Book's fuel-saving simmer-and-cover method (heat until it first simmers, then keep covered 30 minutes) appear beside CDC's standard 1-minute rolling boil? It is CDC-sourced but less conservative, and the guide's rule says to prefer the more conservative option where sources differ.
4. Color-safe and scented bleach: the warning is chemically sound (color-safe bleach is usually not chlorine), but no current CDC page says it. An EPA page reportedly does, but EPA is not on the allowed source list. Should EPA be added as an allowed federal source so the line can return?
   - **Status:** Decided: EPA (.gov) is an allowed source. The Bleach page now says "Never use scented or color-safe bleach, or bleach with added cleaners", citing EPA Emergency Disinfection of Drinking Water.
5. Bleach loses strength with age, so bleach kept in a kit may be weak. No allowed source was found to cite, so it is not on the page. Should an expert add a line about it?
   - **Status:** Added from the same EPA page: "Bleach weakens with age: use bleach less than a year old, kept at room temperature." An expert may still check the wording.
6. Bag stills: the page now says 'Treat it if you can'. ATP says plant water is usually safe to drink. An expert should decide whether transpiration-bag water really needs treating.
7. The yields for the ground solar still (1 pint to 1 quart a day, three stills per person) come from the 1992 and 2018 Army manuals. Field tests are widely reported to give much less. A fieldcraft expert should decide whether to add a stronger 'do not rely on it' line.
8. Figure atp-p63-1 (vegetation bag still) is still marked 'keep' but loses the bag outline at 1-bit. The page text stands alone, but a redraw or gray version is still pending (writer's note).
   - **Status:** Since this review the figure is marked redraw in figures.tsv and the Vegetation bag still page is text-only.
9. 02-finding-water p1 still says 'Doves and pigeons fly low and straight toward water in the early morning and evening' (FM 21-76 lore). It is harmless, but a naturalist should confirm it is reliable enough to keep.

## SHELTER (8)

1. Fire distance: ATP 6-40 puts the fire about 3 ft (1 m) from the shelter opening, and both the lean-to page and bedding page 2 use that. The FIRE section clears a 10 ft (3 m) circle to bare soil. Next to a bough or debris shelter those two cannot both be true. A fieldcraft expert should choose one rule and make both sections match.
2. Reflector lean-to: the page says never to leave the fire burning while you sleep unless someone stays awake, following USFS 'never unattended'. That defeats much of the point of a reflector shelter for someone alone. It is conservative and coherent, but an expert should confirm we want that rule for true survival use.
3. No-flame-inside rule for snow shelters: ATP and FM 21-76 allow candles and cooking inside a vented snow cave. CDC says never use fuel-burning devices in a tent or enclosed shelter. The pages follow CDC and ban candles too. That is conservative and departs from the manuals, so it needs a human decision.
4. Tree-pit page: 'Dig in from the side' is my own mitigation, not a federal instruction. The federal sources only name the hazard: people fall in head first, get trapped and suffocate. A snow-safety expert should confirm the wording, and also whether to add 'don't work alone' (cut for space).
5. The car note (run the engine about 10 minutes an hour) is current CDC guidance, but it sits under a WARNING that bans engines in enclosed spaces. Someone should confirm readers will not see these as conflicting on the device.
6. Legal NOTE: it covers federal land only, citing 36 CFR 261.6 and 2.1. State and private land rules vary, and the spec bars state-agency text. Should a generic 'other public land has its own rules' line be added?
7. Uniformed figures atp-p50-1 (heat loss) and atp-p150-1 (snow cave) are kept for now. A neutral redraw is owed later, as in the other categories.
8. The writer chose cautious figures where the manuals differ: debris hut cover at least 3 ft (1 m), ridgeline 'waist to chest high'. I kept them. An expert may want them to match one manual exactly.

## FIRST AID (9)

1. Homemade rehydration recipe (6 level tsp sugar + 1/2 tsp salt in 1 L): only WHO publishes it. No current CDC page does; a 2003 CDC MMWR warns that home mixing errors happen. Keep it with 'measure carefully', or drop it and keep packets only? Expert call.
   - **Status:** Decided: no current CDC page gives a home recipe (the CDC ORS poster, the CDC cholera treatment page and the CDC cholera community-health-worker manual all use packets), so the recipe and the WHO source were dropped. The page now says: "No packet? Keep sipping treated water or other clean drinks. Carry packets in your first-aid kit." An expert may still want a recipe back with a federal source.
2. Angulated fractures: the page says 'splint it as you find it; do not straighten it'. AHA/Red Cross 2024 allows straightening when it is needed for safe transport, and wilderness courses teach gentle traction-in-line when circulation beyond the break is lost on a long evacuation. Should a wilderness exception be added?
3. CPR far from help: there is no guidance on when to stop CPR in remote settings (WMS suggests about 30 minutes without signs of life, except for hypothermia, lightning and drowning). The page says keep going until help takes over or you are exhausted. Leave it, or add a remote-setting note?
4. Shock: the page says 'nothing to eat or drink'. AHA 2024 encourages fluids for exertional dehydration without shock, and evacuations can take days. Should an alert person get sips on a long evacuation? Leg raise is still left out (evidence mixed).
5. The epinephrine second-dose timing (5 to 10 minutes) and all the AHA/Red Cross 2024 points were checked through search summaries only; ahajournals.org and cpr.heart.org returned 403. A human should confirm them against the full guideline.
6. Snakebite limb position: CDC/NIOSH says 'a neutral position of comfort' and WMS says heart level; the page combines them. Confirm.
7. Stop the Bleed packing is cited from the American College of Surgeons program site as checked only (stopthebleed.org, a non-federal source). The DHS Stop the Bleed PDF covers tourniquets only, and no federal page I found covers packing. Acceptable?
   - **Status:** Decided: kept as a check-only source (the validator allow-lists stopthebleed.org as check-only; the line says "checked only, not copied"). An expert should still confirm the packing step.
8. atp-p47-1 needs a neutral redraw (a person in plain clothes wearing the three slings) before the Slings page gets a figure again.
9. This is still AI review only. The spec's status=reviewed-by-ai stands, and a licensed wilderness-medicine reviewer should sign off on all 31 pages before the public release build.

## NAVIGATION (7)

1. Figure fit: fm92-p292-1 (shadow-tip) is about 560 px tall at 440 px wide. Even with 36 words, the page may not fit the 480x800 screen with its header and footer. Check it on the device, or have the builder cap figure height.
2. atp-p177-1 (paper-strip distance) is only about 157 px tall at 440 px wide. Its baked-in labels ('1520 YARDS', 'PENCIL TICK MARKS') are unreadable at that size. The hands-and-strip drawing still gets the idea across. Keep it, crop it to the right-hand panel, or redraw it?
3. atp-p165-1 and atp-p166-1 (northern and southern sky) are white on large black areas, and their 352 px masters are narrower than 440 px. Should they be inverted for e-ink? That is the builder's or a human's call.
4. fm92-p292-1 shows figures in military field uniform. Like the other categories, it is kept as a candidate for a neutral redraw. Should a human decide it must be redrawn before the public release build?
5. Group crossings with children: no federal source I found covers carrying or helping children across. Should the page add 'Do not carry a child across fast water', and should a fieldcraft expert confirm it?
6. Needle compass: I removed the silk method on physics grounds. A human may want to confirm that the 'small speaker' suggestion for a magnet is acceptable wording.
7. 01-stay-or-move says 'call or text 911'. Text-to-911 is not available everywhere. This should match whatever the EMERGENCY category finally says.

## SIGNALING (5)

1. Signal fire in a fire ban: the page and the EMERGENCY topic now both say no fire in a fire ban or in dry or windy weather. A human expert or SAR lead should decide whether a life-threatening emergency justifies a small, attended signal fire despite a ban. Legal exposure varies by jurisdiction and was not verified.
2. Ground-sign size disagrees across the pack. EMERGENCY 03-signal-now.md says '3 ft (1 m) wide and 20 ft (6 m) long', SIGNALING says '3 feet (1 m) wide and 18 feet (6 m) long' (AFH), and the FAA AIM says symbols at least 10 feet high. I did not edit the EMERGENCY file, which is outside this category. Pick one figure: 18 ft (AFH) vs 20 ft (6 m = 19.7 ft).
3. Helicopter hoist: standard rescue advice (e.g. Coast Guard) is never to tie a lowered cable or basket to anything. No allowed source in hand states it, so I did not add it. 'Do exactly what the crew tells you' and 'let it touch the ground first' are on the page. A human expert could confirm and add the line.
4. Satellite messaging page (07 p2): the specific tips (who/what/where content, tracking, keeping the device on) still rest only on NPS pages and general practice. The FCC pages returned 403 to the writer. Worth an expert read.
5. Beacon antenna 'keep it pointing up': AFH says to keep the antenna at a right angle to the rescue aircraft's path and never grounded. Pointing it up for satellites is standard PLB practice, but no federal page I fetched states it outright.

## WEATHER (8)

1. Sunscreen reapply time: FDA says at least every 2 hours and CDC Yellow Book says every 2 to 4 hours. The page uses 2 hours. Does a human expert agree?
2. Lightning crouch: the page follows NWS/NOAA (no crouch, and notes that some guides still teach it), while NPS Yosemite and the 2014 WMS guideline still describe a last-resort lightning position. An expert should confirm the call. Group spacing of 50 ft (15 m) comes from NPS Yosemite; WMS 2014 says more than 20 ft. Is 50 ft the right number to print?
3. Lightning page: should it add a reverse-triage line for several victims (treat first anyone who is not breathing)? I left it out because I did not confirm it on a federal page and the page is at 165 of 170 words.
4. Wildfire: the writer removed 'do not flee up a narrow canyon or chute' (chimney effect). The NPS fire-behavior page I fetched confirms fast uphill spread but not chimneys specifically, and NWCG blocks fetching. A fire expert should decide whether to add 'avoid narrow uphill gullies and canyons' (it is standard NWCG Watch Out training) and whether a pond or stream can be listed as a refuge.
5. Wildfire 'If trapped': the USDA guide's 'breathe through a cloth' is kept, but CDC/NIOSH says a cloth does not filter smoke particles. The page now puts the N95 first. An expert should confirm the cloth line is still worth keeping for heat and embers.
6. Winter storms: the new CO WARNING bans any flame inside tents and snow shelters (CDC MMWR), which is stricter than ATP 3-50.21 6-39 (heat sources allowed with vent holes open). This matches the Snow shelters page. Confirm that is the intended stance.
7. Heat card: 'Drink often, before you feel thirsty' follows NPS and CDC, but WMS heat guidance leans toward drinking to thirst because of hyponatremia risk. The card keeps the salty-snack and 'water without food can be dangerous' caveat. An expert should confirm the balance.
8. Avalanche danger appears only in the navigation and shelter topics, not in WEATHER. Should Winter storms carry a one-line pointer to the avalanche forecast? I did not add one (no budget issue; the scope question is the editor's call).

## KNOTS & CORDAGE (5)

1. Prusik loop knot: the page ties the loop with a double sheet bend so it only uses knots this guide teaches. The standard is the double fisherman's knot, and ATP finishes its Prusik with a bowline. A double sheet bend in equal-size cord is fine for camp tarp tensioners, but a rigging expert should confirm, or we should add a double fisherman's page.
2. Prusik cord size: 'about half to two-thirds as thick' as the line is reasonable for camp guylines. Climbing practice usually says 60-80% of the rope diameter. Worth an expert glance if the page is ever widened beyond camp rigging.
3. Square lashing finish: the page finishes on the crosspiece, matching the figure's baked-in caption. ATP A-28's text says to finish on the same pole you started on. Both are taught. A human should pick one so the guide does not contradict its own figure.
4. Diagonal lashing start: ATP's text and the page start with a clove hitch around both poles. Panel 1 of figure atp-p212-1 looks more like a timber hitch, which is the common scouting method. An expert could decide whether to say 'timber hitch or clove hitch'.
5. atp-p213-1 (shear lashing) is still faint at its 205 threshold, and atp-p210-1 (Prusik) is too small. The writer already marked both as redraw candidates and I agree.

## FOOD & HAZARDS (7)

1. Two cross-check sources are on nih.gov (PubMed 30597474, shellfish allergy and insects; PubMed 14595067, no home-induced vomiting). That is a federal site but not one of the named sites. Should we keep them as check sources, or find a CDC or FDA page that says the same?
   - **Status:** Decided: both PubMed records are check-only sources, labelled "checked only, not copied" (the validator now requires that label on PubMed links). A CDC or FDA page saying the same would still be better.
2. Snares and set lines: the pages say these are 'usually illegal' or 'banned in many places' outside a true emergency. Whether a survival emergency is a legal defense varies by state and is UNVERIFIED. Does a human want stronger wording, or to drop snare construction from the public build?
3. atp-p91-1 is tall: 440x590 at device width, which exceeds the roughly 440x360 figure area. Should it be cropped to the drag-noose panel (and the channel shown in text only), or scaled down by the builder? This needs a device check.
4. Insects page: is it acceptable to list earthworms as a 'good choice' at all, given parasite risk? Cooking is required on the page. A wilderness-medicine reviewer may prefer to drop earthworms.
5. Game cooking: the page uses 160 F (71 C) for all wild game, from CDC's ground-meat figure and trichinellosis guidance. CDC's trichinellosis page gives no number, and foodsafety.gov/USDA FSIS were not reachable, so no current federal page explicitly gives 160 F for whole cuts of wild game. Should a human confirm against USDA FSIS?
6. Food storage numbers (100 ft from sleeping areas; hang 12 ft up and 5 ft out) come from one park's rules. Other agencies use different figures, such as 200 ft or 10 ft out. The page says to follow local rules; does a human want a range instead?
7. The topic title 'Wild plants: do not guess' contains a colon inside the 'title:' value. Confirm the pack builder splits on the first ': ' only.
   - **Status:** For the builder: same as PRIORITIES & KIT question 1.

## PACIFIC NORTHWEST (7)

1. The ice self-rescue steps come from an NWS Boston page that credits a state emergency agency. I rewrote them in plain words and cite the NWS page. A human should decide whether that meets the 'no state-agency text' rule, or point to a different federal source.
2. Death cap: no federal page I could fetch confirms it in Washington or Oregon; the sources only say it is expanding on the West Coast. If a regional expert can confirm it is in the Northwest, the card could say so.
3. Shellfish: ATP para 4-169 ('take only shellfish that stay covered by water at high tide') is an old general survival rule, not protection against PSP or domoic acid. I kept it next to the closure-check rule. A shellfish-safety reviewer should decide whether it gives false comfort and should go. Domoic acid in crab guts is also not covered, because the NOAA page does not mention crabs.
4. Deadly plants: no seizure first aid, and no 'do not make them vomit' line. HRSA's first-steps page was blocked (403) and could not be checked; a medical reviewer could add one short line. Giant hogweed and wild parsnip skin burns, both Northwest hazards, are also not covered.
5. Foxglove flower colors (purple, pink or white) and 'poisons the heart' are general knowledge. The NPS source only says the flowers are 'variously colored' and that all parts are toxic.
6. Two lines rest on general knowledge rather than a fetched source: 'Eat and drink often. Food is fuel for body heat' (cold water) and 'Keep children and pets away from wild mushrooms' (death cap). Both are low risk, but an editor should sign off.
7. Word-count convention: I counted heading words toward each page's budget (the writer apparently did too). Ticks page 1 is exactly 170. Confirm the device's line wrapping actually fits that on one screen.

## Also open from the pack assembly

- **Figures owed a neutral redraw** (marked redraw, not shown; their pages are text-only): fm92-p99-1 Dakota fire hole, atp-p63-1 vegetation bag still, atp-p47-1 slings, atp-p176-2 graphic scale, atp-p171-1 floating needle compass, atp-p156-1 and atp-p157-1 stream crossing, atp-p210-1 Prusik.
- **Uniformed figures still shown** (marked keep, a neutral redraw is owed later): atp-p131-1, atp-p132-1, atp-p50-1, atp-p150-1, fm92-p292-1, fm92-p304-1. Should any of them be redrawn before a public release?
- **Screen fit:** the reference-only NOTE was added to six non-medical pages that mention hypothermia, frostbite, CPR or heat stroke. Two now sit at the word limit with boxed lines: WEATHER > Heat and sun (a quick card, 169 words, a WARNING and a NOTE) and WEATHER > Lightning page 2 (170 words, two NOTEs). Check them on the device; a quick card must fit one screen.
- **Edits after review (please check):** FIRST AID > Wounds and burns: the WARNING now covers any burn ("Do NOT put butter, oil, egg, creams, lotions or home remedies on any burn"), matching Ready.gov; petroleum jelly or aloe stays allowed on a small, unblistered burn. FIRST AID > Sprains and broken bones, splint page: "Check that fingers or toes stay warm and pink; if not, loosen the ties." Two quick cards were trimmed to fit one screen: Head and spine injuries (shorter intro; steps 5 and 6 merged; danger signs reworded as "worsening headache, or repeated vomiting" and "confusion, slurred speech, odd behavior") and Death cap ("both planted and native" dropped; steps 2 and 3 merged as "Keep leftovers to show"; the children-and-pets line moved into the WARNING). Quick cards are set in a smaller type than other pages so each fits one screen; whether that type is large enough to read in a hurry is a call for the maintainer.
- **For the builder:** the `<!-- myth-ok -->` marker ends three WARNING lines (eat snow, drink seawater, drink urine) and must be stripped; the figures.tsv note field starts with keep, redraw or drop, then free text.
