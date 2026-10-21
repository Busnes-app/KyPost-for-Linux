"""Writes build-shots/compare.html: Android | Linux before | Linux after, per screen.

Usage: screenshots-compare.py <build-shots dir> [android reference dir]
"""
import html
import pathlib
import shutil
import sys

# Linux screen name -> Android store shot (phone / 7-inch landscape).
ANDROID = {
    "inbox-all": 1,
    "inbox-primary": 2,
    "email-detail": 3,
    "compose": 4,
    "contact-detail": 5,
    "contacts-list": 5,
    "settings-security": 6,
}
LAYOUT_PREFIX = {"mobile": "phoneScreenshots", "desktop": "sevenInchScreenshots"}


def main():
    shots = pathlib.Path(sys.argv[1])
    if len(sys.argv) > 2 and pathlib.Path(sys.argv[2]).is_dir():
        ref = shots / "android-ref"
        ref.mkdir(parents=True, exist_ok=True)
        for png in pathlib.Path(sys.argv[2]).glob("*.png"):
            shutil.copy2(png, ref / png.name)

    labels = [d.name for d in sorted(shots.iterdir()) if d.is_dir() and d.name != "android-ref"]
    # before first, after last, anything else between
    labels.sort(key=lambda n: (n != "before", n == "after", n))
    variants = sorted({v.name for label in labels for v in (shots / label).iterdir() if v.is_dir()})

    out = ["<!doctype html><meta charset=utf-8><title>KyPost Linux vs Android</title>",
           "<style>body{font:14px sans-serif;margin:16px;background:#eee}"
           "section{margin:0 0 32px}h2{margin:24px 0 8px}h3{margin:0 0 4px;font-size:13px}"
           ".row{display:flex;gap:12px;align-items:flex-start}figure{margin:0}"
           "img{border:1px solid #aaa;background:#fff;max-height:640px;max-width:100%}"
           "figcaption{font-size:12px;color:#444}</style>",
           "<h1>KyPost Linux vs Android</h1>"]
    for variant in variants:
        layout = variant.split("-")[0]
        out.append(f"<h2>{html.escape(variant)}</h2>")
        names = sorted({p.stem for label in labels for p in (shots / label / variant).glob("*.png")})
        for name in names:
            out.append(f"<section><h3>{html.escape(name)}</h3><div class=row>")
            shot = ANDROID.get(name)
            if shot and layout in LAYOUT_PREFIX:
                src = f"android-ref/{LAYOUT_PREFIX[layout]}-{shot}.png"
                if (shots / src).exists():
                    out.append(f"<figure><img src='{src}'><figcaption>Android</figcaption></figure>")
            for label in labels:
                src = f"{label}/{variant}/{name}.png"
                if (shots / src).exists():
                    out.append(f"<figure><img src='{html.escape(src)}'>"
                               f"<figcaption>Linux {html.escape(label)}</figcaption></figure>")
            out.append("</div></section>")
    (shots / "compare.html").write_text("\n".join(out), encoding="utf-8")


if __name__ == "__main__":
    main()
