#!/usr/bin/env python3
"""dsixl-scrape.py: fetches the art for the menu, from sources that need no account and no key.

First the LaunchBox Games Database (gamesdb.launchbox-app.com): its public Metadata.zip (updated daily, downloaded
once and kept for a month in <media>/.launchbox/) lists every game's images, which are on images.launchbox-app.com:
  Cart - Front              -> cart   (the whole face of the cartridge)
  Box - Front               -> box    (cropped by the menu to a label when there is no cartridge scan)
  Clear Logo                -> logo   (the top screen)
  Screenshot - Gameplay     -> snap   (the photo view, under a logo)
  Screenshot - Game Title   -> title  (the photo view, without one)
Games are matched by name (the ROM's No-Intro name against LaunchBox's names and alternate names), the region of the
ROM preferred. Whatever LaunchBox doesn't have comes from:

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
import argparse, json, os, re, struct, sys, time, threading, urllib.request, urllib.parse, zipfile
import xml.etree.ElementTree as ET
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


PROGRESS = False                            # --progress: lines for the menu's progress bar ("@ ...")


def log(*a):
    with lock:
        print(*a, flush=True)


def prog(*fields):
    """a line the menu reads: @ <what> <fields...> (tab separated)"""
    if PROGRESS:
        with lock:
            print('@\t' + '\t'.join(str(f) for f in fields), flush=True)


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


LB_ZIP = os.environ.get('DSIXL_LB_ZIP_URL', 'https://gamesdb.launchbox-app.com/Metadata.zip')
LB_IMG = os.environ.get('DSIXL_LB_IMG_URL', 'https://images.launchbox-app.com/')
LB_PLATFORM = {'nds': 'Nintendo DS', 'gba': 'Nintendo Game Boy Advance'}
LB_KINDS = {'Cart - Front': 'cart', 'Box - Front': 'box', 'Clear Logo': 'logo',
            'Screenshot - Gameplay': 'snap', 'Screenshot - Game Title': 'title'}
# a ROM's region tag -> LaunchBox's region names, best first ('' is LaunchBox's "no region")
LB_REGION = {'USA': ['North America', 'United States', 'World', ''], 'Europe': ['Europe', 'United Kingdom', 'World', ''],
             'Japan': ['Japan', 'World', ''], 'World': ['World', '', 'North America', 'Europe'],
             'Australia': ['Australia', 'Europe', 'World', ''], 'Korea': ['Korea', 'World', '']}


def download_to(url, path):
    """a big file, in pieces, with a progress line; resumed into a .part"""
    tmp = path + '.part'
    try:
        req = urllib.request.Request(url, headers={'User-Agent': UA})
        with urllib.request.urlopen(req, timeout=60) as r, open(tmp, 'wb') as f:
            total = int(r.headers.get('Content-Length') or 0); got = 0; last = 0
            while True:
                b = r.read(1 << 20)
                if not b:
                    break
                f.write(b); got += len(b)
                if time.time() - last > 2:
                    last = time.time()
                    log('  LaunchBox database: %d MB%s' % (got >> 20, ' of %d MB' % (total >> 20) if total else ''))
                    prog('download', got >> 20, total >> 20)
        os.replace(tmp, path)
        return True
    except Exception as e:
        log('  LaunchBox database: download failed (%s)' % e)
        try:
            os.remove(tmp)
        except OSError:
            pass
        return False


class LaunchBox:
    """The games of our two systems and their images, from LaunchBox's Metadata.xml. The XML is several hundred MB:
    it is read as a stream straight out of the zip (twice: the games first, then their images), never whole, so this
    runs in the handheld's 1 GB. The result is kept as a small JSON index beside the zip."""

    def __init__(self, media, systems, refresh=False):
        self.games = {}                     # sysname -> {norm name: [entry]}; entry: {'name', 'images': {kind: [(region, file)]}}
        d = os.path.join(media, '.launchbox'); os.makedirs(d, exist_ok=True)
        zp, ip = os.path.join(d, 'Metadata.zip'), os.path.join(d, 'index.json')
        fresh = os.path.exists(zp) and time.time() - os.path.getmtime(zp) < 30 * 86400 and not refresh
        if fresh and os.path.exists(ip) and os.path.getmtime(ip) >= os.path.getmtime(zp):
            try:
                self.games = json.load(open(ip))
                if all(s in self.games for s in systems):
                    return
            except Exception:
                pass
        if not fresh:
            log('  LaunchBox database: downloading (once a month)')
            if not download_to(LB_ZIP, zp) and not os.path.exists(zp):
                return
        log('  LaunchBox database: reading (a minute or two)')
        prog('reading')
        try:
            self.games = self._read(zp)
            json.dump(self.games, open(ip, 'w'))
        except Exception as e:
            log('  LaunchBox database: unreadable (%s)' % e)
            self.games = {}

    @staticmethod
    def _stream(zp):
        z = zipfile.ZipFile(zp)
        name = next((n for n in z.namelist() if n.lower().endswith('metadata.xml')), None)
        if not name:
            raise ValueError('no Metadata.xml in the zip')
        f = z.open(name)
        root, depth = None, 0
        for ev, el in ET.iterparse(f, events=('start', 'end')):
            if ev == 'start':
                if root is None:
                    root = el
                depth += 1
                continue
            depth -= 1
            if depth == 1:                  # a record (<Game>, <GameImage>...) is complete
                yield el
                el.clear()
                try:
                    root.remove(el)         # done with it: memory stays flat
                except ValueError:
                    pass

    def _read(self, zp):
        want = {v: k for k, v in LB_PLATFORM.items()}
        ids = {}                            # DatabaseID -> (sysname, entry)
        out = {s: {} for s in LB_PLATFORM}
        for el in self._stream(zp):         # pass 1: the games, and their other names
            if el.tag == 'Game':
                sysname = want.get((el.findtext('Platform') or '').strip())
                if sysname:
                    gid, name = (el.findtext('DatabaseID') or '').strip(), (el.findtext('Name') or '').strip()
                    if gid and name:
                        e = {'name': name, 'images': {}}
                        ids[gid] = (sysname, e)
                        out[sysname].setdefault(norm(name), []).append(e)
            elif el.tag == 'GameAlternateName':
                gid = (el.findtext('DatabaseID') or '').strip()
                if gid in ids:
                    sysname, e = ids[gid]
                    alt = (el.findtext('AlternateName') or '').strip()
                    if alt and e not in out[sysname].setdefault(norm(alt), []):
                        out[sysname][norm(alt)].append(e)
        n = 0
        for el in self._stream(zp):         # pass 2: their images
            if el.tag == 'GameImage':
                gid = (el.findtext('DatabaseID') or '').strip()
                kind = LB_KINDS.get((el.findtext('Type') or '').strip())
                if kind and gid in ids:
                    fn = (el.findtext('FileName') or '').strip()
                    if fn:
                        ids[gid][1]['images'].setdefault(kind, []).append([(el.findtext('Region') or '').strip(), fn])
                        n += 1
        log('  LaunchBox database: %d DS and %d GBA games, %d images of the kinds wanted' %
            (len({id(e) for v in out['nds'].values() for e in v}), len({id(e) for v in out['gba'].values() for e in v}), n))
        return out

    def find(self, sysname, base):
        """the game for a ROM name, and the regions to prefer"""
        cands = self.games.get(sysname, {}).get(norm(base), [])
        if not cands:
            # "Title - Subtitle" vs "Title: Subtitle" and the like differ only in punctuation, which norm() drops;
            # also try without a trailing subtitle
            t = re.sub(r'\s*[\(\[].*$', '', base)
            if ' - ' in t:
                cands = self.games.get(sysname, {}).get(norm(t.split(' - ')[0]), [])
        if not cands:
            return None, []
        tags = re.findall(r'\(([^)]*)\)', base)
        regions = []
        for t in tags:
            for part in t.split(','):
                regions += LB_REGION.get(part.strip(), [])
        regions += ['North America', 'World', '', 'Europe', 'Japan']
        # several games of the same name (rare): the one with the most images
        e = max(cands, key=lambda c: sum(len(v) for v in c['images'].values()))
        return e, regions

    def fetch(self, entry, regions, kind):
        imgs = entry['images'].get(kind) or []
        order = sorted(imgs, key=lambda r: regions.index(r[0]) if r[0] in regions else len(regions))
        for region, fn in order[:3]:
            b = get(LB_IMG + urllib.parse.quote(fn), tries=2)
            if is_image(b):
                return b
        return None


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


def scrape_game(sysname, path, media, lr, force, stats, lb=None):
    base = os.path.splitext(os.path.basename(path))[0]
    got = []
    # LaunchBox first: everything it has for the game
    if lb and lb.games:
        entry, regions = lb.find(sysname, base)
        if entry:
            for kind, what in (('cart', 'cartridge scan'), ('box', 'box'), ('logo', 'logo'), ('snap', 'screenshot'), ('title', 'title screen')):
                if force or not have(media, sysname, kind, base):
                    b = lb.fetch(entry, regions, kind)
                    if b:
                        save(media, sysname, kind, base, b); got.append(what + ' (LaunchBox)')
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
    prog('game', sysname, stats['done'], stats['total'], base, len(got))


def main():
    cfg = read_config()
    ap = argparse.ArgumentParser(description='Fetches box art, labels and logos for the DSi Menu.')
    ap.add_argument('--nds', default=cfg.get('nds_roms'))
    ap.add_argument('--gba', default=cfg.get('gba_roms'))
    ap.add_argument('--media', default=cfg.get('media'))
    ap.add_argument('--only', choices=['nds', 'gba'])
    ap.add_argument('--force', action='store_true')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--no-launchbox', action='store_true', help='skip the LaunchBox Games Database')
    ap.add_argument('--refresh-launchbox', action='store_true', help='download its database again now')
    ap.add_argument('--progress', action='store_true', help='machine-readable progress lines (the menu)')
    a = ap.parse_args()
    global PROGRESS
    PROGRESS = a.progress
    prog('start')
    lb = None
    if not a.no_launchbox:
        systems = [s for s in ('nds', 'gba') if not a.only or a.only == s]
        lb = LaunchBox(a.media, systems, a.refresh_launchbox)
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
                ex.submit(scrape_game, sysname, r, a.media, lr, a.force, stats, lb)
        log('%s: art found for %d of %d' % (sysname.upper(), stats['found'], len(roms)))
        prog('system', sysname, stats['found'], len(roms))
    log('Done. The menu shows the new art the next time it opens (or Options > Look for new games).')
    prog('done')


if __name__ == '__main__':
    main()
