import ast, hashlib, os, re, sys

out = sys.argv[1]
seen = set()

def save(name, data):
    h = hashlib.sha1(data).hexdigest()[:12]
    if h in seen or len(data) < 8 or data[0] & 0xfc == 0:
        return
    seen.add(h)
    open(os.path.join(out, "%s-%s.bin" % (name, h)), "wb").write(data)

for path in sys.argv[2:]:
    name = os.path.basename(path)[:-3]
    tree = ast.parse(open(path).read())
    for node in ast.walk(tree):
        if isinstance(node, ast.List) and len(node.elts) >= 8 and all(isinstance(e, ast.Constant) and isinstance(e.value, int) and 0 <= e.value <= 255 for e in node.elts):
            save(name, bytes(e.value for e in node.elts))
        elif isinstance(node, ast.Constant) and isinstance(node.value, str) and re.fullmatch(r"\s*([0-9a-fA-F]{2}\s+){8,}[0-9a-fA-F]{2}\s*", node.value):
            save(name, bytes(int(x, 16) for x in node.value.split()))
