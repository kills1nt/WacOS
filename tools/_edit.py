root = 'C:/Users/beakk/Projects/WacOS/'
def edit(rel, reps):
    p = root + rel
    raw = open(p, 'rb').read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    s = (raw[3:] if bom else raw).decode('utf-8')
    nl = '\r\n' if '\r\n' in s else '\n'
    for old, new, cnt in reps:
        o = old.replace('\n', nl); n = new.replace('\n', nl)
        c = s.count(o)
        if c != cnt: raise Exception(f'{rel}: expected {cnt} got {c}: {old[:80]}')
        s = s.replace(o, n)
    open(p, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + s.encode('utf-8'))
    print('ok', rel)
