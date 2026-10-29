#!/usr/bin/env python3
"""Make an Android 11 test boot.img: keep kernel + old ramdisk, but make /init the hvinit stub.
The old Android 9 init is kept as /init.old (unused, for reference).
Usage: python3 repack_a11.py boot.img hvinit boot_a11.img"""
import sys, gzip, struct, hashlib

def parse(b):
    assert b[:8] == b'ANDROID!', 'not an Android boot image'
    ks, ka, rs, ra, ss, sa, ta, ps, dts = struct.unpack('<9I', b[8:44])
    return ks, rs, ss, dts, ps

def pad(n, ps): return (n + ps - 1) // ps * ps

def read_cpio(d):
    i, ents = 0, []
    while True:
        assert d[i:i+6] == b'070701', 'ramdisk is not newc cpio'
        h = [int(d[i+6+8*k:i+14+8*k], 16) for k in range(13)]
        ino, mode, uid, gid, nl, mt, fs, dmaj, dmin, rmaj, rmin, nsz, chk = h
        name = d[i+110:i+110+nsz-1].decode()
        p = (i + 110 + nsz + 3) & ~3
        data = d[p:p+fs]
        i = (p + fs + 3) & ~3
        if name == 'TRAILER!!!': break
        ents.append(dict(mode=mode, uid=uid, gid=gid, nl=nl, mt=mt, rmaj=rmaj, rmin=rmin, name=name, data=data))
    return ents

def write_cpio(ents):
    out = bytearray(); ino = 1
    def one(e, ino):
        nb = e['name'].encode() + b'\0'
        hdr = '070701' + ''.join('%08X' % v for v in (ino, e['mode'], e['uid'], e['gid'], e.get('nl', 1), e['mt'], len(e['data']), 0, 0, e['rmaj'], e['rmin'], len(nb), 0))
        r = bytearray(hdr.encode() + nb)
        while len(r) % 4: r.append(0)
        r += e['data']
        while len(r) % 4: r.append(0)
        return r
    for e in ents:
        out += one(e, ino); ino += 1
    out += one(dict(mode=0, uid=0, gid=0, nl=1, mt=0, rmaj=0, rmin=0, name='TRAILER!!!', data=b''), 0)
    while len(out) % 512: out.append(0)
    return bytes(out)

def main(bootp, stubp, outp):
    b = open(bootp, 'rb').read()
    stub = open(stubp, 'rb').read()
    assert stub[:4] == b'\x7fELF', 'hvinit is not an ELF'
    ks, rs, ss, dts, ps = parse(b)
    koff = ps; roff = koff + pad(ks, ps); soff = roff + pad(rs, ps); doff = soff + pad(ss, ps)
    kernel, rd, second, dt = b[koff:koff+ks], b[roff:roff+rs], b[soff:soff+ss], b[doff:doff+dts]
    tail = b[doff + pad(dts, ps):]
    ents = read_cpio(gzip.decompress(rd))
    names = [e['name'] for e in ents]
    assert 'init' in names, 'no /init in ramdisk'
    for e in ents:
        if e['name'] == 'init': e['name'] = 'init.old'
    initent = dict(mode=0o100755, uid=0, gid=0, nl=1, mt=0, rmaj=0, rmin=0, name='init', data=stub)
    ents.insert(0, initent)
    # sbin/ueventd etc. are symlinks to ../init in the old ramdisk; keep them (harmless, Android 11 uses /system/bin)
    newrd = gzip.compress(write_cpio(ents), 9, mtime=0)
    hdr = bytearray(b[:ps])
    struct.pack_into('<I', hdr, 16, len(newrd))
    sha = hashlib.sha1()
    for part, sz in ((kernel, ks), (newrd, len(newrd)), (second, ss)):
        sha.update(part); sha.update(struct.pack('<I', sz))
    if dts or True:
        sha.update(dt); sha.update(struct.pack('<I', dts))
    hdr[576:608] = sha.digest().ljust(32, b'\0')
    out = bytes(hdr) + kernel + b'\0' * (pad(ks, ps) - ks) + newrd + b'\0' * (pad(len(newrd), ps) - len(newrd))
    out += second + b'\0' * (pad(ss, ps) - ss) + dt + b'\0' * (pad(dts, ps) - dts) + tail
    open(outp, 'wb').write(out)
    print('ramdisk %d -> %d bytes, /init=%d bytes (old init kept as /init.old), image %d bytes' % (rs, len(newrd), len(stub), len(out)))
    if tail: print('kept %d trailing bytes (e.g. SEANDROIDENFORCE)' % len(tail))

if __name__ == '__main__':
    if len(sys.argv) != 4: sys.exit(__doc__)
    main(*sys.argv[1:])
