#!/usr/bin/env python3
# ==============================================================================
# TAD book compiler: scripts/tool_book2tad.py
# Python 3.6+ stdlib-only. One HTML profile: the article/section book used by
# doc/cores (B-Book), doc/media (B-Media), and doc/specs (BTRON3 3.20 spec).
# Preserves C99 code layout, ASCII diagrams, API cards, callouts, figures.
# Output stays under tad_bin/b-book, tad_bin/b-media, and tad_bin/b-spec so
# the cabinet and the TAD browser keep their existing paths.
# ==============================================================================
from __future__ import annotations

import glob
import os
import re
import shutil
import struct
import sys
from datetime import date

# ── BTRON3 SPEC 3.20 TAD Segment Tag Identifiers ────────────────────────────
TS_TPAGE  = 0xFFA0
TS_TRULER = 0xFFA1
TS_TFONT  = 0xFFA2
TS_TCHAR  = 0xFFA3
TS_VOBJ   = 0xFFA8
TS_FPRIM  = 0xFFB0
RECORD_TYPE_MAIN = 1


# ── Erlang phash2 replica ─────────────────────────────────────────────────────
def erlang_phash2(term_str: str, range_: int) -> int:
    h = 0
    for b in term_str.encode('utf-8'):
        h = ((h * 31) + b) & 0xFFFFFFFF
    return h % range_

def link_id(href: str) -> int:
    return erlang_phash2(href, 100_000) + 1000


# ── Noise Filter for B-Book ───────────────────────────────────────────────────
def filter_book_noise(html: str) -> str:
    html = re.sub(r'<!DOCTYPE[^>]*>', '', html, flags=re.I)
    html = re.sub(r'<head\b[^>]*>.*?</head>', '', html, flags=re.I | re.S)
    html = re.sub(r'<script\b[^>]*>.*?</script>', '', html, flags=re.I | re.S)
    html = re.sub(r'<style\b[^>]*>.*?</style>', '', html, flags=re.I | re.S)
    html = re.sub(r'<!--.*?-->', '', html, flags=re.S)
    html = re.sub(r'<nav\b[^>]*>.*?</nav>', '', html, flags=re.I | re.S)
    html = re.sub(r"<div class=['\"]breadcrumb['\"]>.*?</div>", '', html, flags=re.I | re.S)
    return html.strip()


# ── Entity Decoder ────────────────────────────────────────────────────────────
_ENTITIES = {
    '&nbsp;':' ','&lt;':'<','&gt;':'>','&amp;':'&','&quot;':'"','&apos;':"'",
    '&times;':'×','&mdash;':'—','&ndash;':'–','&bull;':'•',
    '&mu;':'μ','&larr;':'←','&rarr;':'→',
}
def decode_entities(text: str) -> str:
    for ent, repl in _ENTITIES.items():
        text = text.replace(ent, repl)
    text = re.sub(r'&#(\d+);', lambda m: chr(int(m.group(1))), text)
    text = re.sub(r'&#x([0-9a-fA-F]+);', lambda m: chr(int(m.group(1),16)), text)
    return text

def strip_tags(html: str) -> str:
    text = re.sub(r'<[^>]+>', '', html)
    text = re.sub(r'[ \t]+', ' ', text)
    return text.strip()

def clean_code(html: str) -> str:
    html = re.sub(r'^<code\b[^>]*>', '', html, flags=re.I)
    html = re.sub(r'</code>$', '', html, flags=re.I)
    return decode_entities(html).strip('\n\r')


# ── List / Table Parsers ──────────────────────────────────────────────────────
def parse_list(html: str) -> list:
    items = re.findall(r'<li\b[^>]*>(.*?)</li>', html, flags=re.I | re.S)
    return [decode_entities(strip_tags(i)) for i in items if strip_tags(i)]

def parse_list_elements(html: str, kind: str) -> list:
    """A list whose items are only links becomes virtual-body links.

    Mixed lists keep their bullets and also emit a link for each anchor.
    """
    lis = re.findall(r'<li\b[^>]*>(.*?)</li>', html, flags=re.I | re.S)
    texts = []
    links = []
    pure = True
    for li in lis:
        text = decode_entities(strip_tags(li))
        if not text:
            continue
        am = re.search(r"""<a\s+href=['"]([^'"]+)['"][^>]*>(.*?)</a>""", li, re.I | re.S)
        label = decode_entities(strip_tags(am.group(2))) if am else ''
        if am and label and label == text:
            links.append(('link', am.group(1), label))
        else:
            pure = False
            texts.append(text)
            if am and label:
                links.append(('link', am.group(1), label))
    if pure and links:
        return links
    out = []
    if texts:
        out.append((kind, texts))
    out.extend(links)
    return out

def parse_image_elements(html: str) -> list:
    sm = re.search(r"""src=['"]([^'"]+)['"]""", html, re.I)
    if not sm:
        return []
    am = re.search(r"""alt=['"]([^'"]*)['"]""", html, re.I)
    cm = re.search(r'<figcaption\b[^>]*>(.*?)</figcaption>', html, re.I | re.S)
    alt = decode_entities(strip_tags(cm.group(1))) if cm else ''
    if not alt and am:
        alt = decode_entities(am.group(1))
    return [('image', sm.group(1), alt)]

def parse_table(html: str) -> list:
    rows = re.findall(r'<tr\b[^>]*>(.*?)</tr>', html, flags=re.I | re.S)
    result = []
    for row_html in rows:
        cells = re.findall(r'<t[hd]\b[^>]*>(.*?)</t[hd]>', row_html, flags=re.I | re.S)
        result.append([decode_entities(strip_tags(c)) for c in cells])
    return result


# ── API Card Parser ───────────────────────────────────────────────────────────
def parse_api_card(card_html: str) -> tuple:
    nm = re.search(r"<span class=['\"]api-name['\"]>(.*?)</span>", card_html, re.I|re.S)
    rt = re.search(r"<span class=['\"]ret-badge['\"]>(.*?)</span>", card_html, re.I|re.S)
    ds = re.search(r'<p>(.*?)</p>', card_html, re.I|re.S)
    sg = re.search(r"<div class=['\"]api-signature['\"]>(.*?)</div>", card_html, re.I|re.S)
    tb = re.search(r'<table\b[^>]*>(.*?)</table>', card_html, re.I|re.S)
    mt = re.search(r"<div class=['\"]api-meta['\"]>(.*?)</div>", card_html, re.I|re.S)

    name     = decode_entities(strip_tags(nm.group(1))) if nm else 'UnknownAPI'
    ret_type = decode_entities(strip_tags(rt.group(1))) if rt else 'void'
    desc     = decode_entities(strip_tags(ds.group(1))) if ds else ''
    sig      = decode_entities(clean_code(strip_tags(sg.group(1)))) if sg else ''
    table    = parse_table(tb.group(0)) if tb else None

    meta = []
    if mt:
        pairs = re.findall(
            r"<span class=['\"]meta-label['\"]>(.*?)</span>\s*<span>(.*?)</span>",
            mt.group(1), re.I|re.S)
        meta = [(decode_entities(strip_tags(l)), decode_entities(strip_tags(v)))
                for l, v in pairs]

    return ('api_card', name, ret_type, desc, sig, table, meta)


# ── Callout Parser ────────────────────────────────────────────────────────────
def parse_callout(html: str) -> tuple:
    tm = re.search(r"<div class=['\"]callout-title['\"]>(.*?)</div>", html, re.I|re.S)
    title = decode_entities(strip_tags(tm.group(1))) if tm else 'Note'
    body_html = re.sub(r"<div class=['\"]callout-title['\"]>.*?</div>", '', html, flags=re.I|re.S)
    body = decode_entities(strip_tags(body_html))
    return ('callout', title, body)


# ── Balanced DIV extractor ────────────────────────────────────────────────────
def extract_balanced_div(html: str):
    """Return (full_block, remaining) or None if not found at offset 0."""
    if not html.lower().startswith('<div'):
        return None
    depth = 0
    pos = 0
    for m in re.finditer(r'</?div\b[^>]*>', html, flags=re.I):
        if m.start() == 0 and depth == 0:
            depth = 1
            pos = m.end()
            continue
        if m.group(0).startswith('</') or m.group(0).lower().startswith('</'):
            depth -= 1
        else:
            depth += 1
        if depth == 0:
            end = m.end()
            return html[:end], html[end:]
    return None


# ── Cell wrapper + ASCII table (same as html2tad.py) ─────────────────────────
def wrap_cell_text(text: str, max_len: int) -> list:
    if not text or max_len <= 0:
        return ['']
    words = [w for w in re.split(r'\s+', text) if w]
    if not words:
        return ['']
    split_words = []
    for w in words:
        if len(w) <= max_len:
            split_words.append(w)
        else:
            for i in range(0, len(w), max_len):
                split_words.append(w[i:i+max_len])
    lines, cur = [], ''
    for word in split_words:
        if not cur:
            cur = word
        elif len(cur)+1+len(word) <= max_len:
            cur += ' '+word
        else:
            lines.append(cur); cur = word
    if cur: lines.append(cur)
    return lines if lines else ['']

def format_table_ascii(rows: list, max_table_width: int = 76) -> str:
    if not rows: return ''
    col_count = max(len(r) for r in rows)
    if not col_count: return ''
    padded = [r+['']*(col_count-len(r)) for r in rows]
    nat, mwl = [], []
    for c in range(col_count):
        vals = [row[c] for row in padded]
        nat.append(max(max((len(v) for v in vals),default=0),4))
        words = [w for v in vals for w in re.split(r'\s+',v) if w]
        mwl.append(max((len(w) for w in words),default=4))
    overhead = 3*col_count+1
    avail = max(max_table_width-overhead, col_count*6)
    if sum(nat) <= avail:
        cw = nat
    elif col_count > 1:
        first = [min(max(nat[i],mwl[i]),20) for i in range(col_count-1)]
        cw = first+[max(avail-sum(first),mwl[-1])]
    else:
        cw = [avail]
    def brd(l,m,r,f='─'):
        return l+f+(f+m+f).join(f*w for w in cw)+f+r
    top,mid,bot = brd('┌','┬','┐'),brd('├','┼','┤'),brd('└','┴','┘')
    def fmt(row):
        wrapped=[wrap_cell_text(v,w) for v,w in zip(row,cw)]
        rh=max(len(w) for w in wrapped)
        norm=[w+['']*(rh-len(w)) for w in wrapped]
        return ['│ '+' │ '.join(norm[c][li].ljust(cw[c]) for c in range(col_count))+' │'
                for li in range(rh)]
    hl=fmt(padded[0]); bl=[]
    for i,dr in enumerate(padded[1:]):
        if i: bl.append(mid)
        bl.extend(fmt(dr))
    return '\n'.join([top]+hl+[mid]+bl+[bot])

def parse_markdown_table(text: str):
    lines=[l.strip() for l in text.split('\n') if l.strip()]
    if len(lines)<2: return None
    hdr,sep=lines[0],lines[1]
    if not(hdr.startswith('|') and sep.startswith('|') and '-' in sep): return None
    def pr(line): return [c.strip() for c in line.strip('|').split('|')]
    return [pr(hdr)]+[pr(l) for l in lines[2:]]


# ── Robust Semantic Parser for B-Book ────────────────────────────────────────
def parse_book_html(html: str) -> list:
    clean = filter_book_noise(html)
    elements = []
    remaining = clean
    tag_re = re.compile(
        r'<(?:div\s+class=[\'"](?:api-card|callout\b)'
        r'|figure|img|h[1-6]|pre|p|table|ul|ol|hr|a\s+href)\b[^>]*>',
        re.I)

    while remaining:
        m = tag_re.search(remaining)
        if m is None:
            md = parse_markdown_table(remaining)
            if md:
                elements.append(('table', md))
            break

        lead = remaining[:m.start()]
        if lead:
            md = parse_markdown_table(lead)
            if md:
                elements.append(('table', md))

        token = m.group(0)
        low = token.lower()
        sub = remaining[m.start():]

        if 'api-card' in low:
            result = extract_balanced_div(sub)
            if result:
                block, remaining = result
                elements.append(parse_api_card(block))
            else:
                remaining = sub[m.end()-m.start():]
        elif 'callout' in low:
            result = extract_balanced_div(sub)
            if result:
                block, remaining = result
                elements.append(parse_callout(block))
            else:
                remaining = sub[len(token):]
        elif low.startswith('<pre'):
            pm = re.match(r'<pre\b[^>]*>(.*?)</pre>', sub, re.I|re.S)
            if pm:
                code = clean_code(pm.group(1))
                elements.append(('code_block', code))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif low.startswith('<table'):
            pm = re.match(r'<table\b[^>]*>.*?</table>', sub, re.I|re.S)
            if pm:
                elements.append(('table', parse_table(pm.group(0))))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif re.match(r'<h[1-6]', low):
            n = low[2]
            pm = re.match(fr'<h{n}\b[^>]*>(.*?)</h{n}>', sub, re.I|re.S)
            if pm:
                title = decode_entities(strip_tags(pm.group(1)))
                htype = {'1':'h1','2':'h2','3':'h3'}.get(n,'h4')
                elements.append((htype, title))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif low.startswith('<ul') or low.startswith('<ol'):
            kind = 'ol' if low.startswith('<ol') else 'ul'
            pm = re.match(rf'<{kind}\b[^>]*>.*?</{kind}>', sub, re.I|re.S)
            if pm:
                elements.extend(parse_list_elements(pm.group(0), kind))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif low.startswith('<figure') or low.startswith('<img'):
            if low.startswith('<figure'):
                pm = re.match(r'<figure\b[^>]*>.*?</figure>', sub, re.I|re.S)
            else:
                pm = re.match(r'<img\b[^>]*>', sub, re.I|re.S)
            if pm:
                elements.extend(parse_image_elements(pm.group(0)))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif low.startswith('<hr'):
            pm = re.match(r'<hr\s*/?>', sub, re.I)
            if pm:
                elements.append(('hr',))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif low.startswith('<a'):
            pm = re.match(r"<a\s+href=['\"]([^'\"]+)['\"][^>]*>(.*?)</a>", sub, re.I|re.S)
            if pm:
                href  = pm.group(1)
                label = decode_entities(strip_tags(pm.group(2)))
                elements.append(('link', href, label))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif low.startswith('<p'):
            pm = re.match(r'<p\b[^>]*>(.*?)</p>', sub, re.I|re.S)
            if pm:
                text = decode_entities(strip_tags(pm.group(1)))
                if text:
                    etype = 'page_lead' if 'page-lead' in low else 'p'
                    elements.append((etype, text))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        else:
            remaining = sub[len(token):]

    return elements


# ── Binary Segment Builders ───────────────────────────────────────────────────
def make_segment(tag: int, payload: bytes) -> bytes:
    return struct.pack('>HI', tag, len(payload)) + payload

def seg_page(w=800,h=1200,ml=40,mt=40) -> bytes:
    return make_segment(TS_TPAGE, struct.pack('>BBHHHHHH',0,0,h,w,mt,mt,ml,ml))
def seg_font(fid,plane=1) -> bytes:
    return make_segment(TS_TFONT, struct.pack('>BHB',0,fid,plane))
def seg_char(sz,wt=400,rgb=0x000000) -> bytes:
    return make_segment(TS_TCHAR, struct.pack('>BHHI',0,sz,wt,rgb))
def seg_ruler(lp=22,ind=0) -> bytes:
    return make_segment(TS_TRULER, struct.pack('>BHH',0,lp,ind))
def seg_vobj(tid,label,path='') -> bytes:
    lb,pb = label.encode('utf-8'),path.encode('utf-8')
    return make_segment(TS_VOBJ,
        struct.pack('>BI',0,tid)+struct.pack('>H',len(lb))+lb+struct.pack('>H',len(pb))+pb)
def seg_hr(width=680) -> bytes:
    return make_segment(TS_FPRIM, struct.pack('>BBIhhhh',1,0,0x888888,0,0,width,0))
def seg_text(text) -> bytes:
    return (text+'\n').encode('utf-8')

def seg_image(src: str, caption: str = '', width: int = 480,
              height: int = 140) -> bytes:
    cb, sb = caption.encode('utf-8'), src.encode('utf-8')
    payload = (struct.pack('>BHHB', 10, width, height, 0)
               + struct.pack('>H', len(cb)) + cb
               + struct.pack('>H', len(sb)) + sb)
    return make_segment(TS_FPRIM, payload)

def get_image_dimensions(path: str):
    try:
        with open(path, 'rb') as f:
            hdr = f.read(24)
        if hdr[:3] == b'GIF':
            return struct.unpack_from('<HH', hdr, 6)
        if hdr[:8] == b'\x89PNG\r\n\x1a\n' and hdr[12:16] == b'IHDR':
            return struct.unpack_from('>II', hdr, 16)
    except Exception:
        pass
    return 480, 140

# Site book folders and the tad_bin catalog names the browser already searches.
_BOOK_SEG = {
    'cores': 'b-book',
    'media': 'b-media',
    'specs': 'b-spec',
    'apps': 'b-system/apps',
    'settings': 'b-system/settings',
    'kernel': 'b-system/kernel',
    'virtualization': 'b-system/virtualization',
    'conformance': 'b-system/conformance',
    'verification': 'b-system/verification',
}

def to_tad_href(href: str) -> str:
    """Map a site href onto a path the TAD browser can open without source changes.

    Intra-book relatives (kernel.html, ../shared_data/x.html) stay relative.
    A hop into cores, media, or specs becomes b-book, b-media, or b-spec so
    the browser's tad_bin/ prefix fallback finds the file.
    """
    if not href or href.startswith(('#', 'mailto:')) or '://' in href:
        return href
    path, frag = href, ''
    if '#' in path:
        path, frag = path.split('#', 1)
        frag = '#' + frag
    query = ''
    if '?' in path:
        path, query = path.split('?', 1)
        query = '?' + query
    segs = []
    up = 0
    for part in path.split('/'):
        if part == '..':
            if segs:
                segs.pop()
            else:
                up += 1
        elif part and part != '.':
            segs.append(part)
    if segs and segs[0] == 'doc' and len(segs) > 1 and segs[1] in _BOOK_SEG:
        segs = [_BOOK_SEG[segs[1]]] + segs[2:]
        up = 0
    elif segs and segs[0] in _BOOK_SEG:
        segs = [_BOOK_SEG[segs[0]]] + segs[1:]
        up = 0
    return ('../' * up) + '/'.join(segs) + query + frag

def _image_file(src: str, base_dir: str) -> str:
    cands = []
    if base_dir and not src.startswith('/'):
        cands.append(os.path.join(base_dir, src))
    cands.append(src)
    if src.startswith('b-spec/'):
        cands.append(os.path.join('doc/specs', src[len('b-spec/'):]))
    for cand in cands:
        if cand and os.path.isfile(cand):
            return cand
    return ''


# ── Binary TAD Compiler ───────────────────────────────────────────────────────
def compile_to_binary_tad(elements: list, base_dir: str = '') -> bytes:
    init = seg_page()+seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0)
    body = b''
    for elem in elements:
        tag = elem[0]
        if tag == 'h1':
            body += (seg_font(1,1)+seg_char(20,700,0x001833)+seg_ruler(30,0)
                     +seg_text('\n■ '+elem[1])+seg_hr(680)
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'h2':
            body += (seg_font(1,1)+seg_char(16,700,0x003355)+seg_ruler(26,0)
                     +seg_text('\n▶ '+elem[1])+seg_hr(560)
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'h3':
            body += (seg_font(1,1)+seg_char(13,600,0x004466)+seg_ruler(24,0)
                     +seg_text('\n▼ '+elem[1])
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'h4':
            body += (seg_font(1,1)+seg_char(12,600,0x444444)+seg_ruler(20,0)
                     +seg_text('● '+elem[1])
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'api_card':
            _, name, ret_type, desc, sig, table, meta = elem
            body += (seg_font(1,1)+seg_char(14,700,0x005577)+seg_ruler(24,0)
                     +seg_text('\n◆ FUNCTION: '+name+'  ['+ret_type+']'))
            if desc:
                body += (seg_font(0,1)+seg_char(12,400,0x222222)+seg_ruler(20,10)
                         +seg_text('  '+desc))
            if sig:
                body += (seg_font(2,1)+seg_char(11,600,0x0A2540)+seg_ruler(16,20)
                         +seg_text('  ┌─ C99 Prototype ──────────────────────────────────────────\n  │ '
                                   +sig+'\n  └──────────────────────────────────────────────────────────'))
            if table:
                body += (seg_font(2,1)+seg_char(10,400,0x111111)+seg_ruler(15,20)
                         +seg_text(format_table_ascii(table)))
            if meta:
                meta_lines = '\n'.join(f'  • {k}: {v}' for k, v in meta)
                body += (seg_font(0,1)+seg_char(11,500,0x475569)+seg_ruler(18,20)
                         +seg_text(meta_lines))
            body += (seg_hr(680)+seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'callout':
            _, title, cbody = elem
            body += (seg_font(1,1)+seg_char(11,600,0x1E3A5F)+seg_ruler(20,10)
                     +seg_text('┌─ NOTE: '+title+' ─────────────────────────────────────────')
                     +seg_text('│ '+cbody)
                     +seg_text('└──────────────────────────────────────────────────────────')
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'code_block':
            body += (seg_font(2,1)+seg_char(10,400,0x1E293B)+seg_ruler(16,20)
                     +seg_text(elem[1]+'\n')
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'table':
            body += (seg_font(2,1)+seg_char(10,400,0x000000)+seg_ruler(16,10)
                     +seg_text(format_table_ascii(elem[1])+'\n')
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'ul':
            text = '\n'.join('  • '+it for it in elem[1])
            body += (seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(20,10)
                     +seg_text(text+'\n')+seg_ruler(22,0))
        elif tag == 'ol':
            text = '\n'.join(f'  {i+1}. {it}' for i,it in enumerate(elem[1]))
            body += (seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(20,10)
                     +seg_text(text+'\n')+seg_ruler(22,0))
        elif tag == 'hr':
            body += seg_hr(720)
        elif tag in ('p','page_lead'):
            body += seg_text(elem[1])
        elif tag == 'image':
            src, alt = elem[1], elem[2] if len(elem) > 2 else ''
            path = _image_file(src, base_dir)
            w, h = get_image_dimensions(path) if path else (480, 140)
            body += seg_image(src, alt, int(w), int(h))
        elif tag == 'link':
            href = to_tad_href(elem[1])
            body += seg_vobj(link_id(href), elem[2], href)

    payload = init + body
    return struct.pack('>HI', RECORD_TYPE_MAIN, len(payload)) + payload


# ── Symbolic TAD Compiler ─────────────────────────────────────────────────────
def compile_to_symbolic_tad(elements: list, doc_title: str = 'B-Book Reference Manual') -> str:
    today = date.today().isoformat()
    header = (
        '================================================================================\n'
        f'TAD Real Body [実身] : {doc_title}\n'
        'RECORD TYPE : 1 (TAD Main Record) | B-Book BeBook-Style Developer Reference\n'
        f'DATE        : {today} | GENERATOR : book2tad.py (Python stdlib)\n'
        '================================================================================\n\n'
        f'[付箋: DOCUMENT_HEADER | Title="{doc_title}" | Layout="BeBook-TwoColumn" | Spec="BTRON 3.20"]\n'
    )
    parts = []
    for elem in elements:
        tag = elem[0]
        if tag == 'h1':
            parts.append('【'+elem[1]+'】\n'+'━'*60)
        elif tag == 'page_lead':
            parts.append('>>> '+elem[1])
        elif tag == 'callout':
            _, title, body = elem
            parts.append(
                f'┌─ NOTE: {title} ─────────────────────────────────────────\n'
                f'│ {body}\n'
                f'└──────────────────────────────────────────────────────────')
        elif tag == 'h2':
            parts.append('\n■ '+elem[1]+'\n'+'─'*50)
        elif tag == 'h3':
            parts.append('\n▶ '+elem[1])
        elif tag == 'h4':
            parts.append('◆ '+elem[1])
        elif tag == 'api_card':
            _, name, ret_type, desc, sig, table, meta = elem
            tbl_part = ('\n'+format_table_ascii(table)) if table else ''
            meta_part = ('\n'+'\n'.join(f'  • {k}: {v}' for k,v in meta)) if meta else ''
            tbl_section = ('│ Parameters & Invariants:\n'
                           + tbl_part.replace('\n','\n│   ')+'\n│') if tbl_part else ''
            meta_section = (meta_part+'\n│') if meta_part else ''
            parts.append(
                f'┌── FUNCTION: {name} [{ret_type}] ──────────────────────────────\n'
                f'│ {desc}\n│\n│ C99 Prototype:\n│   {sig}\n'
                f'{tbl_section}{meta_section}'
                f'└─────────────────────────────────────────────────────────────────')
        elif tag == 'code_block':
            parts.append('```\n'+elem[1]+'\n```')
        elif tag == 'table':
            parts.append(format_table_ascii(elem[1]))
        elif tag == 'ul':
            parts.append('\n'.join('  • '+i for i in elem[1]))
        elif tag == 'ol':
            parts.append('\n'.join(f'  {i+1}. {it}' for i,it in enumerate(elem[1])))
        elif tag == 'p':
            parts.append(elem[1])
        elif tag == 'hr':
            parts.append('─'*70)
        elif tag == 'link':
            href = to_tad_href(elem[1])
            parts.append(f'[仮身] #{link_id(href)} : {elem[2]} -> [{href}]')
        elif tag == 'image':
            src, alt = elem[1], elem[2] if len(elem) > 2 else ''
            parts.append(f'[付箋: FIGURE src="{src}" caption="{alt}"]')
    return header + '\n\n'.join(parts) + '\n'


# ── Doc title name ────────────────────────────────────────────────────────────
_MEDIA_DOCS = {
    'index': 'B-Media', 'core': 'B-Core', 'audio': 'B-Audio',
    'video': 'B-Video', 'sound': 'B-Sound', 'rtp': 'B-RTP',
    'msx': 'B-MSX', 'gl': 'B-GL',
}

def _doc_title_name(base_name: str, html_path: str, src_root: str, kind: str) -> str:
    if (kind == 'media' and base_name in _MEDIA_DOCS
            and os.path.dirname(os.path.abspath(html_path)) == os.path.abspath(src_root)):
        return _MEDIA_DOCS[base_name]
    if base_name != 'index':
        return base_name
    rel = os.path.relpath(html_path, src_root) if src_root else html_path
    sub = os.path.dirname(rel)
    if sub and sub != '.':
        m = {'vobject':'VObject','hmi':'HMI'}
        return m.get(sub.lower(), sub.lower().capitalize())
    if kind == 'spec':
        return 'B-Spec'
    if kind == 'media':
        return 'B-Media'
    if kind == 'system':
        return 'B-System'
    return 'B-Book'


# ── File Processor ────────────────────────────────────────────────────────────
def _copy_asset_dir(html_dir: str, target_dir: str, name: str) -> None:
    src = os.path.join(html_dir, name)
    dst = os.path.join(target_dir, name)
    if os.path.isdir(src) and not os.path.isdir(dst):
        shutil.copytree(src, dst)

def process_file(html_path: str, target_dir: str, src_root: str = '', kind: str = 'book') -> dict:
    os.makedirs(target_dir, exist_ok=True)
    html_dir = os.path.dirname(os.path.abspath(html_path))
    for asset in ('gif', 'img', 'figures'):
        _copy_asset_dir(html_dir, target_dir, asset)
    base_name = os.path.splitext(os.path.basename(html_path))[0]
    with open(html_path, 'r', encoding='utf-8', errors='replace') as f:
        html = f.read()

    # title from <title> tag
    tm = re.search(r'<title>(.*?)</title>', html, re.I|re.S)
    title = decode_entities(strip_tags(tm.group(1))) if tm else base_name

    elements = parse_book_html(html)
    doc_title_name = _doc_title_name(base_name, html_path, src_root, kind)

    bin_path = os.path.join(target_dir, doc_title_name+'.tad')
    txt_path = os.path.join(target_dir, doc_title_name+'.tad.txt')

    bin_tad = compile_to_binary_tad(elements, html_dir)
    sym_tad = compile_to_symbolic_tad(elements, title)

    with open(bin_path,'wb') as f: f.write(bin_tad)
    with open(txt_path,'w',encoding='utf-8') as f: f.write(sym_tad)

    if base_name == 'index' and doc_title_name != 'index':
        with open(os.path.join(target_dir,'index.tad'),'wb') as f: f.write(bin_tad)
        with open(os.path.join(target_dir,'index.tad.txt'),'w',encoding='utf-8') as f: f.write(sym_tad)

    return {'html_path':html_path,'bin_path':bin_path,'txt_path':txt_path,
            'elements_count':len(elements),'bin_bytes':len(bin_tad),'title':doc_title_name}


# ── Self-Test ─────────────────────────────────────────────────────────────────
def run_tests():
    print('Running BtronBook.Compiler unit tests (Python)...')
    failures = []
    def chk(cond, msg):
        status = 'PASS' if cond else 'FAIL'
        print(f'  [{status}] {msg}')
        if not cond: failures.append(msg)

    sample_html = """<!DOCTYPE html><html>
    <head><title>Test Book</title></head>
    <body>
      <nav class="book-sidebar"><a>Ignored link</a></nav>
      <main class="book-content">
        <h1>Kernel Specification</h1>
        <div class="callout callout-info">
          <div class="callout-title">Important Scope</div>
          Zero dynamic heap allocations in fast paths.
        </div>
        <h2>1. Architecture</h2>
        <p>Preemptive priority scheduling.</p>
        <pre><code>
        [ Ready Queue ] ──► [ Task Dispatcher ]
        </code></pre>
        <h2>2. API Reference</h2>
        <div class="api-card">
          <div class="api-header">
            <span class="api-name">tk_cre_tsk</span>
            <span class="ret-badge">ID</span>
          </div>
          <p>Creates a new task within the kernel execution context.</p>
          <div class="api-signature">ID tk_cre_tsk(const T_CTSK *pk_ctsk);</div>
          <table class="api-table">
            <tr><th>Parameter</th><th>Type</th><th>Description</th></tr>
            <tr><td>pk_ctsk</td><td>const T_CTSK*</td><td>Task packet.</td></tr>
          </table>
          <div class="api-meta">
            <span class="meta-label">Errors</span><span>ER_NOMEM, ER_LIMIT</span>
          </div>
        </div>
      </main>
    </body></html>"""

    elements = parse_book_html(sample_html)
    chk(len(elements) >= 6, f'Parsed elements count ({len(elements)} >= 6)')
    api = next((e for e in elements if e[0]=='api_card' and e[1]=='tk_cre_tsk'), None)
    chk(api is not None, 'API Card extraction')
    cb = next((e for e in elements if e[0]=='code_block'), None)
    chk(cb is not None and '\n' in cb[1], 'Code block newline preservation')
    bin_tad = compile_to_binary_tad(elements)
    rec_type, payload_len = struct.unpack_from('>HI', bin_tad)
    chk(rec_type == 1, 'Binary TAD header validation')
    chk(payload_len == len(bin_tad)-6, 'Payload length matches')

    chk(to_tad_href('../cores/window.html') == 'b-book/window.html',
        'Site cores link maps to tad_bin b-book')
    chk(to_tad_href('../media/gl.html') == 'b-media/gl.html',
        'Site media link maps to tad_bin b-media')
    chk(to_tad_href('../../specs/shared_data/tad1.html') == 'b-spec/shared_data/tad1.html',
        'Site specs link maps to tad_bin b-spec')
    chk(to_tad_href('kernel.html') == 'kernel.html', 'Intra-book link stays relative')
    chk(to_tad_href('../shared_data/tad1.html') == '../shared_data/tad1.html',
        'Spec chapter link stays relative')

    figure_html = """<article><section>
        <ul><li><a href="../cores/kernel.html">Kernel</a></li></ul>
        <figure><img src="gif/all_struct.gif" alt="Shared data"><figcaption>Figure 1</figcaption></figure>
      </section></article>"""
    fig_elems = parse_book_html(figure_html)
    chk(any(e[0]=='link' and e[1]=='../cores/kernel.html' for e in fig_elems),
        'Link list becomes a virtual body')
    chk(any(e[0]=='image' and e[1]=='gif/all_struct.gif' and e[2]=='Figure 1' for e in fig_elems),
        'Figure image and caption are kept')
    fig_bin = compile_to_binary_tad(fig_elems)
    chk(struct.pack('>H', TS_VOBJ) in fig_bin, 'Link compiles to TS_VOBJ')
    chk(struct.pack('>H', TS_FPRIM) in fig_bin, 'Image compiles to TS_FPRIM')
    chk(b'b-book/kernel.html' in fig_bin, 'Compiled link uses the b-book catalog name')

    if failures:
        print(f'  {len(failures)} test(s) FAILED.')
        sys.exit(1)
    print('All BtronBook.Compiler unit tests passed!\n')


# ── Canonical books the cabinet opens by a fixed path ─────────────────────────
def compile_foundational_books(target_dir: str = 'tad_bin') -> None:
    """Three portal real bodies. Image paths stay b-spec/... so the browser
    resolves them under tad_bin/ after the spec gifs are copied there."""
    btron3_elements = [
        ('h1', 'BTRON3 仕様書 バージョン 3.20.00 (Sakamura BTRON Architecture)'),
        ('image', 'b-spec/shared_data/gif/all_struct.gif',
         '図 1: BTRON3 実身・仮身データ構造仕様 (Shared Data Structure)'),
        ('link', 'b-spec/shared_data/index.tad',
         'Part 1: 共通データ構造仕様 (Shared Data Specifications)'),
        ('link', 'b-spec/os_spec/index.tad',
         'Part 2: オペレーティングシステム機能仕様 (OS Specification)'),
        ('link', 'b-spec/os_spec/indexfig.tad',
         'Static Analysis & Bounded Heap Memory Model (NASA JPL Rule 3)'),
        ('h2', '目次 (Table of Contents)'),
        ('h3', '第1部：共通データ構造仕様 (Shared Data)'),
        ('link', 'b-spec/shared_data/data_type.tad',
         '第1章 基本データ型とエラーコード (Data Types & Error Codes)'),
        ('link', 'b-spec/shared_data/tron_code.tad',
         '第2章 TRONコード文字体系仕様 (TRON Multilingual Character Code)'),
        ('link', 'b-spec/shared_data/tad1.tad',
         '第3章 TAD (TRON Application Databus) 文書フォーマット仕様'),
        ('link', 'b-spec/shared_data/fd_format.tad',
         '第4章 BTRON FS 実身／仮身ファイルシステム構造仕様'),
        ('h3', '第2部：OS機能仕様 (OS Specification)'),
        ('link', 'b-spec/os_spec/kernel/kernel.tad',
         '第5章 μITRON リアルタイムカーネルとタスク管理 (Kernel & Tasking)'),
        ('link', 'b-spec/os_spec/dp/dp.tad',
         '第6章 表示プリミティブ DP (Display Primitives Graphics Engine)'),
        ('link', 'b-spec/os_spec/shell/shell.tad',
         '第7章 GUIシェルとウィンドウマネージャ (Window Manager & Shell)'),
        ('link', 'b-spec/os_spec/indexfig.tad',
         '第8章 静的解析と決定論的メモリ制約 (Static Scope & Bounded Memory)'),
        ('hr',),
        ('h2', '基本データ型定義 (C99 Types)'),
        ('code_block',
         'typedef char            B;   /* 符号付き 8ビット整数 */\n'
         'typedef short           H;   /* 符号付き 16ビット整数 */\n'
         'typedef int             W;   /* 符号付き 32ビット整数 */\n'
         'typedef unsigned char   UB;  /* 符号なし 8ビット整数 */\n'
         'typedef unsigned short  UH;  /* 符号なし 16ビット整数 */\n'
         'typedef unsigned int    UW;  /* 符号なし 32ビット整数 */\n'
         'typedef void           *VP;  /* 汎用ポインタ */'),
        ('p', 'BTRON3仕様では、ブート完了後の動的ヒープ確保 (malloc/free) を完全禁止し、全てのメモリ領域を有界化します。'),
    ]
    bcore_elements = [
        ('h1', 'B-Core インターコア・ストリーミングプロトコル仕様書 (InterCore Streaming Protocol)'),
        ('image', 'b-spec/os_spec/kernel/gif/processtask.gif',
         '図 2: 五呼出プロトコル状態モデル (pub/sub/spawn/snd/rcv State Model)'),
        ('link', 'b-media/B-Core.tad',
         '第1章 InterCore ストリーミングプロトコル開発者仕様 (Protocol Developer Spec)'),
        ('link', 'b-media/theorems/media_intercore.tad',
         '第2章 Star Ring 形式検証台帳 (602 Coq判定・公理ゼロ)'),
        ('h2', '五呼出アルファベット (The Five-Call Alphabet)'),
        ('ol', [
            'pub  — 発信カーソル生成 (bounded publisher cursor)',
            'sub  — 受信カーソル末尾結合 (late joiner never replays)',
            'spawn — コア指定とカーソル排他所有権移転 (ownership replaces the mutex)',
            'snd  — 非ブロック送信 FULL(drop) 付き (non-blocking, says why)',
            'rcv  — 厳密非ブロック O(1) 受信 EMPTY (no scheduler yield, ruling R7)',
        ]),
        ('p', 'セクタプール保存則 pool_free + Σ resident = 64 と enq = deq + drop により、Clean-room BTRONマイクロカーネルの通信心臓部は実行時ヒープ確保ゼロで動作する。'),
    ]
    bfree_elements = [
        ('h1', 'B-Free 自由なBTRON3オペレーティングシステム技術解説書'),
        ('image', 'b-spec/os_spec/kernel/gif/filesystem.gif',
         '図 3: BTRON ファイルシステム構造仕様 (Filesystem Structure)'),
        ('h2', '設計理念と自由ソフトウェアの精神'),
        ('p', 'Ken Sakamura教授が提唱した「万人に開かれた標準」をGPL (GNU General Public License) の下で実現するクリーンルーム実装。'),
    ]
    books = [
        (os.path.join('b-spec', '01_btron3_spec'), 'BTRON3 3.20 Specification Book', btron3_elements),
        (os.path.join('b-media', '02_bcore_book'), 'B-Core InterCore Streaming Protocol Book', bcore_elements),
        (os.path.join('b-system', '03_bfree_os_book'), 'B-Free Operating System Book', bfree_elements),
    ]
    print('======================================================================')
    print(' B-System Foundational TAD Books (article compiler)')
    print(f' Output Directory: ./{target_dir}/')
    print('======================================================================')
    for rel_name, title, elems in books:
        out_base = os.path.join(target_dir, rel_name)
        os.makedirs(os.path.dirname(out_base), exist_ok=True)
        bin_tad = compile_to_binary_tad(elems, '')
        txt_tad = compile_to_symbolic_tad(elems, title)
        with open(out_base + '.tad', 'wb') as f:
            f.write(bin_tad)
        with open(out_base + '.tad.txt', 'w', encoding='utf-8') as f:
            f.write(txt_tad)
        print(f'  [COMPILED BOOK]   {(rel_name + ".tad").ljust(32)} : {title} ({len(bin_tad)} bytes)')
    print('======================================================================\n')


def compile_catalog(src_dir: str, out_dir: str, kind: str) -> int:
    if not os.path.isdir(src_dir):
        print(f'  [SKIP] {src_dir} is not present')
        return 0
    print('======================================================================')
    print(f' TAD book compiler  {src_dir}  ->  {out_dir}  ({kind})')
    print('======================================================================')
    html_files = sorted(glob.glob(os.path.join(src_dir, '**', '*.html'), recursive=True))
    total = 0
    for html_file in html_files:
        rel = os.path.relpath(html_file, src_dir)
        sub = os.path.dirname(rel)
        tdir = out_dir if (not sub or sub == '.') else os.path.join(out_dir, sub)
        res = process_file(html_file, tdir, src_dir, kind)
        out_n = os.path.relpath(res['bin_path'], 'tad_bin')
        print(f"  [BOOK2TAD] {rel.ljust(42)} -> {out_n} "
              f"({res['elements_count']} items, {res['bin_bytes']} bytes)")
        total += 1
    print(f'  compiled {total} documents into {out_dir}')
    print('======================================================================\n')
    return total


# ── Main ──────────────────────────────────────────────────────────────────────
# Exactly five canonical book directories live under tad_bin/:
#   1) b-book    — B-Book (doc/cores)
#   2) b-media   — B-Media (doc/media)
#   3) b-spec    — B-Spec (doc/specs)
#   4) b-system  — B-System (apps, settings, doc/kernel, virtualization, conformance, verification)
#   5) osal      — Osal Book (Tibetan Longchen Osal TADs exported by Clarity)
ALLOWED_TOP_DIRS = {'b-book', 'b-media', 'b-spec', 'b-system', 'osal'}

CATALOGS = (
    ('doc/cores',          'tad_bin/b-book',                  'book'),
    ('doc/media',          'tad_bin/b-media',                 'media'),
    ('doc/specs',          'tad_bin/b-spec',                  'spec'),
    ('apps',               'tad_bin/b-system/apps',           'system'),
    ('settings',           'tad_bin/b-system/settings',       'system'),
    ('doc/kernel',         'tad_bin/b-system/kernel',         'system'),
    ('doc/virtualization', 'tad_bin/b-system/virtualization', 'system'),
    ('doc/conformance',    'tad_bin/b-system/conformance',    'system'),
    ('doc/verification',   'tad_bin/b-system/verification',   'system'),
)

SYSTEM_SINGLE_PAGES = (
    ('index.html',       'tad_bin/b-system', 'system'),
    ('doc/license.html', 'tad_bin/b-system', 'system'),
)


def _clean_tad_bin_leftovers(root: str = 'tad_bin') -> None:
    """Keep only the five canonical book directories under tad_bin/."""
    os.makedirs(root, exist_ok=True)
    for entry in os.listdir(root):
        path = os.path.join(root, entry)
        if entry not in ALLOWED_TOP_DIRS or not os.path.isdir(path):
            if os.path.isdir(path) and not os.path.islink(path):
                shutil.rmtree(path)
            else:
                os.unlink(path)
    # Rebuild the four HTML-compiled book folders fresh (preserve osal/).
    for d in ('b-book', 'b-media', 'b-spec', 'b-system'):
        p = os.path.join(root, d)
        if os.path.isdir(p):
            shutil.rmtree(p)
        os.makedirs(p, exist_ok=True)
    os.makedirs(os.path.join(root, 'osal'), exist_ok=True)


if __name__ == '__main__':
    args = sys.argv[1:]
    if '--test' in args:
        run_tests()

    _clean_tad_bin_leftovers('tad_bin')
    for src_dir, out_dir, kind in CATALOGS:
        compile_catalog(src_dir, out_dir, kind)
    for html_file, out_dir, kind in SYSTEM_SINGLE_PAGES:
        if os.path.isfile(html_file):
            process_file(html_file, out_dir, os.path.dirname(html_file) or '.', kind)
    compile_foundational_books('tad_bin')
