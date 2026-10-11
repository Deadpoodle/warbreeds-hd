"""Build art/menus/: one folder per 4:3 menu screen with its capture, a 4x
upscale, outpainting canvases (centre/left x 16:9/21:9) and a Gemini prompt.
Captures come from build/screens (autopilot mode 4; the main menu's bare
background from mode 3).
Usage: menuart.py"""
import os
import shutil

from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, "..")
shots = os.path.join(root, "build", "screens")
out = os.path.join(root, "art", "menus")

# folder, scene class (= Menus\<Scene>.png), capture, what it is, scene description
SCREENS = [
    ("01-main-menu", "SHeadquarters", "main-menu-bare.png",
     "The main menu. The same background is also used by Campaign, Load Game, the mission briefing and "
     "dialogs over the main menu, so this one image covers all of them.",
     "an alien, organic, biomechanical cave with ribbed purple-blue rock walls, glowing molten orange-red "
     "light through the cave mouth, and a glowing orb hovering above a rippling, glowing pool"),
    ("02-title-screen", "SWarBreedsScreen", "auto-screen-SWarBreedsScreen.bmp",
     "The title screen shown at start-up.",
     "a golden alien face with glowing yellow eyes filling the sky above the WARBREEDS logo, with rays of "
     "light streaming over a desert of dunes and distant moons, in warm gold and amber tones"),
    ("03-skirmish-setup", "SMultiSetup", "auto-screen-SMultiSetup.bmp",
     "The skirmish setup screen (clan, map and option choices).",
     "a dark violet and blue biomechanical scene of giant alien bones, ribs and organic skeletal structures"),
    ("04-gene-lab", "SGeneLabScene", "auto-screen-SGeneLabScene.bmp",
     "The Gene Lab. The creature in the middle changes as you browse, but the scenery around it stays the "
     "same, so one image works for every creature.",
     "a vivid alien jungle of twisted red and orange organic trees and vines, a glowing blue crystal, "
     "glowing water and a creature on display"),
    ("05-device-lab", "SDeviceLab", "auto-screen-SDeviceLab.bmp",
     "The Device Lab. The device in the middle changes as you browse, but the scenery around it stays the "
     "same, so one image works for every device.",
     "a fiery red-orange alien landscape with dark twisted organic trees and vines, a glowing sky and a "
     "biomechanical device standing on a platform"),
]

STYLE = ("Match the original painting style, colour palette, texture, grain and lighting direction of this "
         "1998 pre-rendered game art. No text, lettering, logos, buttons, panels, user interface, people or "
         "new creatures in the new areas. Don't continue the game's menu panels, buttons or text into the new "
         "areas; extend only the scenery behind them.")


def prompt(desc, ratio, side):
    if side == "centre":
        where = ("Fill the two flat grey areas on the left and right by extending the scene outwards "
                 f"(outpainting) to a {ratio} image. Keep the existing central picture exactly as it is")
    else:
        where = ("Fill the flat grey area on the right by extending the scene to the right (outpainting) to "
                 f"a {ratio} image. Keep the existing picture on the left exactly as it is")
    return (f"This is a painted background from a 1998 sci-fi strategy game: {desc}. {where}. Don't "
            "change, move, crop, recolour, sharpen or re-render anything in it, and keep it at exactly the same "
            "size and position. Continue its scenery naturally across the edge with no visible seam, and keep "
            "the new areas a little darker and less detailed than the original so the eye stays on it. "
            + STYLE)


def main():
    os.makedirs(out, exist_ok=True)
    index = ["# Widescreen menu backgrounds", "",
             "One folder per menu screen. Each has the original screen, a 4x upscale, canvases with grey areas "
             "for Gemini to fill (16:9 and 21:9, with the original centred or at the left), and prompts.", "",
             "| Folder | Screen | Save the result as |", "|---|---|---|"]
    for folder, scene, capture, what, desc in SCREENS:
        src_path = os.path.join(shots, capture)
        if not os.path.exists(src_path):
            print("missing", capture)
            continue
        d = os.path.join(out, folder)
        os.makedirs(d, exist_ok=True)
        src = Image.open(src_path).convert("RGB")
        src.save(os.path.join(d, "original-640x480.png"))
        src.resize((2560, 1920), Image.LANCZOS).save(os.path.join(d, "original-2560x1920.png"))
        big = src.resize((1920, 1440), Image.LANCZOS)
        for ratio, w in (("16x9", 2560), ("21x9", 3440)):
            for side in ("centre", "left"):
                c = Image.new("RGB", (w, 1440), (128, 128, 128))
                c.paste(big, ((w - 1920) // 2 if side == "centre" else 0, 0))
                c.save(os.path.join(d, f"canvas-{side}-{ratio}-{w}x1440.png"))
        lines = [f"# {folder[3:].replace('-', ' ').title()} (`{scene}`)", "", what, "",
                 f"**Save the finished image as `Menus\\{scene}.png`** in the game folder.", "",
                 "The game draws its own 640x480 screen, with all its buttons and clickable areas, on top of "
                 "your image, so nothing can move or break. Only the new strip beside it is seen.", ""]
        for side, label in (("centre", "Centred (`MenuAlign=centre`, default)"),
                            ("left", "Left-aligned (`MenuAlign=left`)")):
            lines += [f"## {label}", ""]
            for ratio, w, nice in (("16x9", 2560, "16:9"), ("21x9", 3440, "21:9 ultrawide")):
                lines += [f"**{nice}**: give Gemini `canvas-{side}-{ratio}-{w}x1440.png` with:", "",
                          "> " + prompt(desc, nice.split()[0], side), ""]
        lines += ["## Tips", "",
                  "- Ask for the output at the canvas's exact size (for example 2560x1440) if Gemini resizes it.",
                  "- Gemini may alter the original part slightly. That's fine, because the game covers it. Check "
                  "the seam where old and new art meet.",
                  "- Several short attempts usually beat one long prompt. Keep the best seam.",
                  "- If the result doesn't line up, WarBreeds HD's maintainer can realign it to the canvas."]
        open(os.path.join(d, "README.md"), "w", encoding="utf-8").write("\n".join(lines) + "\n")
        index.append(f"| [{folder}]({folder}/README.md) | {what.split('.')[0]} | `Menus\\{scene}.png` |")
    index += ["", "**Not included:**",
              "- The **credits** cycle through different concept-art slides, so a single background can't "
              "match them. They keep black bars.",
              "- Screens not reachable without multiplayer (TCP/IP and join screens) can be added later.", "",
              "**Alignment:** choose `MenuAlign=centre` (default) or `left` under `[Display]` in "
              "`WarBreedsHD.ini`. Use the matching canvases for every screen."]
    open(os.path.join(out, "README.md"), "w", encoding="utf-8").write("\n".join(index) + "\n")
    print("wrote", out)


if __name__ == "__main__":
    main()
