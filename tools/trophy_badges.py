#!/usr/bin/env python3
"""The trophies' pictures (runtime/trophies.c): each trophy's badge from its
RetroAchievements achievement (game 9093), fetched once into <out>/<key>.png on
the player's own computer by BUILD.bat. They are the RetroAchievements
authors' pictures: never kept in the project, never shared, only fetched for
the player. Without them (no Internet, the site down) the game shows its cups.

    python tools/trophy_badges.py game/trophies

Fetches only the missing ones, a few seconds at most once they are all there.
The achievement pages read are remembered in <out>/ra.txt (id, badge, title)."""
import os, re, sys, time, html, urllib.request

SITE = "https://retroachievements.org/achievement/%d"
BADGE = "https://media.retroachievements.org/Badge/%s.png"
UA = "DQIX-Enhanced trophy pictures (+https://github.com/romusan495/Dragon-Quest-IX-Modern)"

# the trophies whose achievement is known (from the set's guide on GitHub,
# RetroAchievements/guides: Dragon Quest IX)
FIXED = {
    "story_hexagoon": 146732, "story_morag": 146733, "story_contagion": 146734, "story_nuun": 146735,
    "story_lleviathan": 146736, "story_garth_goyle": 146737, "story_tyrantula": 146738,
    "story_grand_lizzier": 146740, "story_larstastnaras": 146741, "story_dreadmaster": 146742,
    "story_gadrongo": 146744, "story_greygnarl": 146745, "story_goreham_hogg": 146746,
    "story_hootingham_gore": 146747, "story_goresby_purrvis": 146748, "story_godwyn": 146749,
    "story_corvus": 146750,
    "voc_change": 146763, "voc_armamentalist": 146764, "voc_gladiator": 146765, "voc_ranger": 146766,
    "voc_paladin": 146767, "voc_sage": 146768, "voc_luminary": 146769,
    "battles_1000": 146770, "metal_slime": 146771, "metal_medley": 146772, "metal_liquid": 146773,
    "metal_king": 146774, "metal_platinum": 146775,
    "items_56": 146776, "items_112": 146777, "items_168": 146778,
    "monsters_76": 146780, "monsters_153": 146781, "monsters_230": 146782,
    "wardrobe_236": 146784, "wardrobe_472": 146785, "wardrobe_708": 146786,
    "alchemy_113": 146788, "alchemy_226": 146789, "alchemy_339": 146790,
    "quest_004": 146792, "quest_015": 146793, "quest_039": 146794,
    "quests_20": 146819, "quests_50": 146820, "quests_100": 146821, "quests_184": 146822,
}
for i, q in enumerate(range(91, 103)): FIXED["quest_%03d" % q] = 146795 + i
for i, q in enumerate((104, 105, 107, 108, 110, 111, 113, 114, 116, 117, 119, 120)): FIXED["quest_%03d" % q] = 146807 + i
for line in range(1, 27): FIXED["skill_%02d" % line] = 146822 + line          # in the game's order of the skills
# badges seen on the game's page: the achievement's number + 16419 (the first 20)
KNOWN_BADGE = {a: a + 16419 for a in range(146731, 146751)}

# the others: found by their words among a range of achievements
VOC = ["Warrior", "Priest", "Mage", "Martial Artist", "Thief", "Minstrel", "Gladiator",
       "Armamentalist", "Paladin", "Sage", "Luminary", "Ranger"]
SEARCH = [("monsters_307", list(range(146779, 146792, 4)), ["307"])]
SEARCH += [("level50_%02d" % (v + 1), list(range(146849, 146861)), ["50", VOC[v]]) for v in range(12)]
BOSSES = {"qboss_yore": "Yore", "qboss_rover": "Rover", "qboss_godfrey": "Godfrey", "qboss_wrecks": "Tyrannosaurus Wrecks",
          "qboss_wrecks2": "Tyrannosaura Wrecks", "qboss_nodoph": "Nodoph", "qboss_sanguinus": "Sanguinus", "qboss_elusid": "Elusid",
          "qboss_hammibal": "Hammibal", "qboss_fowleye": "Fowleye", "qboss_excalipurr": "Excalipurr", "qboss_nokturnus": "Nokturnus",
          "grotto_equinox": "Equinox", "grotto_nemean": "Nemean", "grotto_shogum": "Shogum", "grotto_trauminator": "Trauminator",
          "grotto_atlas": "Atlas", "grotto_greygnarl": "Greygnarl"}
SEARCH += [(k, list(range(146861, 146878)), [n]) for k, n in BOSSES.items()]
LEGACY = {"dragonlord": "Dragonlord", "malroth": "Malroth", "baramos": "Baramos", "zoma": "Zoma", "psaro": "Psaro",
          "estark": "Estark", "nimzo": "Nimzo", "murdaw": "Murdaw", "mortamor": "Mortamor", "orgodemir": "Orgodemir",
          "dhoulmagus": "Dhoulmagus", "rhapthorne": "Rhapthorne"}
SEARCH += [("legacy_" + k, list(range(369287, 369300)), [n]) for k, n in LEGACY.items()]


def get(url, tries=2):
    for t in range(tries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=20) as r:
                return r.read()
        except Exception as e:
            err = e
            time.sleep(1 + t)
    raise err


def page(aid, cache):
    """(badge, words) of an achievement, its page read once."""
    if aid in cache: return cache[aid]
    text = get(SITE % aid).decode("utf-8", "replace")
    m = re.search(r"/Badge/(\d+)(?:_lock)?\.png", text)
    t = re.search(r'<meta[^>]+property="og:title"[^>]+content="([^"]*)"', text) or re.search(r"<title>([^<]*)</title>", text)
    d = re.search(r'<meta[^>]+(?:property="og:description"|name="description")[^>]+content="([^"]*)"', text)
    words = html.unescape(((t.group(1) if t else "") + " | " + (d.group(1) if d else "")).replace("\t", " ").replace("\n", " "))
    cache[aid] = (m.group(1) if m else "", words)
    time.sleep(0.3)                                             # gently
    return cache[aid]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "game/trophies"
    os.makedirs(out, exist_ok=True)
    keys = list(FIXED) + [k for k, _, _ in SEARCH]
    missing = [k for k in keys if not os.path.exists(os.path.join(out, k + ".png"))]
    if not missing:
        print("   trophy pictures: all there"); return 0
    cache_file = os.path.join(out, "ra.txt"); cache = {}
    if os.path.exists(cache_file):
        for line in open(cache_file, encoding="utf-8"):
            p = line.rstrip("\n").split("\t")
            if len(p) == 3 and p[0].isdigit(): cache[int(p[0])] = (p[1], p[2])
    got = failed = in_row = 0; start = time.time()

    def fetch(key, badge):
        nonlocal got
        data = get(BADGE % badge)
        if not data.startswith(b"\x89PNG"): raise ValueError("not a PNG")
        with open(os.path.join(out, key + ".png.new"), "wb") as f: f.write(data)
        os.replace(os.path.join(out, key + ".png.new"), os.path.join(out, key + ".png"))
        got += 1

    search = {k: (ids, w) for k, ids, w in SEARCH}
    used = set(FIXED.values())
    try:
        for key in missing:
            if time.time() - start > 240 or in_row >= 5: break     # never hold the build long (the site out of reach)
            try:
                if key in FIXED:
                    aid = FIXED[key]
                    badge = str(KNOWN_BADGE[aid]) if aid in KNOWN_BADGE else page(aid, cache)[0]
                else:
                    ids, words = search[key]; badge = ""
                    for aid in ids:
                        if aid in used: continue
                        b, text = page(aid, cache)
                        if all(re.search(r"\b" + re.escape(w) + r"\b", text, re.I) for w in words):
                            # the longest name wins (Tyrannosaurus / Tyrannosaura Wrecks)
                            if key.startswith("qboss_wrecks") and ("Tyrannosaura" in text) != (key == "qboss_wrecks2"): continue
                            badge = b; used.add(aid); break
                if not badge: failed += 1; continue
                fetch(key, badge); in_row = 0
            except Exception:
                failed += 1; in_row += 1
    finally:
        with open(cache_file, "w", encoding="utf-8") as f:
            for aid in sorted(cache): f.write("%d\t%s\t%s\n" % (aid, cache[aid][0], cache[aid][1]))
    print("   trophy pictures: %d fetched, %d not (the cups instead; tried again next build), %d already there"
          % (got, len(missing) - got, len(keys) - len(missing)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
