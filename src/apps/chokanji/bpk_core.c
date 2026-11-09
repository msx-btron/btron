/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	bpk_lh5.c
 *	The LH5 decoder (design 17.16)
 *
 *	The input is read as a stream of bits, most significant first; the
 *	next 16 of them are always in bitbuf. A block starts with its count
 *	of codes and three tables: the code lengths of the code lengths, the
 *	code lengths of the literals and lengths, and those of the
 *	distances. A code of up to 12 bits (8 for the small tables) is found
 *	in one look in its table; a longer one goes on down a tree of left
 *	and right from there.
 *
 *	A code below 256 is a byte; above, it is a length of 3 to 256 and a
 *	distance follows: the number of its bits and then the bits. The
 *	whole output is one buffer, so the window is the output itself.
 */

#include <btron/bpk.h>
#include <btron/troncode.h>

#define THRESHOLD	3
#define CBIT		9
#define TBIT		5
#define PBIT		4

/* ---------------------------------------------------------------- bits */

LOCAL UINT next_byte( BPKLH5 *z )
{
	if ( z->inpos < z->inlen ) return z->in[z->inpos++];
	z->over++;
	return 0;
}

/* n bits out of bitbuf, the next n moved in */
LOCAL void fillbuf( BPKLH5 *z, INT n )
{
	z->bitbuf = ( z->bitbuf << n ) & 0xFFFF;
	while ( n > z->bitcount ) {
		n -= z->bitcount;
		z->bitbuf |= ( z->subbitbuf << n ) & 0xFFFF;
		z->subbitbuf = next_byte(z);
		z->bitcount = 8;
	}
	z->bitcount -= n;
	z->bitbuf |= z->subbitbuf >> z->bitcount;
	z->subbitbuf &= ( 1U << z->bitcount ) - 1;
}

LOCAL UINT getbits( BPKLH5 *z, INT n )
{
	UINT	x;

	if ( n == 0 ) return 0;
	x = z->bitbuf >> ( 16 - n );
	fillbuf(z, n);
	return x;
}

/* ---------------------------------------------------------------- tables */

/*
 * The table for codes of the lengths given: every code of up to
 * tablebits bits fills the entries it begins; a longer one takes the
 * entry of its first tablebits bits and a path of nodes from there.
 */
LOCAL BOOL make_table( BPKLH5 *z, INT nchar, const UB *bitlen, INT tablebits, UH *table )
{
	UINT	count[17], weight[17], start[18];
	UINT	i, k, len, avail, nextcode, mask, jutbits;
	INT	ch;
	UH	*p;

	for ( i = 1; i <= 16; i++ ) count[i] = 0;
	for ( ch = 0; ch < nchar; ch++ ) {
		if ( bitlen[ch] > 16 ) return FALSE;
		count[bitlen[ch]]++;
	}
	start[1] = 0;
	for ( i = 1; i <= 16; i++ ) start[i + 1] = start[i] + ( count[i] << ( 16 - i ) );
	if ( start[17] != 0x10000 ) return FALSE;

	jutbits = 16 - (UINT)tablebits;
	for ( i = 1; i <= (UINT)tablebits; i++ ) {
		start[i] >>= jutbits;
		weight[i] = 1U << ( tablebits - i );
	}
	for ( ; i <= 16; i++ ) weight[i] = 1U << ( 16 - i );

	i = start[tablebits + 1] >> jutbits;
	k = 1U << tablebits;
	while ( i < k ) table[i++] = 0;

	avail = (UINT)nchar;
	mask = 1U << ( 15 - tablebits );
	for ( ch = 0; ch < nchar; ch++ ) {
		len = bitlen[ch];
		if ( len == 0 ) continue;
		nextcode = start[len] + weight[len];
		if ( len <= (UINT)tablebits ) {
			if ( nextcode > ( 1U << tablebits ) ) return FALSE;
			for ( i = start[len]; i < nextcode; i++ ) table[i] = (UH)ch;
		} else {
			k = start[len];
			p = &table[k >> jutbits];
			i = len - (UINT)tablebits;
			while ( i != 0 ) {
				if ( *p == 0 ) {
					if ( avail >= 2 * LH5_NC - 1 ) return FALSE;
					z->right[avail] = z->left[avail] = 0;
					*p = (UH)avail++;
				}
				p = ( k & mask ) ? &z->right[*p] : &z->left[*p];
				k <<= 1;
				i--;
			}
			*p = (UH)ch;
		}
		start[len] = nextcode;
	}
	return TRUE;
}

/* A code of the table of nchar codes whose first 8 bits index pt_table */
LOCAL UINT pt_code( BPKLH5 *z, UINT nchar )
{
	UINT	c = z->pt_table[z->bitbuf >> 8], mask = 1U << 7;

	while ( c >= nchar ) {
		if ( mask == 0 || c >= 2 * LH5_NC - 1 ) {
			z->bad = TRUE;
			return 0;
		}
		c = ( z->bitbuf & mask ) ? z->right[c] : z->left[c];
		mask >>= 1;
	}
	fillbuf(z, z->pt_len[c]);
	return c;
}

LOCAL void read_pt_len( BPKLH5 *z, INT nn, INT nbit, INT i_special )
{
	INT	i, n;
	UINT	c, mask;

	n = (INT)getbits(z, nbit);
	if ( n == 0 ) {
		c = getbits(z, nbit);
		for ( i = 0; i < nn; i++ ) z->pt_len[i] = 0;
		for ( i = 0; i < 256; i++ ) z->pt_table[i] = (UH)c;
		return;
	}
	if ( n > nn ) {
		z->bad = TRUE;
		return;
	}
	i = 0;
	while ( i < n ) {
		c = z->bitbuf >> 13;
		if ( c == 7 ) {
			mask = 1U << 12;
			while ( ( mask & z->bitbuf ) != 0 && c < 16 ) {
				mask >>= 1;
				c++;
			}
		}
		fillbuf(z, ( c < 7 ) ? 3 : (INT)c - 3);
		z->pt_len[i++] = (UB)c;
		if ( i == i_special ) {
			INT	k = (INT)getbits(z, 2);

			while ( --k >= 0 && i < nn ) z->pt_len[i++] = 0;
		}
	}
	while ( i < nn ) z->pt_len[i++] = 0;
	if ( !make_table(z, nn, z->pt_len, 8, z->pt_table) ) z->bad = TRUE;
}

LOCAL void read_c_len( BPKLH5 *z )
{
	INT	i, n, k;
	UINT	c;

	n = (INT)getbits(z, CBIT);
	if ( n == 0 ) {
		c = getbits(z, CBIT);
		for ( i = 0; i < LH5_NC; i++ ) z->c_len[i] = 0;
		for ( i = 0; i < 4096; i++ ) z->c_table[i] = (UH)c;
		return;
	}
	if ( n > LH5_NC ) {
		z->bad = TRUE;
		return;
	}
	i = 0;
	while ( i < n && !z->bad ) {
		c = pt_code(z, LH5_NT);
		if ( c <= 2 ) {
			k = ( c == 0 ) ? 1 : ( c == 1 ) ? (INT)getbits(z, 4) + 3 : (INT)getbits(z, CBIT) + 20;
			while ( --k >= 0 && i < LH5_NC ) z->c_len[i++] = 0;
		} else {
			z->c_len[i++] = (UB)( c - 2 );
		}
	}
	while ( i < LH5_NC ) z->c_len[i++] = 0;
	if ( !make_table(z, LH5_NC, z->c_len, 12, z->c_table) ) z->bad = TRUE;
}

/* ---------------------------------------------------------------- codes */

LOCAL UINT decode_c( BPKLH5 *z )
{
	UINT	j, mask;

	if ( z->blocksize == 0 ) {
		z->blocksize = getbits(z, 16);
		if ( z->blocksize == 0 ) {
			z->bad = TRUE;		/* the data ended before the output was whole */
			return 0;
		}
		read_pt_len(z, LH5_NT, TBIT, 3);
		if ( !z->bad ) read_c_len(z);
		if ( !z->bad ) read_pt_len(z, LH5_NP, PBIT, -1);
		if ( z->bad ) return 0;
	}
	z->blocksize--;
	j = z->c_table[z->bitbuf >> 4];
	mask = 1U << 3;
	while ( j >= LH5_NC ) {
		if ( mask == 0 || j >= 2 * LH5_NC - 1 ) {
			z->bad = TRUE;
			return 0;
		}
		j = ( z->bitbuf & mask ) ? z->right[j] : z->left[j];
		mask >>= 1;
	}
	fillbuf(z, z->c_len[j]);
	return j;
}

LOCAL UINT decode_p( BPKLH5 *z )
{
	UINT	j = pt_code(z, LH5_NP);

	if ( j != 0 ) j = ( 1U << ( j - 1 ) ) + getbits(z, (INT)j - 1);
	return j;
}

EXPORT INT lh5_decode( BPKLH5 *z, const UB *in, UINT inlen, UB *out, UINT outlen )
{
	UINT	pos = 0, c, len, dist;

	z->in = in;
	z->inlen = inlen;
	z->inpos = 0;
	z->over = 0;
	z->bitbuf = 0;
	z->subbitbuf = 0;
	z->bitcount = 0;
	z->blocksize = 0;
	z->bad = FALSE;
	fillbuf(z, 16);

	while ( pos < outlen ) {
		c = decode_c(z);
		if ( z->bad || z->over > 4 ) break;
		if ( c < 256 ) {
			out[pos++] = (UB)c;
			continue;
		}
		len = c - ( 256 - THRESHOLD );
		dist = decode_p(z) + 1;
		if ( z->bad ) break;
		while ( len-- > 0 && pos < outlen ) {
			/* before the start the window holds zeroes */
			out[pos] = ( dist <= pos ) ? out[pos - dist] : 0;
			pos++;
		}
	}
	if ( pos == 0 && ( z->bad || z->over > 4 ) ) return -1;
	return (INT)pos;
}
/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	bpk_arc.c
 *	An archive (書庫, BPK) read (design 17.16)
 *
 *	The archive file is a binary TAD: a 管理情報セグメント, a figure and
 *	in it a 指定付箋 whose application ID is 8000 C003 8000. The 付箋's
 *	data begins with the global header; the compressed stream follows
 *	it. Inflated, the stream is the extension, the local headers and
 *	then each object's records in turn, each a record header and its
 *	bytes; the local headers' offsets say the same. A link record names
 *	its target by the number of its local header, and a 実行機能付箋
 *	record lists the applications that open the object.
 */

#include <btron/bpk.h>
#include <btron/troncode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LOCAL UINT rd16( const UB *p )
{
	return (UINT)p[0] | ( (UINT)p[1] << 8 );
}

LOCAL UINT rd32( const UB *p )
{
	return (UINT)p[0] | ( (UINT)p[1] << 8 ) | ( (UINT)p[2] << 16 ) | ( (UINT)p[3] << 24 );
}

LOCAL void wr16( UB *p, UINT v )
{
	p[0] = (UB)v;
	p[1] = (UB)( v >> 8 );
}

LOCAL void wr32( UB *p, UINT v )
{
	p[0] = (UB)v;
	p[1] = (UB)( v >> 8 );
	p[2] = (UB)( v >> 16 );
	p[3] = (UB)( v >> 24 );
}

/* ---------------------------------------------------------------- the headers */

EXPORT void bpk_ghead_get( const UB *p, BPKGHEAD *h )
{
	h->headtype = p[GH_HEADTYPE];
	h->checksum = p[GH_CHECKSUM];
	h->version = rd16(p + GH_VERSION);
	h->crc = rd16(p + GH_CRC);
	h->nfiles = rd16(p + GH_NFILES);
	h->method = rd16(p + GH_COMPMETHOD);
	h->time = rd32(p + GH_TIME);
	h->filesize = rd32(p + GH_FILESIZE);
	h->origsize = rd32(p + GH_ORIGSIZE);
	h->compsize = rd32(p + GH_COMPSIZE);
	h->extsize = rd32(p + GH_EXTSIZE);
}

/* The sum of the global header's bytes after the checksum, as one byte */
EXPORT UINT bpk_ghead_sum( const UB *p )
{
	UINT	s = 0;
	INT	i;

	for ( i = GH_VERSION; i < BPK_GLOBALHEAD; i++ ) s += p[i];
	return s & 0xFF;
}

/* The global header written, its checksum made from what it holds */
EXPORT void bpk_ghead_put( const BPKGHEAD *h, UB *p )
{
	p[GH_HEADTYPE] = (UB)h->headtype;
	wr16(p + GH_VERSION, h->version);
	wr16(p + GH_CRC, h->crc);
	wr16(p + GH_NFILES, h->nfiles);
	wr16(p + GH_COMPMETHOD, h->method);
	wr32(p + GH_TIME, h->time);
	wr32(p + GH_FILESIZE, h->filesize);
	wr32(p + GH_ORIGSIZE, h->origsize);
	wr32(p + GH_COMPSIZE, h->compsize);
	wr32(p + GH_EXTSIZE, h->extsize);
	p[GH_CHECKSUM] = (UB)bpk_ghead_sum(p);
}

EXPORT void bpk_lhead_get( const UB *p, BPKLHEAD *h )
{
	INT	i;

	h->f_type = rd16(p + LH_F_TYPE);
	h->f_atype = rd16(p + LH_F_ATYPE);
	for ( i = 0; i < 20; i++ ) h->name[i] = (UH)rd16(p + LH_NAME + 2 * i);
	h->origid = (INT)(H)rd16(p + LH_ORIGID);
	h->compmethod = (INT)(H)rd16(p + LH_COMPMETHOD);
	h->origsize = rd32(p + LH_ORIGSIZE);
	h->compsize = rd32(p + LH_COMPSIZE);
	h->f_nlink = rd16(p + LH_F_NLINK);
	h->crc = rd16(p + LH_CRC);
	h->f_size = rd32(p + LH_F_SIZE);
	h->offset = rd32(p + LH_OFFSET);
	h->f_nrec = rd32(p + LH_F_NREC);
	h->f_ltime = rd32(p + LH_F_LTIME);
	h->f_atime = rd32(p + LH_F_ATIME);
	h->f_mtime = rd32(p + LH_F_MTIME);
	h->f_ctime = rd32(p + LH_F_CTIME);
}

EXPORT void bpk_lhead_put( const BPKLHEAD *h, UB *p )
{
	INT	i;

	memset(p, 0, BPK_LOCALHEAD);
	wr16(p + LH_F_TYPE, h->f_type);
	wr16(p + LH_F_ATYPE, h->f_atype);
	for ( i = 0; i < 20; i++ ) wr16(p + LH_NAME + 2 * i, h->name[i]);
	wr16(p + LH_ORIGID, (UINT)h->origid);
	wr16(p + LH_COMPMETHOD, (UINT)h->compmethod);
	wr32(p + LH_ORIGSIZE, h->origsize);
	wr32(p + LH_COMPSIZE, h->compsize);
	wr16(p + LH_F_NLINK, h->f_nlink);
	wr16(p + LH_CRC, h->crc);
	wr32(p + LH_F_SIZE, h->f_size);
	wr32(p + LH_OFFSET, h->offset);
	wr32(p + LH_F_NREC, h->f_nrec);
	wr32(p + LH_F_LTIME, h->f_ltime);
	wr32(p + LH_F_ATIME, h->f_atime);
	wr32(p + LH_F_MTIME, h->f_mtime);
	wr32(p + LH_F_CTIME, h->f_ctime);
}

/* ---------------------------------------------------------------- applications */

/* The applications a 実行機能付箋 may name, and the programs that are them */
typedef struct {
	UH		appl;		/* the middle word; the others are 8000 */
	const char	*prog;
	const char	*name;
	UINT		bgoff;		/* where its data keeps the window's colour, 0 none */
} APPLMAP;

LOCAL const APPLMAP applmap[] = {
	{ 0x0001, "virtual-object-list", "仮身一覧", 0 },
	{ 0x0002, "basic-figure-editor", "基本図形編集", 0x1E },
	{ 0x0003, "basic-text-editor", "基本文章編集", 0x16 },
	{ 0x0009, "basic-calc-editor", "基本表計算", 0x16 },
	{ 0x000B, "microscript", "マイクロスクリプト", 0x1E },
	{ BPK_APPL_ARCHIVE, "unpack-file", "書庫解凍", 0 },
};
#define NAPPL	( (INT)( sizeof(applmap) / sizeof(applmap[0]) ) )

LOCAL const APPLMAP *appl_find( const UH *appl )
{
	INT	i;

	if ( appl[0] != BPK_APPL_HI || appl[2] != BPK_APPL_HI ) return NULL;
	for ( i = 0; i < NAPPL; i++ ) {
		if ( applmap[i].appl == appl[1] ) return &applmap[i];
	}
	return NULL;
}

EXPORT const char *bpk_appl_prog( const UH *appl, const char **p_name )
{
	const APPLMAP	*m = appl_find(appl);

	if ( p_name != NULL ) *p_name = ( m != NULL ) ? m->name : NULL;
	return ( m != NULL ) ? m->prog : NULL;
}

/* ---------------------------------------------------------------- the archive */

/* The 指定付箋 that holds the archive: where its data starts, and its size */
LOCAL BOOL find_fusen( BPKARC *a, UINT *p_at, UINT *p_len )
{
	UINT	pos = 0;

	while ( pos + 2 <= a->rawlen ) {
		UINT	w = rd16(a->raw + pos), len;

		if ( w == 0 ) break;
		if ( w <= TC_SPEC ) {
			pos += 2;
			continue;
		}
		pos += 2;
		if ( w == TS_EXT ) continue;		/* its next word is the id */
		if ( pos + 2 > a->rawlen ) break;
		len = rd16(a->raw + pos);
		pos += 2;
		if ( len == TS_LONG ) {
			if ( pos + 4 > a->rawlen ) break;
			len = rd32(a->raw + pos);
			pos += 4;
		}
		if ( len > a->rawlen - pos ) break;
		if ( w == TS_DFUSEN && len >= DF_DATA + BPK_GLOBALHEAD ) {
			const UB	*d = a->raw + pos;

			if ( rd16(d + DF_APPL) == BPK_APPL_HI && rd16(d + DF_APPL + 2) == BPK_APPL_ARCHIVE
			  && rd16(d + DF_APPL + 4) == BPK_APPL_HI ) {
				UH	name[16];
				INT	i;

				for ( i = 0; i < 16; i++ ) name[i] = (UH)rd16(d + DF_NAME + 2 * i);
				bpk_tron_utf8(name, 16, a->name, sizeof(a->name));
				*p_at = pos;
				*p_len = len;
				return TRUE;
			}
		}
		pos += len;
	}
	return FALSE;
}

/* The entries of a 実行機能付箋 record: 96 bytes and the data each */
LOCAL void read_exec( BPKFILE *f, const UB *p, UINT size )
{
	UINT	at = 0;

	while ( at + BPK_EXECHEAD <= size ) {
		UINT		dlen = rd16(p + at + EX_DLEN), adv;
		const UB	*d = p + at + BPK_EXECHEAD;
		const APPLMAP	*m;
		BPKEXEC		*e, *grow;

		grow = realloc(f->exec, sizeof(BPKEXEC) * (size_t)( f->nexec + 1 ));
		if ( grow == NULL ) return;
		f->exec = grow;
		e = &f->exec[f->nexec++];
		memset(e, 0, sizeof(*e));
		e->appl[0] = (UH)rd16(p + at + EX_APPL);
		e->appl[1] = (UH)rd16(p + at + EX_APPL + 2);
		e->appl[2] = (UH)rd16(p + at + EX_APPL + 4);
		if ( at + BPK_EXECHEAD + dlen <= size ) {
			/* the window the application opens, a RECT after 4 bytes */
			if ( dlen >= 0x10 ) {
				e->wl = (H)rd16(d + 4);
				e->wt = (H)rd16(d + 6);
				e->wr = (H)rd16(d + 8);
				e->wb = (H)rd16(d + 10);
				e->haswin = TRUE;
			}
			m = appl_find(e->appl);
			if ( m != NULL && m->bgoff != 0 && dlen >= m->bgoff + 4 ) {
				e->bgraw = rd32(d + m->bgoff);
				e->hasbg = TRUE;
			}
			/* マイクロスクリプト keeps $SV, 50 numbers, at the end of its data */
			if ( m != NULL && m->appl == 0x000B && dlen >= 50 * 4 ) {
				INT	i;

				for ( i = 0; i < 50; i++ ) e->sv[i] = (INT)rd32(d + dlen - 200 + 4 * i);
				e->hassv = TRUE;
			}
		}
		adv = BPK_EXECHEAD + dlen;
		if ( adv & 1 ) adv++;
		at += adv;
	}
}

/*
 * The extension at the stream's start: a count, the length that
 * follows, then the root's link (VLINK, 52 bytes: fs_name TC[20],
 * f_id, attr, rel, appl[3]) and its virtual object segment (view RECT,
 * height, chsz, frcol, chcol, tbcol, bgcol, dlen).
 */
#define RX_COUNT	0x00
#define RX_ATTR		0x2E
#define RX_VIEW		0x38
#define RX_CHSZ		0x42
#define RX_FRCOL	0x44
#define RX_END		0x56		/* up to the segment's dlen */

LOCAL void read_root( BPKARC *a )
{
	const UB	*p = a->plain;
	INT		i;

	a->hasroot = FALSE;
	if ( p == NULL || a->extsize < RX_END || a->plainlen < RX_END || rd16(p + RX_COUNT) < 1 ) {
		return;
	}
	a->rootattr = rd16(p + RX_ATTR);
	for ( i = 0; i < 4; i++ ) {
		a->rootview[i] = (INT)(H)rd16(p + RX_VIEW + i * 2);
		a->rootcol[i] = rd32(p + RX_FRCOL + i * 4);
	}
	a->rootchsz = rd16(p + RX_CHSZ);
	a->hasroot = (BOOL)( a->rootview[2] > a->rootview[0] && a->rootview[3] > a->rootview[1] );
}

LOCAL ER fail( char *err, INT max, const char *s, ER er )
{
	if ( err != NULL && max > 0 ) {
		strncpy(err, s, (size_t)max - 1);
		err[max - 1] = 0;
	}
	return er;
}

EXPORT ER bpk_parse( BPKARC *a, const UB *raw, UINT rawlen, char *err, INT errmax )
{
	UINT		at = 0, flen = 0, pos, i;
	INT		j;
	const UB	*g;

	memset(a, 0, sizeof(*a));
	a->raw = raw;
	a->rawlen = rawlen;
	if ( !find_fusen(a, &at, &flen) ) return fail(err, errmax, "書庫の指定付箋が見つかりません", E_OBJ);
	g = raw + at + DF_DATA;
	bpk_ghead_get(g, &a->head);
	a->nfiles = a->head.nfiles;
	a->method = a->head.method;
	a->origsize = a->head.origsize;
	a->compsize = a->head.compsize;
	a->extsize = a->head.extsize;
	if ( a->method != BPK_LH0 && a->method != BPK_LH5 ) {
		return fail(err, errmax, "対応していない圧縮方式です", E_NOSPT);
	}
	if ( a->head.checksum != bpk_ghead_sum(g) || a->nfiles == 0 || a->compsize > flen - DF_DATA - BPK_GLOBALHEAD
	  || a->origsize < a->extsize + a->nfiles * BPK_LOCALHEAD || a->origsize > 256 * 1024 * 1024 ) {
		return fail(err, errmax, "書庫の見出しが壊れています", E_OBJ);
	}

	/* the stream inflated */
	a->plain = malloc(a->origsize);
	if ( a->plain == NULL ) return fail(err, errmax, "展開する場所がありません", E_NOMEM);
	if ( a->method == BPK_LH5 ) {
		BPKLH5	*z = malloc(sizeof(BPKLH5));
		INT	n;

		if ( z == NULL ) return fail(err, errmax, "展開する場所がありません", E_NOMEM);
		n = lh5_decode(z, g + BPK_GLOBALHEAD, a->compsize, a->plain, a->origsize);
		free(z);
		if ( n != (INT)a->origsize ) return fail(err, errmax, "圧縮データが壊れています", E_OBJ);
	} else {
		if ( a->origsize > a->compsize ) return fail(err, errmax, "書庫のデータが足りません", E_OBJ);
		memcpy(a->plain, g + BPK_GLOBALHEAD, a->origsize);
	}
	a->plainlen = a->origsize;

	/* the local headers */
	a->file = calloc(a->nfiles, sizeof(BPKFILE));
	if ( a->file == NULL ) return fail(err, errmax, "展開する場所がありません", E_NOMEM);
	pos = a->extsize;
	for ( i = 0; i < a->nfiles; i++, pos += BPK_LOCALHEAD ) {
		BPKFILE	*f = &a->file[i];

		bpk_lhead_get(a->plain + pos, &f->head);
		bpk_tron_utf8(f->head.name, 20, f->name, sizeof(f->name));
		if ( f->head.f_nrec > 65536 ) return fail(err, errmax, "書庫の実身の見出しが壊れています", E_OBJ);
		f->nrec = (INT)f->head.f_nrec;
	}

	/* each object's records, one object after another */
	for ( i = 0; i < a->nfiles; i++ ) {
		BPKFILE	*f = &a->file[i];

		f->rec = calloc(f->nrec ? (size_t)f->nrec : 1, sizeof(BPKREC));
		f->link = calloc(f->nrec ? (size_t)f->nrec : 1, sizeof(INT));
		if ( f->rec == NULL || f->link == NULL ) return fail(err, errmax, "展開する場所がありません", E_NOMEM);
		for ( j = 0; j < f->nrec; j++ ) {
			BPKREC	*r = &f->rec[j];

			if ( pos + BPK_RECHEAD > a->plainlen ) {
				return fail(err, errmax, "書庫のレコードが途中で切れています", E_OBJ);
			}
			r->type = (UH)rd16(a->plain + pos + RH_TYPE);
			r->sub = (UH)rd16(a->plain + pos + RH_SUBTYPE);
			r->size = rd32(a->plain + pos + RH_SIZE);
			pos += BPK_RECHEAD;
			if ( r->size > a->plainlen - pos ) {
				return fail(err, errmax, "書庫のレコードが途中で切れています", E_OBJ);
			}
			r->data = a->plain + pos;
			pos += r->size;
			if ( r->type == BPK_RT_LINK ) {
				INT	id = ( r->size >= BPK_LINKREC ) ? (INT)rd16(r->data + LK_F_ID) : -1;

				f->link[f->nlink++] = ( id >= 0 && id < (INT)a->nfiles ) ? id : -1;
			} else if ( r->type == BPK_RT_TAD ) {
				f->ntad++;
			} else if ( r->type == BPK_RT_EXEC ) {
				read_exec(f, r->data, r->size);
			}
		}
	}
	a->root = 0;
	read_root(a);
	return E_OK;
}

EXPORT void bpk_free( BPKARC *a )
{
	UINT	i;

	if ( a->file != NULL ) {
		for ( i = 0; i < a->nfiles; i++ ) {
			free(a->file[i].rec);
			free(a->file[i].link);
			free(a->file[i].exec);
		}
		free(a->file);
	}
	free(a->plain);
	memset(a, 0, sizeof(*a));
}
/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	bpk_tron.c
 *	TRON characters and Unicode, both ways
 *
 *	A TRON character is 16 bits in a plane; FE21 and on switch the plane
 *	(FE21 to plane 1). Each plane has four zones of two-byte codes: A
 *	(both bytes 21-7E), B (the first 80-FD), C (the second 80-FD) and D
 *	(both 80-FD). Plane 1 is the system script: zone A is JIS X 0208,
 *	except row 3's letters, which are ASCII, with JIS X 0213's first
 *	plane in its gaps; zone B holds JIS X 0213's second plane (first byte
 *	87..A0) and JIS X 0212 (A1..ED); zone C GB 2312 and zone D KS X 1001
 *	(from B7), each of these 94 by 94 laid 126 to a first byte. Planes 16
 *	and 17 lay out the Unicode BMP in zone order, so any character of the
 *	BMP has a TRON code. The other planes -- the GT fonts, 大漢和 and the
 *	rest -- have no Unicode and read as 〓; so does a place of plane 1 no
 *	table fills.
 *
 *	The tables come from tools/gen_jis.py bpk. A place that holds a
 *	character past the BMP, or a letter and a combining mark, holds
 *	0xD800 + n, n indexing bpk_tron_multi.
 */

#include <btron/bpk.h>
#include <btron/troncode.h>
#include <string.h>

#define PLANE_BMP	16		/* planes 16 and 17: the BMP, 44032 characters each */
#define PLANE_SPAN	44032
#define ZONE_PACK	( 94 * 94 )	/* the places of a set laid 126 to a first byte */

EXPORT INT bpk_utf8( UINT cp, char *o )
{
	if ( cp < 0x80 ) {
		o[0] = (char)cp;
		return 1;
	}
	if ( cp < 0x800 ) {
		o[0] = (char)( 0xC0 | ( cp >> 6 ) );
		o[1] = (char)( 0x80 | ( cp & 0x3F ) );
		return 2;
	}
	if ( cp < 0x10000 ) {
		o[0] = (char)( 0xE0 | ( cp >> 12 ) );
		o[1] = (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
		o[2] = (char)( 0x80 | ( cp & 0x3F ) );
		return 3;
	}
	o[0] = (char)( 0xF0 | ( cp >> 18 ) );
	o[1] = (char)( 0x80 | ( ( cp >> 12 ) & 0x3F ) );
	o[2] = (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
	o[3] = (char)( 0x80 | ( cp & 0x3F ) );
	return 4;
}

/* A two-byte code as its place in its plane, -1 when it is in no zone */
LOCAL INT zone_linear( UINT hi, UINT lo )
{
	if ( hi >= 0x21 && hi <= 0x7E && lo >= 0x21 && lo <= 0x7E ) return (INT)( ( hi - 0x21 ) * 94 + ( lo - 0x21 ) );
	if ( hi >= 0x80 && hi <= 0xFD && lo >= 0x21 && lo <= 0x7E ) return (INT)( 8836 + ( hi - 0x80 ) * 94 + ( lo - 0x21 ) );
	if ( hi >= 0x21 && hi <= 0x7E && lo >= 0x80 && lo <= 0xFD ) return (INT)( 20680 + ( hi - 0x21 ) * 126 + ( lo - 0x80 ) );
	if ( hi >= 0x80 && hi <= 0xFD && lo >= 0x80 && lo <= 0xFD ) return (INT)( 32524 + ( hi - 0x80 ) * 126 + ( lo - 0x80 ) );
	return -1;
}

/* A place in a plane as its two-byte code */
LOCAL UINT zone_code( INT lin )
{
	if ( lin < 8836 ) return ( (UINT)( 0x21 + lin / 94 ) << 8 ) | (UINT)( 0x21 + lin % 94 );
	lin -= 8836;
	if ( lin < 11844 ) return ( (UINT)( 0x80 + lin / 94 ) << 8 ) | (UINT)( 0x21 + lin % 94 );
	lin -= 11844;
	if ( lin < 11844 ) return ( (UINT)( 0x21 + lin / 126 ) << 8 ) | (UINT)( 0x80 + lin % 126 );
	lin -= 11844;
	return ( (UINT)( 0x80 + lin / 126 ) << 8 ) | (UINT)( 0x80 + lin % 126 );
}

/* A character of plane 1 */
LOCAL INT plane1( UINT hi, UINT lo, UINT out[2] )
{
	if ( hi == 0x23 && lo >= 0x20 && lo <= 0x7E ) {
		out[0] = lo;
		return 1;
	}
	char u8[8];
	int n = tc_to_utf8((TC)((hi << 8) | lo), u8, sizeof(u8));
	if ( n > 0 ) {
		UINT cp = 0;
		if ( n == 1 ) cp = (UB)u8[0];
		else if ( n == 2 ) cp = (((UB)u8[0] & 0x1F) << 6) | ((UB)u8[1] & 0x3F);
		else if ( n == 3 ) cp = (((UB)u8[0] & 0x0F) << 12) | (((UB)u8[1] & 0x3F) << 6) | ((UB)u8[2] & 0x3F);
		else if ( n == 4 ) cp = (((UB)u8[0] & 0x07) << 18) | (((UB)u8[1] & 0x3F) << 12) | (((UB)u8[2] & 0x3F) << 6) | ((UB)u8[3] & 0x3F);
		out[0] = cp;
		out[1] = 0;
		return 1;
	}
	if ( zone_linear(hi, lo) >= 0 ) {
		out[0] = UC_GETA;		/* 点字 and the places no set fills */
		return BPK_TRON_NOUCS;
	}
	return 0;
}

EXPORT INT bpk_tron_chars( UINT ch, INT *p_plane, UINT out[2] )
{
	UINT	hi = ( ch >> 8 ) & 0xFF, lo = ch & 0xFF;
	INT	lin;

	out[0] = out[1] = 0;
	if ( ( ch >= 0xFE21 && ch <= 0xFE7E ) || ( ch >= 0xFE80 && ch <= 0xFEFE ) ) {
		*p_plane = (INT)( ch - 0xFE21 + 1 );
		return 0;
	}
	if ( ch == TC_TAB || ch == TC_NL || ch == TC_CR ) {
		out[0] = ch;
		return 1;
	}
	if ( *p_plane == PLANE_BMP || *p_plane == PLANE_BMP + 1 ) {
		lin = zone_linear(hi, lo);
		if ( lin >= 0 && ( *p_plane - PLANE_BMP ) * PLANE_SPAN + lin <= 0xFFFF ) {
			out[0] = (UINT)( ( *p_plane - PLANE_BMP ) * PLANE_SPAN + lin );
			return 1;
		}
		if ( lin < 0 ) return 0;
		out[0] = UC_GETA;
		return BPK_TRON_NOUCS;
	}
	if ( *p_plane == 1 ) return plane1(hi, lo, out);
	if ( zone_linear(hi, lo) < 0 ) return 0;
	out[0] = UC_GETA;
	return BPK_TRON_NOUCS;
}

EXPORT UINT bpk_tron_ucs( UINT ch, INT *p_plane )
{
	UINT	u[2];

	return ( bpk_tron_chars(ch, p_plane, u) != 0 ) ? u[0] : 0;
}

/* The row and cell (row << 8 | cell) of a character in zone A, 0 none */
LOCAL UINT jis_of( UINT cp )
{
	char u8[8];
	int len = bpk_utf8(cp, u8);
	if ( len > 0 ) {
		int consumed = 0;
		TC tc = utf8_to_tc(u8, &consumed);
		if ( tc != 0 && tc != '?' ) {
			UINT hi = ( tc >> 8 ) & 0xFF;
			UINT lo = tc & 0xFF;
			if ( hi >= 0x21 && hi <= 0x7E && lo >= 0x21 && lo <= 0x7E ) {
				return (( hi - 0x20 ) << 8) | ( lo - 0x20 );
			}
		}
	}
	return 0;
}

/* The plane 1 code of a character zone A does not hold, 0 none */
LOCAL UINT other_of( UINT cp )
{
	char u8[8];
	int len = bpk_utf8(cp, u8);
	if ( len > 0 ) {
		int consumed = 0;
		TC tc = utf8_to_tc(u8, &consumed);
		if ( tc != 0 && tc != '?' ) {
			return (UINT)tc;
		}
	}
	return 0;
}

/* A code written in a plane, the plane switch before it when it has to be */
LOCAL INT put_code( INT plane, UINT code, UH *out, INT max, INT *p_plane )
{
	INT	n = 0;

	if ( plane != *p_plane ) {
		if ( max < 2 ) return 0;
		out[n++] = (UH)( 0xFE21 + plane - 1 );
		*p_plane = plane;
	} else if ( max < 1 ) {
		return 0;
	}
	out[n++] = (UH)code;
	return n;
}

EXPORT INT bpk_ucs_tron( UINT cp, UH *out, INT max, INT *p_plane )
{
	UINT	code, rc;

	if ( cp == TC_TAB || cp == TC_NL || cp == TC_CR ) {
		if ( max < 1 ) return 0;
		out[0] = (UH)cp;
		return 1;
	}
	/* plane 1 when it has the character: ASCII in row 3, the rest by the tables */
	if ( cp >= 0x20 && cp <= 0x7E ) return put_code(1, 0x2300 | cp, out, max, p_plane);
	/* row 3 of plane 1 reads as ASCII: what the tables put there goes elsewhere */
	if ( cp <= 0xFFFF && ( rc = jis_of(cp) ) != 0 && ( rc >> 8 ) != 3 ) {
		code = ( ( ( rc >> 8 ) + 0x20 ) << 8 ) | ( ( rc & 0xFF ) + 0x20 );
		return put_code(1, code, out, max, p_plane);
	}
	if ( ( code = other_of(cp) ) != 0 && ( code >> 8 ) != 0x23 ) return put_code(1, code, out, max, p_plane);
	if ( cp <= 0xFFFF ) return put_code(PLANE_BMP + (INT)( cp / PLANE_SPAN ), zone_code((INT)( cp % PLANE_SPAN )), out, max, p_plane);
	return put_code(1, TC_GETA, out, max, p_plane);
}

EXPORT void bpk_tron_utf8( const UH *s, INT n, char *out, INT max )
{
	INT	i, k = 0, plane = 1, c, j;

	for ( i = 0; i < n && s[i] != 0; i++ ) {
		UINT	u[2];
		char	o[8];
		INT	m = 0;

		c = bpk_tron_chars(s[i], &plane, u);
		if ( c == BPK_TRON_NOUCS ) c = 1;
		for ( j = 0; j < c; j++ ) {
			if ( u[j] >= 0x20 ) m += bpk_utf8(u[j], o + m);
		}
		if ( k + m >= max ) break;
		memcpy(out + k, o, (size_t)m);
		k += m;
	}
	out[k] = 0;
}

/* One character out of UTF-8; its length, 0 at the end, 1 for a stray byte */
LOCAL INT utf8_next( const UB *s, UINT *p_cp )
{
	UINT	c = s[0];
	INT	n, i;

	if ( c == 0 ) return 0;
	if ( c < 0x80 ) {
		*p_cp = c;
		return 1;
	}
	n = ( c >= 0xF0 ) ? 4 : ( c >= 0xE0 ) ? 3 : ( c >= 0xC0 ) ? 2 : 0;
	if ( n == 0 ) {
		*p_cp = UC_GETA;
		return 1;
	}
	*p_cp = c & ( 0x3F >> ( n - 1 ) );
	for ( i = 1; i < n; i++ ) {
		if ( ( s[i] & 0xC0 ) != 0x80 ) {
			*p_cp = UC_GETA;
			return i;
		}
		*p_cp = ( *p_cp << 6 ) | ( s[i] & 0x3F );
	}
	return n;
}

/* The code of a letter and the mark after it that have a place together, 0 none */
LOCAL UINT seq_of( UINT a, UINT b )
{
	INT	i;

	for ( i = 0; i < bpk_tron_nseq; i++ ) {
		if ( bpk_tron_seq[i][0] == a && bpk_tron_seq[i][1] == b ) return bpk_tron_seq[i][2];
	}
	return 0;
}

EXPORT INT bpk_utf8_tron( const char *s, UH *out, INT max )
{
	const UB	*p = (const UB *)s;
	INT		k = 0, plane = 1, m, d, d2;
	UINT		cp = 0, next = 0, code;

	while ( ( d = utf8_next(p, &cp) ) > 0 ) {
		d2 = utf8_next(p + d, &next);
		if ( d2 > 0 && ( code = seq_of(cp, next) ) != 0 ) {
			m = put_code(1, code, out + k, max - k, &plane);
			d += d2;
		} else {
			m = bpk_ucs_tron(cp, out + k, max - k, &plane);
		}
		if ( m == 0 ) break;
		k += m;
		p += d;
	}
	for ( m = k; m < max; m++ ) out[m] = 0;
	return k;
}
/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	bpk_util.c
 *	Text that grows, and colours
 */

#include <btron/bpk.h>
#include <btron/troncode.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- buffers */

EXPORT void bpk_buf_init( BPKBUF *b )
{
	b->s = NULL;
	b->n = b->max = 0;
	b->fail = FALSE;
}

EXPORT void bpk_buf_free( BPKBUF *b )
{
	free(b->s);
	bpk_buf_init(b);
}

/* Room for more bytes and the 0 after them */
LOCAL BOOL room( BPKBUF *b, INT more )
{
	INT	want = b->n + more + 1, m;
	char	*s;

	if ( b->fail ) return FALSE;
	if ( want <= b->max ) return TRUE;
	m = ( b->max < 256 ) ? 256 : b->max;
	while ( m < want ) m *= 2;
	s = realloc(b->s, (size_t)m);
	if ( s == NULL ) {
		b->fail = TRUE;
		return FALSE;
	}
	b->s = s;
	b->max = m;
	return TRUE;
}

EXPORT void bpk_buf_putn( BPKBUF *b, const char *s, INT n )
{
	if ( n <= 0 || !room(b, n) ) return;
	memcpy(b->s + b->n, s, (size_t)n);
	b->n += n;
	b->s[b->n] = 0;
}

EXPORT void bpk_buf_puts( BPKBUF *b, const char *s )
{
	bpk_buf_putn(b, s, (INT)strlen(s));
}

EXPORT void bpk_buf_putc( BPKBUF *b, char c )
{
	bpk_buf_putn(b, &c, 1);
}

EXPORT void bpk_buf_printf( BPKBUF *b, const char *fmt, ... )
{
	va_list	ap;
	char	tmp[256];
	INT	n;

	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if ( n < 0 ) return;
	if ( n < (INT)sizeof(tmp) ) {
		bpk_buf_putn(b, tmp, n);
		return;
	}
	if ( !room(b, n) ) return;
	va_start(ap, fmt);
	(void)vsnprintf(b->s + b->n, (size_t)n + 1, fmt, ap);
	va_end(ap);
	b->n += n;
}

EXPORT void bpk_buf_xml( BPKBUF *b, const char *s )
{
	for ( ; *s != 0; s++ ) {
		switch ( *s ) {
		case '&':	bpk_buf_puts(b, "&amp;");	break;
		case '<':	bpk_buf_puts(b, "&lt;");	break;
		case '>':	bpk_buf_puts(b, "&gt;");	break;
		case '"':	bpk_buf_puts(b, "&quot;");	break;
		default:
			/* the controls XML does not allow are left out */
			if ( (UB)*s >= 0x20 || *s == '\t' || *s == '\n' ) bpk_buf_putc(b, *s);
			break;
		}
	}
}

EXPORT void bpk_buf_json( BPKBUF *b, const char *s )
{
	for ( ; *s != 0; s++ ) {
		if ( *s == '"' || *s == '\\' ) {
			bpk_buf_putc(b, '\\');
			bpk_buf_putc(b, *s);
		} else if ( (UB)*s >= 0x20 ) {
			bpk_buf_putc(b, *s);
		}
	}
}

EXPORT void bpk_buf_cp( BPKBUF *b, UINT cp )
{
	char	o[4];

	bpk_buf_putn(b, o, bpk_utf8(cp, o));
}

/* A value in thousandths as a decimal: 19200 is "19.2", 12000 "12" */
EXPORT void bpk_buf_num( BPKBUF *b, INT milli )
{
	INT	frac;

	if ( milli < 0 ) {
		bpk_buf_putc(b, '-');
		milli = -milli;
	}
	bpk_buf_printf(b, "%d", milli / 1000);
	frac = milli % 1000;
	if ( frac == 0 ) return;
	if ( frac % 100 == 0 ) bpk_buf_printf(b, ".%d", frac / 100);
	else if ( frac % 10 == 0 ) bpk_buf_printf(b, ".%02d", frac / 10);
	else bpk_buf_printf(b, ".%03d", frac);
}

/* ---------------------------------------------------------------- colours */

/* The system's standard colour map, for a map index a record does not define */
LOCAL const UINT std_cmap[16] = {
	0xFFFFFF, 0x00009F, 0x00EF00, 0x00EFEF, 0xFF0000, 0xEF00FF, 0xDFDF00, 0x7F7F9F,
	0xDFDFDF, 0x7F9FFF, 0xBFFFAF, 0xCFFFFF, 0xFF6F6F, 0xEF8FFF, 0xEFFF9F, 0x000000
};

/*
 * A COLOR: bit 31 transparent, bits 28-30 the mode. Mode 1 is RGB in the
 * low 24 bits; mode 0 an index into the record's colour map, which holds
 * COLORs of its own.
 */
EXPORT void bpk_colour( UINT raw, const UINT *cmap, INT ncmap, char out[8] )
{
	UINT	mode = ( raw >> 28 ) & 7, rgb = 0;

	if ( mode == 1 ) {
		rgb = raw & 0xFFFFFF;
	} else if ( mode == 0 ) {
		UINT	i = raw & 0x0FFFFFFF;

		if ( cmap != NULL && i < (UINT)ncmap && ( ( cmap[i] >> 28 ) & 7 ) == 1 ) rgb = cmap[i] & 0xFFFFFF;
		else if ( i < 16 ) rgb = std_cmap[i];
	}
	snprintf(out, 8, "#%06x", rgb & 0xFFFFFF);
}
/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	bpk_jis.c
 *	The character sets of TRON code's first plane, made by tools/gen_jis.py bpk
 */

#include <btron/bpk.h>
#include <btron/troncode.h>


/* the two-character places of zone A: the letter, the mark and the code */
const UINT bpk_tron_seq[25][3] = {
	{ 0x304B, 0x309A, 0x2477 },
	{ 0x304D, 0x309A, 0x2478 },
	{ 0x304F, 0x309A, 0x2479 },
	{ 0x3051, 0x309A, 0x247A },
	{ 0x3053, 0x309A, 0x247B },
	{ 0x30AB, 0x309A, 0x2577 },
	{ 0x30AD, 0x309A, 0x2578 },
	{ 0x30AF, 0x309A, 0x2579 },
	{ 0x30B1, 0x309A, 0x257A },
	{ 0x30B3, 0x309A, 0x257B },
	{ 0x30BB, 0x309A, 0x257C },
	{ 0x30C4, 0x309A, 0x257D },
	{ 0x30C8, 0x309A, 0x257E },
	{ 0x31F7, 0x309A, 0x2678 },
	{ 0x00E6, 0x0300, 0x2B44 },
	{ 0x0254, 0x0300, 0x2B48 },
	{ 0x0254, 0x0301, 0x2B49 },
	{ 0x028C, 0x0300, 0x2B4A },
	{ 0x028C, 0x0301, 0x2B4B },
	{ 0x0259, 0x0300, 0x2B4C },
	{ 0x0259, 0x0301, 0x2B4D },
	{ 0x025A, 0x0300, 0x2B4E },
	{ 0x025A, 0x0301, 0x2B4F },
	{ 0x02E9, 0x02E5, 0x2B65 },
	{ 0x02E5, 0x02E9, 0x2B66 },
};

const INT bpk_tron_nseq = 25;
