#!/usr/bin/env python3
"""dsixl-scrape.py: fetches the art for the menu, from sources that need no account and no key:

  box art   DS: GameTDB (art.gametdb.com, by the game code in the ROM's header), else libretro-thumbnails
            GBA: libretro-thumbnails (github.com/libretro-thumbnails, by No-Intro file name)
  cart      DS: GameTDB's scans of the real cartridge, when it has one for the game
  title     both: the title screen and a screenshot (snap), from libretro-thumbnails' Named_Titles and Named_Snaps
            (the top screen's photo)
  logo      GBA: libretro-thumbnails' Named_Logos. (There is no free logo set for DS games: those get the title
            set as a logo by the menu, or put your own in media/nds/logo/, or scrape "marquee" art with
            EmulationStation's scraper: the menu reads <roms>/images/<name>-marquee.png too.)

It writes <media>/<nds|gba>/<box|cart|title|snap|logo>/<rom name without extension>.<png|jpg>, where the menu looks.
Runs on the handheld (ROCKNIX has python3) or on a computer with the SD card or the network share mounted.
Only the Python standard library.

    python3 dsixl-scrape.py                       # everything missing, folders from /storage/.config/dsixl/config.ini
    python3 dsixl-scrape.py --nds /path/nds --gba /path/gba --media /path/media
    python3 dsixl-scrape.py --force               # fetch again what's there
"""
import argparse, json, os, re, struct, sys, time, threading, urllib.request, urllib.parse
from concurrent.futures import ThreadPoolExecutor

LIBRETRO = {'nds': 'Nintendo_-_Nintendo_DS', 'gba': 'Nintendo_-_Game_Boy_Advance'}
EXTS = {'nds': ('.nds', '.dsi', '.ids', '.srl', '.zip', '.7z'), 'gba': ('.gba', '.agb', '.bin', '.mb', '.zip', '.7z')}
UA = 'dsixl-scrape/1.0 (personal DSi-style menu)'
REGION_PREF = ['USA', 'World', 'USA, Europe', 'Europe', 'Australia', 'Japan', 'Korea']
# a DS game code's last letter -> GameTDB's region folders, best first
TDB_REGION = {'E': ['US', 'EN'], 'P': ['EN', 'US'], 'J': ['JA'], 'K': ['KO'], 'D': ['DE', 'EN'], 'F': ['FR', 'EN'],
              'S': ['ES', 'EN'], 'I': ['IT', 'EN'], 'H': ['NL', 'EN'], 'U': ['AU', 'EN', 'US'], 'O': ['US', 'EN'],
              'T': ['US', 'EN'], 'V': ['EN', 'US'], 'X': ['EN', 'US'], 'Y': ['EN', 'US'], 'Z': ['EN', 'US']}
lock = threading.Lock()


def log(*a):
    with lock:
        print(*a, flush=True)


def get(url, tries=3):
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers={'User-Agent': UA})
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code in (404, 410):
                return None
            if e.code == 403 and 'api.github.com' in url:
                return None
        except Exception:
            pass
        time.sleep(1 + i * 2)
    return None


def is_image(b):
    return b and (b[:8] == b'\x89PNG\r\n\x1a\n' or b[:3] == b'\xff\xd8\xff')


def read_config():
    cfg = {'nds_roms': '/storage/roms/nds', 'gba_roms': '/storage/roms/gba', 'media': '/storage/roms/dsixl/media'}
    p = os.environ.get('DSIXL_HOME', '/storage/.config/dsixl') + '/config.ini'
    try:
        for line in open(p):
            if '=' in line and not line.strip().startswith('#'):
                k, v = line.split('=', 1)
                cfg[k.strip()] = v.strip()
    except OSError:
        pass
    return cfg


def norm(name):
    """a name for matching: no tags, no punctuation, no articles"""
    n = re.sub(r'\s*[\(\[].*?[\)\]]', '', name).lower()
    n = n.replace('&', 'and').replace(', the', '').replace('the ', '')
    return re.sub(r'[^a-z0-9]', '', n)


def libretro_safe(name):
    return re.sub(r'[&*/:`<>?\\|"]', '_', name)


def region_rank(name):
    tags = re.findall(r'\(([^)]*)\)', name)
    for i, r in enumerate(REGION_PREF):
        if any(t == r or t.startswith(r + ',') for t in tags):
            return i
    return len(REGION_PREF)


class Libretro:
    """the file list of a libretro-thumbnails repository (one request, kept for a week)"""

    def __init__(self, sysname, media):
        self.repo = LIBRETRO[sysname]
        self.folders = {}
        cache = os.path.join(media, '.libretro-%s.json' % sysname)
        tree = None
        if os.path.exists(cache) and time.time() - os.path.getmtime(cache) < 7 * 86400:
            try:
                tree = json.load(open(cache))
            except Exception:
                tree = None
        if tree is None:
            b = get('https://api.github.com/repos/libretro-thumbnails/%s/git/trees/master?recursive=1' % self.repo)
            if b:
                try:
                    tree = [t['path'] for t in json.loads(b).get('tree', []) if t.get('type') in ('blob', 'link', 'commit') or '.' in t['path']]
                    os.makedirs(media, exist_ok=True)
                    json.dump(tree, open(cache, 'w'))
                except Exception:
                    tree = None
        for p in tree or []:
            if '/' in p and p.endswith('.png'):
                folder, f = p.split('/', 1)
                self.folders.setdefault(folder, {})[f[:-4]] = True
        self.index = {}
        for folder, names in self.folders.items():
            idx = {}
            for n in names:
                idx.setdefault(norm(n), []).append(n)
            self.index[folder] = idx
        if not tree:
            log('  (libretro-thumbnails file list unavailable: trying exact names only)')

    def find(self, folder, base):
        names = self.folders.get(folder)
        safe = libretro_safe(base)
        if names is None:
            return [safe]                 # no list: just try the exact name
        if safe in names:
            return [safe]
        cands = self.index[folder].get(norm(base), [])
        return sorted(cands, key=lambda n: (region_rank(n), len(n)))[:2]

    def fetch(self, folder, name):
        url = 'https://raw.githubusercontent.com/libretro-thumbnails/%s/master/%s/%s.png' % (
            self.repo, urllib.parse.quote(folder), urllib.parse.quote(name))
        b = get(url)
        if b and not is_image(b) and len(b) < 600:
            # a git symlink: its text is the name of the file it points at
            target = b.decode('utf-8', 'replace').strip().split('/')[-1]
            url = 'https://raw.githubusercontent.com/libretro-thumbnails/%s/master/%s/%s' % (
                self.repo, urllib.parse.quote(folder), urllib.parse.quote(target))
            b = get(url)
        return b if is_image(b) else None


def nds_code(path):
    if not path.lower().endswith(('.nds', '.dsi', '.ids', '.srl')):
        return None
    try:
        with open(path, 'rb') as f:
            h = f.read(0x10)
        code = h[0x0c:0x10].decode('ascii')
        return code if re.fullmatch(r'[A-Z0-9]{4}', code) and code not in ('NTRJ', '####') else None
    except Exception:
        return None


def have(media, sysname, kind, base):
    for ext in ('.png', '.jpg'):
        if os.path.exists(os.path.join(media, sysname, kind, base + ext)):
            return True
    return False


def save(media, sysname, kind, base, data):
    d = os.path.join(media, sysname, kind)
    os.makedirs(d, exist_ok=True)
    ext = '.png' if data[:4] == b'\x89PNG' else '.jpg'
    tmp = os.path.join(d, base + ext + '.part')
    with open(tmp, 'wb') as f:
        f.write(data)
    os.replace(tmp, os.path.join(d, base + ext))


def scrape_game(sysname, path, media, lr, force, stats):
    base = os.path.splitext(os.path.basename(path))[0]
    got = []
    code = nds_code(path) if sysname == 'nds' else None
    regions = TDB_REGION.get(code[3], ['US', 'EN']) if code else []
    # the box (also the cartridge's label art, cropped)
    if force or not have(media, sysname, 'box', base):
        b = None
        for reg in regions:
            for kind, ext in (('coverM', '.jpg'), ('cover', '.jpg'), ('box', '.png')):
                b = get('https://art.gametdb.com/ds/%s/%s/%s%s' % (kind, reg, code, ext), tries=1)
                if is_image(b):
                    break
                b = None
            if b:
                break
        if not b and lr:
            for name in lr.find('Named_Boxarts', base):
                b = lr.fetch('Named_Boxarts', name)
                if b:
                    break
        if b:
            save(media, sysname, 'box', base, b); got.append('box')
    # a scan of the real cartridge, where GameTDB has one: the menu then shows it instead of a made-up label
    if code and (force or not have(media, sysname, 'cart', base)):
        for reg in regions:
            b = get('https://art.gametdb.com/ds/cart/%s/%s.png' % (reg, code), tries=1)
            if is_image(b):
                save(media, sysname, 'cart', base, b); got.append('cartridge scan')
                break
    # the top screen's photo: the title screen, and a screenshot (shown instead when the game has a logo to float
    # over it: the title screen would show the logo twice)
    for kind, folder, what in (('title', 'Named_Titles', 'title screen'), ('snap', 'Named_Snaps', 'screenshot')):
        if lr and (force or not have(media, sysname, kind, base)):
            for name in lr.find(folder, base):
                b = lr.fetch(folder, name)
                if b:
                    save(media, sysname, kind, base, b); got.append(what)
                    break
    # the logo
    if lr and (force or not have(media, sysname, 'logo', base)) and 'Named_Logos' in (lr.folders or {'Named_Logos': 1}):
        for name in lr.find('Named_Logos', base):
            b = lr.fetch('Named_Logos', name)
            if b:
                save(media, sysname, 'logo', base, b); got.append('logo')
                break
    with lock:
        stats['done'] += 1
        if got:
            stats['found'] += 1
    log('  [%d/%d] %s: %s' % (stats['done'], stats['total'], base, ', '.join(got) if got else 'nothing new'))


def main():
    cfg = read_config()
    ap = argparse.ArgumentParser(description='Fetches box art, labels and logos for the DSi Menu.')
    ap.add_argument('--nds', default=cfg.get('nds_roms'))
    ap.add_argument('--gba', default=cfg.get('gba_roms'))
    ap.add_argument('--media', default=cfg.get('media'))
    ap.add_argument('--only', choices=['nds', 'gba'])
    ap.add_argument('--force', action='store_true')
    ap.add_argument('--jobs', type=int, default=4)
    a = ap.parse_args()
    for sysname in ('nds', 'gba'):
        if a.only and a.only != sysname:
            continue
        d = getattr(a, sysname)
        if not d or not os.path.isdir(d):
            log('%s: no folder %s' % (sysname.upper(), d))
            continue
        roms = sorted(os.path.join(d, f) for f in os.listdir(d) if f.lower().endswith(EXTS[sysname]) and not f.startswith('.'))
        log('%s: %d games in %s' % (sysname.upper(), len(roms), d))
        if not roms:
            continue
        lr = Libretro(sysname, a.media)
        stats = {'done': 0, 'found': 0, 'total': len(roms)}
        with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as ex:
            for r in roms:
                ex.submit(scrape_game, sysname, r, a.media, lr, a.force, stats)
        log('%s: art found for %d of %d' % (sysname.upper(), stats['found'], len(roms)))
    log('Done. The menu shows the new art the next time it opens (or Options > Look for new games).')


if __name__ == '__main__':
    main()
