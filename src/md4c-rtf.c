/*
 * MD4C-RTF: RTF Renderer for MD4C parser
 *
 * Copyright (c) 2022 Eric M.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * ----------------------------------------------------------------------------
 *
 * This program is not standalone but extension of the MD4C Markdown parser.
 * To successfully compile this program you'll need:
 *
 *  - The MD4C headers files.
 *  - The MD4C source files to be compiled.
 *     OR
 *  - The MD4 compiled version as linked library.
 *
 * To get MD4C please visit MD4C github page:
 *    http://github.com/mity/md4c
 */
#include <stdio.h>
#include <stdlib.h>     /* strtoul */
#include <string.h>

#include "md4c-rtf.h"
#include "entity.h"

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199409L
    /* C89/90 or old compilers in general may not understand "inline". */
    #if defined __GNUC__
        #define inline __inline__
    #elif defined _MSC_VER
        #define inline __inline
    #else
        #define inline
    #endif
#endif

#ifdef _WIN32
    #define snprintf _snprintf
#endif

/* Implementation for long unsigned number conversion to string

Converts an long unsigned integer value to a null-terminated string
using the specified base and stores the result in the array given
by str parameter. */
static char*
ultostr(long unsigned value, char *str, int base)
{
  char buf[32];
  char *pb = buf;
  char *ps = str;
  int d;

  while(value || pb == buf)  {
    d = value % base;
    *pb++ = (d < 10) ? d+'0' : d+'a'-10;
    value /= base;
  }

  /* copy buffer to destination in reverse order */

  while(pb > buf)
    *ps++ = *--pb;

  *ps++ = '\0';

  return str;
}


typedef struct MD_RTF_list {
  unsigned        type;
  unsigned        count;
  unsigned        start;
  const MD_CHAR*  cw_tx;
  const MD_CHAR*  cw_li;
  const MD_CHAR*  cw_sa;
} MD_RTF_LIST;

#define MD_RTF_LIST_TYPE_UL   0x0
#define MD_RTF_LIST_TYPE_OL   0x1

static const MD_CHAR* g_cw_list_bullt[2] = {
  "\\u8226 ",
  "\\u9702 "};

static const MD_CHAR* g_cw_list_delim[2] = {
  ".",
  ")"};

typedef struct MD_RTF_tag {
  void (*process_output)(const MD_CHAR*, MD_SIZE, void*);
  void* userdata;
  unsigned flags;
  char escape_map[256];
  /* RTF document page sizes (twip) */
  unsigned    page_width;
  unsigned    page_height;
  unsigned    page_margin;
  /* Normal font base size (half-point) */
  unsigned    font_base;
  /* list render process variables */
  MD_RTF_LIST list[8];
  unsigned    list_depth;
  unsigned    list_stop;
  unsigned    list_reset;
  /* table render process variables */
  unsigned    tabl_cols;
  unsigned    tabl_head;
  /* do not end paragraph flag */
  unsigned    no_p_end;
  /* RTF control words with prebuilt values */
  MD_CHAR     cw_fs[2][8];
  MD_CHAR     cw_hf[6][16];
  MD_CHAR     cw_sa[3][16];
  MD_CHAR     cw_li[8][16];
  MD_CHAR     cw_tr[2][32];
  MD_CHAR     cw_fi[2][16];
  MD_CHAR     cw_cx[2][16];
} MD_RTF;

#define NEED_RTF_ESC_FLAG   0x1
#define NEED_URL_ESC_FLAG   0x2
#define NEED_PRE_ESC_FLAG   0x4

/******************************************
 ***   RTF rendering helper functions   ***
 ******************************************/

#define ISDIGIT(ch)     ('0' <= (ch) && (ch) <= '9')
#define ISLOWER(ch)     ('a' <= (ch) && (ch) <= 'z')
#define ISUPPER(ch)     ('A' <= (ch) && (ch) <= 'Z')
#define ISALNUM(ch)     (ISLOWER(ch) || ISUPPER(ch) || ISDIGIT(ch))

static inline void
render_verbatim(MD_RTF* r, const MD_CHAR* text, MD_SIZE size)
{
  r->process_output(text, size, r->userdata);
}

/* Keep this as a macro. Most compiler should then be smart enough to replace
 * the strlen() call with a compile-time constant if the string is a C literal. */
#define RENDER_VERBATIM(r, verbatim)                                    \
        render_verbatim((r), (verbatim), (MD_SIZE) (strlen(verbatim)))


static void
render_url_escaped(MD_RTF* r, const MD_CHAR* data, MD_SIZE size)
{
  static const MD_CHAR hex_chars[] = "0123456789ABCDEF";
  MD_OFFSET beg = 0;
  MD_OFFSET off = 0;

  /* Some characters need to be escaped in URL attributes. */
  #define NEED_URL_ESC(ch)    (r->escape_map[(unsigned char)(ch)] & NEED_URL_ESC_FLAG)

  while(1) {

    while(off < size  &&  !NEED_URL_ESC(data[off]))
      off++;

    if(off > beg)
      render_verbatim(r, data + beg, off - beg);

    if(off < size) {

      char hex[3];

      switch(data[off])
      {
      case '&':
        RENDER_VERBATIM(r, "&amp;");
        break;
      default:
        hex[0] = '%';
        hex[1] = hex_chars[((unsigned)data[off] >> 4) & 0xf];
        hex[2] = hex_chars[((unsigned)data[off] >> 0) & 0xf];
        render_verbatim(r, hex, 3);
        break;
      }

      off++;

    } else {
      break;
    }

    beg = off;
  }
}

static inline void
render_unicode(MD_RTF* r, unsigned u)
{
  MD_CHAR str_ucp[16];
  RENDER_VERBATIM(r, "\\u");
  RENDER_VERBATIM(r, ultostr(u, str_ucp, 10));
}


static inline void
render_cp1252(MD_RTF* r, unsigned u)
{
  MD_CHAR str_acp[16];
  RENDER_VERBATIM(r, "\\'");
  RENDER_VERBATIM(r, ultostr(u, str_acp, 16));
}


/* Translate entity to its UTF-8 equivalent, or output the verbatim one
 * if such entity is unknown (or if the translation is disabled). */
static void
render_entity(MD_RTF* r, const MD_CHAR* text, MD_SIZE size,
              void (*fn_append)(MD_RTF*, const MD_CHAR*, MD_SIZE))
{
  if(r->flags & MD_RTF_FLAG_VERBATIM_ENTITIES) {
    render_verbatim(r, text, size);
    return;
  }

  /* We assume Unicode output is what is desired. */
  if(size > 3 && text[1] == '#') {

    unsigned u = 0;

    if(text[2] == 'x' || text[2] == 'X') {
      u = strtoul(text + 3, NULL, 16);
    } else {
      u = strtoul(text + 2, NULL, 10);
    }

    render_unicode(r, u);

    return;

  } else {
    #ifdef MD4C_ENTITY_H

    /* Named entity (e.g. "&nbsp;"). */
    /*
    const struct entity* ent;

    ent = entity_lookup(text, size);
    if(ent != NULL) {
      render_utf8_codepoint(r, ent->codepoints[0], fn_append);
      if(ent->codepoints[1])
        render_utf8_codepoint(r, ent->codepoints[1], fn_append);
      return;
    }
    */
    #endif
  }

  fn_append(r, text, size);
}


static unsigned
render_non_ascii(MD_RTF* r, const unsigned char* s, unsigned size)
{
  unsigned u = 0;
  unsigned b = 0;

  #define IS_UTF8_2BYTES(a) (((a)[1] & 0xC0) == 0x80)
  #define IS_UTF8_3BYTES(a) (((a)[1] & 0xC0) == 0x80) && \
                            (((a)[2] & 0xC0) == 0x80)
  #define IS_UTF8_4BYTES(a) (((a)[1] & 0xC0) == 0x80) && \
                            (((a)[2] & 0xC0) == 0x80) && \
                            (((a)[3] & 0xC0) == 0x80) && \

  /* validate and decode UTF-8 */
  if((s[0] & 0xE0) == 0xC0) { /* 110X XXXX : 2 octets */
    if(size > 1) { /* need one more octet */
      if(IS_UTF8_2BYTES(s)) {
        u = (s[0] & 0x1F) <<  6 |
            (s[1] & 0x3F);
        b = 2; /* skip 2 byte */
      }
    }
  } else if((s[0] & 0xF0) == 0xE0) { /* 1110 XXXX : 3 octets */
    if(size > 2) { /* need 2 more octets */
      if(IS_UTF8_3BYTES(s)) {
        u = (s[0] & 0x0F) << 12 |
            (s[1] & 0x3F) <<  6 |
            (s[2] & 0x3F);
        b = 3; /* skip 3 byte */
      }
    }
  } else if((s[0] & 0xF8) == 0xF0) { /* 1111 0XXX : 4 octets */
    if(size > 3) { /* need 3 more octets */
      if(IS_UTF8_2BYTES(s)) {
        u = (s[0] & 0x07) << 18 |
            (s[1] & 0x3F) << 12 |
            (s[2] & 0x3F) <<  6 |
            (s[3] & 0x3F);
        b = 4; /* skip 4 byte */
      }
    }
  }

  /* check if we got a valid Unicode codepoint */
  if(b > 0) {

    render_unicode(r, u);

  } else {

    /* if we don't got a valid Unicode codepoint we assume an AINSI CP1252
    encoding. We translate it to RTF using old standard escaping for 8-bit
    non ASCII characters. */
    render_cp1252(r, *s);

    return 1; /* skip 1 byte */
  }

  return b;
}

static void
render_rtf_escaped(MD_RTF* r, const MD_CHAR* data, MD_SIZE size)
{
  MD_OFFSET beg = 0;
  MD_OFFSET off = 0;

  /* Some characters need to be escaped in normal RTF text. */
  #define NEED_RTF_ESC(ch)   (r->escape_map[(unsigned char)(ch)] & NEED_RTF_ESC_FLAG)

  while(1) {
    /* Optimization: Use some loop unrolling. */
    while(off + 3 < size  &&  !NEED_RTF_ESC(data[off+0])  &&  !NEED_RTF_ESC(data[off+1])
                          &&  !NEED_RTF_ESC(data[off+2])  &&  !NEED_RTF_ESC(data[off+3]))
      off += 4;

    while((off < size) && !NEED_RTF_ESC(data[off]))
      off++;

    if(off > beg)
      render_verbatim(r, data + beg, off - beg);

    if(off < size) {

      // escape Non-ASCII characters
      if((unsigned char)data[off] > 0x7F) {
        off += render_non_ascii(r, (unsigned char*)(data + off), size - off);
      } else {
        // escape RTF reserved characters
        switch(data[off]) {
          case '\\': RENDER_VERBATIM(r, "\\\\"); break;
          case '{' : RENDER_VERBATIM(r, "\\{"); break;
          case '}' : RENDER_VERBATIM(r, "\\}"); break;
          case '\n': RENDER_VERBATIM(r, "\\line1"); break;
        }
        off++;
      }

    } else {
      break;
    }

    beg = off;
  }
}


static void
render_font_norm(MD_RTF* r)
{
  /* change font style with the normal font (#0 : Calibri)
  with its dedicated size, little smaller than the normal */

  RENDER_VERBATIM(r, "\\f0");
  RENDER_VERBATIM(r, r->cw_fs[0]);
}

static void
render_font_mono(MD_RTF* r)
{
  /* change paragraph style to monospace font (#1 :. Courrier New )
  with its dedicated size, little smaller than the normal */

  RENDER_VERBATIM(r, "\\f1");
  RENDER_VERBATIM(r, r->cw_fs[1]);
}

inline static void
render_end_block(MD_RTF* r)
{
  /* this sequence is used to put standard paragraph space after a special
  block such as list, quote or code, where space-after value is usually
  changed or disabled. */

  /* reset with zero-sized font */
  RENDER_VERBATIM(r, "\\pard\\f0\\fs0");
  /* paragraph space-after value */
  RENDER_VERBATIM(r, r->cw_sa[2]);
  /* end paragraph, notice that CRLF is here
  only for readability of source data */
  RENDER_VERBATIM(r, "\\par\r\n");
}

inline static void
render_list_start(MD_RTF* r)
{
  MD_CHAR str_num[16];

  unsigned d = r->list_depth;

  RENDER_VERBATIM(r, "\\pard"); /* reset paragraph */
  RENDER_VERBATIM(r, r->cw_fs[0]);  /* normal font size */
  RENDER_VERBATIM(r, "{\\pntext\\f0 ");  /* normal font size */

  if(r->list[d].type == MD_RTF_LIST_TYPE_OL) {  /* OL */

    /* we need to the start number added to item count because
    this can be a list restart after the end of nested list */
    ultostr(r->list[d].count + r->list[d].start, str_num, 10);

    RENDER_VERBATIM(r, str_num);
    RENDER_VERBATIM(r, r->list[d].cw_tx); /* bullet character */
    RENDER_VERBATIM(r, "\\tab}{\\*\\pn\\pnlvlbody\\pnf0\\pnstart");
    RENDER_VERBATIM(r, str_num);
    RENDER_VERBATIM(r, "\\pndec{\\pntxta");
  } else {                                      /* UL */
    RENDER_VERBATIM(r, r->list[d].cw_tx); /* bullet character */
    RENDER_VERBATIM(r, "\\tab}{\\*\\pn\\pnlvlblt\\pnf0{\\pntxtb");
  }

  RENDER_VERBATIM(r, r->list[d].cw_tx); /* bullet character */
  RENDER_VERBATIM(r, "}}");
  RENDER_VERBATIM(r, r->list[d].cw_li); /* \liN */
  RENDER_VERBATIM(r, r->list[d].cw_sa); /* \saN */

  if(r->list[d].type == MD_RTF_LIST_TYPE_OL) {  /* OL */
    RENDER_VERBATIM(r, r->cw_fi[1]); /* \fiN */
  } else {                                      /* UL */
    RENDER_VERBATIM(r, r->cw_fi[0]); /* \fiN */
  }

  r->list_reset = 0;
}

inline static void
render_list_item(MD_RTF* r)
{
  MD_CHAR str_num[16];

  unsigned d = r->list_depth;

  /* reset paragraph */
  RENDER_VERBATIM(r, "{\\pntext\\f0 ");

  if(r->list[d].type == MD_RTF_LIST_TYPE_OL) { /* OL */
    ultostr(r->list[d].count + r->list[d].start, str_num, 10);
    RENDER_VERBATIM(r, str_num);
  }

  RENDER_VERBATIM(r, r->list[d].cw_tx); /* either delimiter or bullet char */
  RENDER_VERBATIM(r, "\\tab}");
}

static void
render_enter_block_doc(MD_RTF* r)
{
  MD_CHAR str_page[512];

  RENDER_VERBATIM(r,  "{\\rtf1\\ansi\\ansicpg1252\\deff0"
                        /* font table */
                        "{\\fonttbl"
                          "{\\f0\\fswiss Calibri;}"
                          "{\\f1\\fmodern Courier New;}"
                          "{\\f2\\fnil Symbol;}"
                        "}"
                        /* color table */
                        "{\\colortbl;"
                          "\\red0\\green0\\blue0;"        /* black */
                          "\\red255\\green255\\blue255;"  /* white */
                          "\\red180\\green180\\blue180;"  /* gray */
                          "\\red0\\green102\\blue204;"    /* blue */
                          "\\red230\\green230\\blue230;"  /* silver */
                        "}"
                        /* additional informations */
                        "{\\*\\generator MD4C-RTF}");


                        /* document parameters */
  sprintf(str_page,     "\\paperw%u\\paperh%u"
                        "\\margl%u\\margr%u\\margt%u\\margb%u",
                        r->page_width, r->page_height,
                        r->page_margin, r->page_margin, r->page_margin, r->page_margin);

  RENDER_VERBATIM(r, str_page);

                        /* document initialization */
  RENDER_VERBATIM(r, "\\uc0\r\n\\pard");
  RENDER_VERBATIM(r, r->cw_sa[2]); /* default space-after */
}

static void
render_leave_block_doc(MD_RTF* r)
{
  RENDER_VERBATIM(r, "}\0");
}

static void
render_enter_block_hr(MD_RTF* r)
{
  /* MS Rich Edit 4.1 handle table border in a very specific way, not defined
  or invisible border are (forced) displayed in light gray.

  To make borders invisible even with MS Rich Edit 4.1 we need to define a
  minimum width of 1 (so it is considered visible) and to define a color the
  same as background */

  RENDER_VERBATIM(r, "\\pard\\fs0\\trowd\\trrh0\\trautofit1"
                      "\\clbrdrt\\brdrs\\brdrw20\\brdrcf2"
                      "\\clbrdrb\\brdrs\\brdrw1\\brdrcf3"   /* invisible border */
                      "\\clbrdrl\\brdrs\\brdrw1\\brdrcf2"   /* invisible border */
                      "\\clbrdrr\\brdrs\\brdrw1\\brdrcf2"); /* invisible border */


  RENDER_VERBATIM(r, r->cw_cx[1]); // \cellxN
  RENDER_VERBATIM(r, "\\par\\cell\\row");

  /* create proper space after paragraph */
  render_end_block(r);
}

static void
render_enter_block_h(MD_RTF* r, const MD_BLOCK_H_DETAIL* h)
{
  RENDER_VERBATIM(r, r->cw_hf[h->level - 1]);
}

static void
render_leave_block_h(MD_RTF* r, const MD_BLOCK_H_DETAIL* h)
{
  if(h->level > 3) {
    RENDER_VERBATIM(r, "\\b0\\i0 \\par\r\n");
  } else {
    RENDER_VERBATIM(r, "\\b0 \\par\r\n");
  }
}

static void
render_enter_block_quote(MD_RTF* r)
{
  /* reset paragraph to normal font style */
  RENDER_VERBATIM(r, "\\pard\\f0");
  RENDER_VERBATIM(r, r->cw_fs[0]);
  /* start table row with proper parameters */
  RENDER_VERBATIM(r, "\\trowd");
  RENDER_VERBATIM(r, r->cw_tr[0]);

  /* quote is enclosed in a table with only the left border visible */
  RENDER_VERBATIM(r,  "\\clbrdrt\\brdrs\\brdrw1\\brdrcf2"   /* invisible border */
                      "\\clbrdrb\\brdrs\\brdrw1\\brdrcf2"   /* invisible border */
                      "\\clbrdrl\\brdrs\\brdrw50\\brdrcf3"
                      "\\clbrdrr\\brdrs\\brdrw1\\brdrcf2"); /* invisible border */

  /* set cell width, unfortunately basic RTF viewer does not handle
  autofit so we must define static cell size according defined page width */
  RENDER_VERBATIM(r, r->cw_cx[0]); // \cellxN

  /* prevent to emit a \par at end of paragraph to avoid
  extra line feed within the table */
  r->no_p_end = 1;
}

static void
render_leave_block_quote(MD_RTF* r)
{
  RENDER_VERBATIM(r, "\\cell\\row");

  /* create proper space after paragraph */
  render_end_block(r);

  /* we can now emit \par at end of paragraph */
  r->no_p_end = 0;
}

static void
render_enter_block_code(MD_RTF* r)
{
  /* reset paragraph to monospace font style */
  RENDER_VERBATIM(r, "\\pard\\f1");
  RENDER_VERBATIM(r, r->cw_fs[1]);
  /* start table row with proper parameters */
  RENDER_VERBATIM(r, "\\trowd");
  RENDER_VERBATIM(r, r->cw_tr[0]);

  /* quote is enclosed in an invisible table */
  RENDER_VERBATIM(r,  "\\clbrdrt\\brdrs\\brdrw1\\brdrcf2"   /* invisible border */
                      "\\clbrdrb\\brdrs\\brdrw1\\brdrcf2"   /* invisible border */
                      "\\clbrdrl\\brdrs\\brdrw1\\brdrcf2"
                      "\\clbrdrr\\brdrs\\brdrw1\\brdrcf2"); /* invisible border */

  /* set cell width, unfortunately basic RTF viewer does not handle
  autofit so we must define static cell size according defined page width */
  RENDER_VERBATIM(r, r->cw_cx[0]); // \cellxN

  /* prevent to emit a \par at end of paragraph to avoid
  extra line feed within the table */
  r->no_p_end = 1;
}

static void
render_leave_block_code(MD_RTF* r)
{
  /* space after block is always done due to last \n of its
  content we simply switch back to default font */
  RENDER_VERBATIM(r, "\\cell\\row\\pard\\f0");
  /* set proper space-after parameter for next paragraphs */
  RENDER_VERBATIM(r, r->cw_sa[2]);

  /* we can now emit \par at end of paragraph */
  r->no_p_end = 0;
}

static void
render_enter_block_ul(MD_RTF* r, const MD_BLOCK_UL_DETAIL* ul)
{
  /* increment depth */
  unsigned d = ++r->list_depth;

  /* nested list, we close the previous paragraph */
  if(d > 0) RENDER_VERBATIM(r, "\\par");

  /* do we need more than 8 levels of nested list ? */
  if(d > 7) return;

  /* initialize list parameters */
  r->list[d].type = MD_RTF_LIST_TYPE_UL;
  /* current item count */
  r->list[d].count = 0;
  /* bullet character */
  r->list[d].cw_tx = g_cw_list_bullt[d % 2];
  /* space-after \saN to use */
  r->list[d].cw_sa = ul->is_tight ? r->cw_sa[0]
                                  : r->cw_sa[1];
  /* left-indent \liN to use */
  r->list[d].cw_li = r->cw_li[d];

  /* start new list paragraph */
  render_list_start(r);
}

static void
render_leave_block_ul(MD_RTF* r)
{
  if(r->list_depth) {
    /* nested list ended, we must setup block again */
    r->list_reset = 1;
  } else {
    /* all ended, create proper space after paragraph */
    render_end_block(r);
  }

  /* decrement depth */
  r->list_depth--;
}

static void
render_enter_block_ol(MD_RTF* r, const MD_BLOCK_OL_DETAIL* ol)
{
  /* increment depth */
  unsigned d = ++r->list_depth;

  /* nested list, we close the previous paragraph */
  if(d > 0) RENDER_VERBATIM(r, "\\par");

  /* do we need more than 8 levels of nested list ? */
  if(d > 7) return;

  /* initialize list parameters */
  r->list[d].type = MD_RTF_LIST_TYPE_OL;
  /* current item count */
  r->list[d].count = 0;
  /* item start number */
  r->list[d].start = ol->start;
  /* delimiter character */
  r->list[d].cw_tx = (ol->mark_delimiter == ')')  ? g_cw_list_delim[1]
                                                  : g_cw_list_delim[0];
  /* space-after \saN to use */
  r->list[d].cw_sa = ol->is_tight ? r->cw_sa[0]
                                  : r->cw_sa[1];
  /* left-indent \liN to use */
  r->list[d].cw_li = r->cw_li[d];

  /* start new list paragraph */
  render_list_start(r);
}

static void
render_leave_block_ol(MD_RTF* r)
{
  if(r->list_depth) {
    /* nested list ended, we must setup block again */
    r->list_reset = 1;
  } else {
    /* all ended, create proper space after paragraph */
    render_end_block(r);
  }

  /* decrement depth */
  r->list_depth--;
}

static void
render_enter_block_li(MD_RTF* r, const MD_BLOCK_LI_DETAIL* li)
{
  /* if we just leave a nested list, we may need to start a new paragraph
  with proper parameters */
  if(r->list_reset)
    render_list_start(r);

  /* if this is the fist item of list, proper data is already
  written during pn block start */
  if(r->list[r->list_depth].count > 0)
    render_list_item(r);

  /* increment item count */
  r->list[r->list_depth].count++;

  /* item started must be stopped, see
  render_leave_block_li() for explanation */
  r->list_stop = 1;
}

static void
render_leave_block_li(MD_RTF* r)
{
  /* Unlike HTML, RTF do not work with opened blocks which must be closed but
  by ending a paragraph which automatically begins a new one, which produce
  a line feed.

  considering this behavior, ending a paragraph each time a block is closed
  would produce inconsistent line feeds. This is especially problematic with
  nested lists where we can have cascading LI blocks closures.

  To avoid this problem, we allow only one paragraph end after one or more
  LI blocks opening */

  if(r->list_stop) {

    /* end paragraph, line feed */
    RENDER_VERBATIM(r, "\\par");

    /* do not end another one before a new LI block opening */
    r->list_stop = 0;
  }
}

static void
render_enter_block_table(MD_RTF* r, const MD_BLOCK_TABLE_DETAIL* tb)
{
  /* we hold column count to calculate cell width */
  r->tabl_cols = tb->col_count;

  /* start new table with smaller font and horizontal align to center */
  RENDER_VERBATIM(r, "\\pard\\f0");
  RENDER_VERBATIM(r, r->cw_fs[1]);
}

inline static void
render_leave_block_table(MD_RTF* r)
{
  r->tabl_cols = 0;

  /* all ended, create proper space after paragraph */
  render_end_block(r);
}

inline static void
render_enter_block_thead(MD_RTF* r)
{
  /* simply set a flag to know we are in table head */
  r->tabl_head = 1;
}

inline static void
render_leave_block_thead(MD_RTF* r)
{
  /* we are no longer in table head */
  r->tabl_head = 0;
}

static void
render_enter_block_tr(MD_RTF* r)
{
  MD_CHAR str_num[16];

  /* create new raw with proper parameters */
  RENDER_VERBATIM(r, "\\trowd");
  RENDER_VERBATIM(r, r->cw_tr[1]);

  /* 9000 seem to be the average width of an RTF document */
  float tw = r->page_width - (2*r->page_margin) - (0.2f*r->page_width);
  unsigned cw = tw / r->tabl_cols;

  /* we must first declare cells with their respecting properties */
  for(unsigned i = 0; i < r->tabl_cols; ++i) {

    RENDER_VERBATIM(r,  "\\clvertalc" /* vertical-align center */
                        "\\clbrdrt\\brdrs\\brdrw20\\brdrcf3"
                        "\\clbrdrb\\brdrs\\brdrw20\\brdrcf3"
                        "\\clbrdrl\\brdrs\\brdrw20\\brdrcf3"
                        "\\clbrdrr\\brdrs\\brdrw20\\brdrcf3");

    /* if we render a table head, we add a background to cells */
    if(r->tabl_head) {
      RENDER_VERBATIM(r, "\\clcbpat5\\cellx");
      ultostr(cw * (i + 1), str_num, 10);
    } else {
      RENDER_VERBATIM(r, "\\cellx");
      ultostr(cw * (i + 1), str_num, 10);
    }

    RENDER_VERBATIM(r, str_num);
  }
}

inline static void
render_leave_block_tr(MD_RTF* r)
{
  /* close the row */
  RENDER_VERBATIM(r, "\\row\r\n");
}

static void
render_enter_block_td(MD_RTF* r, const MD_BLOCK_TD_DETAIL* td)
{
  switch(td->align) {
    case MD_ALIGN_CENTER: RENDER_VERBATIM(r, "\\qc "); break;
    case MD_ALIGN_RIGHT: RENDER_VERBATIM(r, "\\qr "); break;
    default: RENDER_VERBATIM(r, "\\ql "); break;
  }
}

inline static void
render_leave_block_td(MD_RTF* r)
{
  RENDER_VERBATIM(r, "\\intbl\\cell ");
}

static void
render_enter_block_th(MD_RTF* r, const MD_BLOCK_TD_DETAIL* td)
{
  /* same as td but with bold text */
  switch(td->align) {
    case MD_ALIGN_CENTER: RENDER_VERBATIM(r, "\\qc\\b "); break;
    case MD_ALIGN_RIGHT: RENDER_VERBATIM(r, "\\qr\\b "); break;
    default: RENDER_VERBATIM(r, "\\ql\\b "); break;
  }
}

inline static void
render_leave_block_th(MD_RTF* r)
{
  RENDER_VERBATIM(r, "\\b0\\intbl\\cell ");
}

inline static void
render_enter_block_p(MD_RTF* r)
{
  render_font_norm(r);
}

inline static void
render_leave_block_p(MD_RTF* r)
{
  if(!r->no_p_end)
    RENDER_VERBATIM(r, "\\par\r\n");
}

static void
render_enter_span_url(MD_RTF* r, const MD_SPAN_A_DETAIL* a)
{
  RENDER_VERBATIM(r, "\\cf4\\ul {\\field{\\*\\fldinst HYPERLINK \"");
  render_url_escaped(r, a->href.text, a->href.size);
  RENDER_VERBATIM(r, "\"}{\\fldrslt ");
}

inline static void
render_leave_span_url(MD_RTF* r)
{
  RENDER_VERBATIM(r, "}}\\ul0 \\cf0 ");
}



/***************************************
 ***   RTF renderer implementation   ***
 ***************************************/

static int
enter_block_callback(MD_BLOCKTYPE type, void* detail, void* userdata)
{
  MD_RTF* r = (MD_RTF*) userdata;

  switch(type) {
      case MD_BLOCK_DOC:      render_enter_block_doc(r); break;
      case MD_BLOCK_QUOTE:    render_enter_block_quote(r); break;
      case MD_BLOCK_UL:       render_enter_block_ul(r, (const MD_BLOCK_UL_DETAIL*)detail); break;
      case MD_BLOCK_OL:       render_enter_block_ol(r, (const MD_BLOCK_OL_DETAIL*)detail); break;
      case MD_BLOCK_LI:       render_enter_block_li(r, (const MD_BLOCK_LI_DETAIL*)detail); break;
      case MD_BLOCK_HR:       render_enter_block_hr(r); break;
      case MD_BLOCK_H:        render_enter_block_h(r, (const MD_BLOCK_H_DETAIL*)detail); break;
      case MD_BLOCK_CODE:     render_enter_block_code(r); break;
      case MD_BLOCK_P:        render_enter_block_p(r); break;
      case MD_BLOCK_HTML:     /* noop */ break;
      case MD_BLOCK_TABLE:    render_enter_block_table(r, (const MD_BLOCK_TABLE_DETAIL*)detail); break;
      case MD_BLOCK_THEAD:    render_enter_block_thead(r); break;
      case MD_BLOCK_TBODY:    /* noop */ break;
      case MD_BLOCK_TR:       render_enter_block_tr(r); break;
      case MD_BLOCK_TH:       render_enter_block_th(r,(const MD_BLOCK_TD_DETAIL*)detail); break;
      case MD_BLOCK_TD:       render_enter_block_td(r,(const MD_BLOCK_TD_DETAIL*)detail); break;
  }

  return 0;
}

static int
leave_block_callback(MD_BLOCKTYPE type, void* detail, void* userdata)
{
  MD_RTF* r = (MD_RTF*) userdata;

  switch(type) {
      case MD_BLOCK_DOC:      render_leave_block_doc(r); break;
      case MD_BLOCK_QUOTE:    render_leave_block_quote(r); break;
      case MD_BLOCK_UL:       render_leave_block_ul(r); break;
      case MD_BLOCK_OL:       render_leave_block_ol(r); break;
      case MD_BLOCK_LI:       render_leave_block_li(r); break;
      case MD_BLOCK_HR:       /*noop*/ break;
      case MD_BLOCK_H:        render_leave_block_h(r, (const MD_BLOCK_H_DETAIL*)detail); break;
      case MD_BLOCK_CODE:     render_leave_block_code(r); break;
      case MD_BLOCK_P:        render_leave_block_p(r); break;
      case MD_BLOCK_HTML:     /* noop */ break;
      case MD_BLOCK_TABLE:    render_leave_block_table(r); break;
      case MD_BLOCK_THEAD:    render_leave_block_thead(r); break;
      case MD_BLOCK_TBODY:    /* noop */ break;
      case MD_BLOCK_TR:       render_leave_block_tr(r); break;
      case MD_BLOCK_TH:       render_leave_block_th(r); break;
      case MD_BLOCK_TD:       render_leave_block_td(r); break;
  }

  return 0;
}

static int
enter_span_callback(MD_SPANTYPE type, void* detail, void* userdata)
{
  MD_RTF* r = (MD_RTF*) userdata;

  switch(type) {
      case MD_SPAN_EM:                RENDER_VERBATIM(r, "\\i "); break;
      case MD_SPAN_STRONG:            RENDER_VERBATIM(r, "\\b "); break;
      case MD_SPAN_U:                 RENDER_VERBATIM(r, "\\ul "); break;
      case MD_SPAN_DEL:               RENDER_VERBATIM(r, "\\strike "); break;
      case MD_SPAN_A:                 render_enter_span_url(r, (MD_SPAN_A_DETAIL*) detail); break;
      case MD_SPAN_CODE:              render_font_mono(r); break;
      //case MD_SPAN_IMG:               render_open_img_span(r, (MD_SPAN_IMG_DETAIL*) detail); break;
      //case MD_SPAN_LATEXMATH:         RENDER_VERBATIM(r, "<x-equation>"); break;
      //case MD_SPAN_LATEXMATH_DISPLAY: RENDER_VERBATIM(r, "<x-equation type=\"display\">"); break;
      //case MD_SPAN_WIKILINK:          render_open_wikilink_span(r, (MD_SPAN_WIKILINK_DETAIL*) detail); break;
  }

  return 0;
}

static int
leave_span_callback(MD_SPANTYPE type, void* detail, void* userdata)
{
  MD_RTF* r = (MD_RTF*) userdata;

  switch(type) {
      case MD_SPAN_EM:                RENDER_VERBATIM(r, "\\i0 "); break;
      case MD_SPAN_STRONG:            RENDER_VERBATIM(r, "\\b0 "); break;
      case MD_SPAN_U:                 RENDER_VERBATIM(r, "\\ul0 "); break;
      case MD_SPAN_DEL:               RENDER_VERBATIM(r, "\\strike0 "); break;
      case MD_SPAN_A:                 render_leave_span_url(r); break;
      case MD_SPAN_CODE:              render_font_norm(r); break;
      //case MD_SPAN_IMG:               /*noop, handled above*/ break;
      //case MD_SPAN_LATEXMATH:         /*fall through*/
      //case MD_SPAN_LATEXMATH_DISPLAY: RENDER_VERBATIM(r, "</x-equation>"); break;
      //case MD_SPAN_WIKILINK:          RENDER_VERBATIM(r, "</x-wikilink>"); break;
  }

  return 0;
}


static int
text_callback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
{
  MD_RTF* r = (MD_RTF*) userdata;

  switch(type) {
      case MD_TEXT_NULLCHAR:  RENDER_VERBATIM(r, "\0"); break;
      case MD_TEXT_BR:        RENDER_VERBATIM(r, "\\line1"); break;
      case MD_TEXT_SOFTBR:    RENDER_VERBATIM(r, "\r\n"); break;
      case MD_TEXT_HTML:      render_rtf_escaped(r, text, size); break;
      case MD_TEXT_ENTITY:    render_entity(r, text, size, render_rtf_escaped); break;
      default:                render_rtf_escaped(r, text, size); break;
  }

  return 0;
}

static void
debug_log_callback(const char* msg, void* userdata)
{
  MD_RTF* r = (MD_RTF*) userdata;
  if(r->flags & MD_RTF_FLAG_DEBUG)
      fprintf(stderr, "MD4C: %s\n", msg);
}


int md_rtf(const MD_CHAR* input, MD_SIZE input_size,
            void (*process_output)(const MD_CHAR*, MD_SIZE, void*),
            void* userdata, unsigned parser_flags, unsigned renderer_flags,
            unsigned font_size, unsigned doc_width)
{
  MD_RTF render;
  render.process_output = process_output;
  render.userdata = userdata;
  render.flags = renderer_flags;
  render.font_base = 2 * font_size; /* point to half-point */
  render.page_width = 56.689f * doc_width; /* mm to twip */
  render.page_height = 1.41428f * render.page_width; /* ISO 216 ratio */
  render.page_margin = 400; /* left and right margin */
  render.list_depth = -1;
  render.list_stop = 0;
  render.list_reset = 0;
  render.no_p_end = 0;


  MD_PARSER parser = {
      0,
      parser_flags,
      enter_block_callback,
      leave_block_callback,
      enter_span_callback,
      leave_span_callback,
      text_callback,
      debug_log_callback,
      NULL
  };

  /* Build map of characters which need escaping. */
  for(unsigned i = 0; i < 256; i++) {

      unsigned char ch = (unsigned char)i;

      render.escape_map[i] = 0;

      if(strchr("\\{}\n", ch) != NULL || ch > 0x7F)
          render.escape_map[i] |= NEED_RTF_ESC_FLAG|NEED_PRE_ESC_FLAG;

      if(!ISALNUM(ch)  &&  strchr("~-_.+!*(),%#@?=;:/,+$", ch) == NULL)
          render.escape_map[i] |= NEED_URL_ESC_FLAG;
  }

  /* Consider skipping UTF-8 byte order mark (BOM). */
  if(renderer_flags & MD_RTF_FLAG_SKIP_UTF8_BOM  &&  sizeof(MD_CHAR) == 1) {

    static const MD_CHAR bom[3] = { 0xef, 0xbb, 0xbf };

    if(input_size >= sizeof(bom)  &&  memcmp(input, bom, sizeof(bom)) == 0) {
      input += sizeof(bom);
      input_size -= sizeof(bom);
    }
  }

  /* build preformated strings of control words with space and size
  values according given font size and document width */

  /* we clamp size in order to prevent buffer overflow due to large numbers
  printed in template strings */
  if(render.font_base > 98)
    render.font_base = 98;

  /* general font sizes */
  sprintf(render.cw_fs[0], "\\fs%u", render.font_base );
  sprintf(render.cw_fs[1], "\\fs%u", (unsigned)(0.9f*render.font_base) );

  /* titles styles per level with font size and space-after values */
  sprintf(render.cw_hf[0], "\\fs%u\\sa%u\\b ", (unsigned)(2.2f*render.font_base), 2*render.font_base);
  sprintf(render.cw_hf[1], "\\fs%u\\sa%u\\b ", (unsigned)(1.7f*render.font_base), 2*render.font_base);
  sprintf(render.cw_hf[2], "\\fs%u\\sa%u\\b ", (unsigned)(1.4f*render.font_base), 2*render.font_base);
  sprintf(render.cw_hf[3], "\\fs%u\\sa%u\\b\\i ", (unsigned)(1.2f*render.font_base), 2*render.font_base);
  sprintf(render.cw_hf[4], "\\fs%u\\sa%u\\b\\i ", (unsigned)(1.1f*render.font_base), 2*render.font_base);
  sprintf(render.cw_hf[5], "\\fs%u\\sa%u\\b\\i ", (unsigned)(render.font_base), 2*render.font_base);

  /* space-after values */
  sprintf(render.cw_sa[0], "\\sa%u ", 2*render.font_base);
  sprintf(render.cw_sa[1], "\\sa%u ", 3*render.font_base);
  sprintf(render.cw_sa[2], "\\sa%u ", 6*render.font_base);

  /* left-ident values , up to 8 level */
  sprintf(render.cw_li[0], "\\li%u",  20*render.font_base);
  sprintf(render.cw_li[1], "\\li%u",  40*render.font_base);
  sprintf(render.cw_li[2], "\\li%u",  60*render.font_base);
  sprintf(render.cw_li[3], "\\li%u",  80*render.font_base);
  sprintf(render.cw_li[4], "\\li%u", 100*render.font_base);
  sprintf(render.cw_li[5], "\\li%u", 120*render.font_base);
  sprintf(render.cw_li[6], "\\li%u", 140*render.font_base);
  sprintf(render.cw_li[7], "\\li%u", 160*render.font_base);

  /* tables basic parameter and left margin */
  unsigned l = 12*render.font_base;
  sprintf(render.cw_tr[0], "\\trgaph%u\\trleft%u", 6*render.font_base, l);
  sprintf(render.cw_tr[1], "\\trgaph%u\\trrh%u\\trleft%u", 3*render.font_base, 16*render.font_base, l);

  /* frist-line indent values, used for bulleted and numbered lists */
  sprintf(render.cw_fi[0], "\\fi%i", -10*render.font_base);
  sprintf(render.cw_fi[1], "\\fi%i", -12*render.font_base);

  /* table cell width adjusted to given page width */
  unsigned w = render.page_width - (2*render.page_margin) - (0.2f*render.page_width);
  sprintf(render.cw_cx[0], "\\cellx%u ", w );
  sprintf(render.cw_cx[1], "\\cellx%u ", render.page_width * 2 );

  return md_parse(input, input_size, &parser, (void*)&render);
}
