import sys
def parse(path):
    """Returns ({codepoint: (dwidth, bbx(w,h,xo,yo), [row ints])}, ascent, descent)."""
    g = {}; asc = desc = 0
    cur = None; inbm = False
    for line in open(path, encoding="utf-8", errors="replace"):
        p = line.split()
        if not p: continue
        if p[0] == "FONT_ASCENT": asc = int(p[1])
        elif p[0] == "FONT_DESCENT": desc = int(p[1])
        elif p[0] == "STARTCHAR": cur = {"rows": []}; inbm = False
        elif cur is not None:
            if p[0] == "ENCODING": cur["cp"] = int(p[1])
            elif p[0] == "DWIDTH": cur["dw"] = int(p[1])
            elif p[0] == "BBX": cur["bbx"] = tuple(int(v) for v in p[1:5])
            elif p[0] == "BITMAP": inbm = True
            elif p[0] == "ENDCHAR":
                g[cur["cp"]] = (cur.get("dw", 8), cur["bbx"], cur["rows"]); cur = None
            elif inbm: cur["rows"].append((int(p[0], 16), len(p[0]) * 4))
    return g, asc, desc
