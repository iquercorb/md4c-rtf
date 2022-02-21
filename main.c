#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Windows.h>
#include <RichEdit.h>

#include "md4c-rtf.h"

/**
 * Load source text file in buffer
 */
static char* md2rtf_load_text(const char* path, size_t* size)
{
  FILE*   fp;
  size_t  rb;

  *size = 0;
  char* text = NULL;

  fp = fopen(path, "rb");
  if(fp) {
    fseek(fp, 0, SEEK_END);
    *size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    text = (char*)malloc(*size + 1);
    if(!text) {
      fclose(fp);
      return NULL;
    }

    rb = fread(text, 1, *size, fp);
    if(rb != *size) {
      free(text);
      fclose(fp);
      return NULL;
    }
    text[rb] = '\0';

    fclose(fp);
  }

  return text;
}

/**
 * Custom MD to Rich Edit Stream context structure
 */
typedef struct {
  MD_RTF_DATA*  buf;
  size_t        cap;
  size_t        len;
  size_t        brd;
} MD2RTF_CTX;

/**
 * Initialize MD to Rich Edit Stream context
 */
static void md2rtf_ctx_init(MD2RTF_CTX* mdrtf)
{
  mdrtf->buf = (MD_RTF_DATA*)malloc(4096);
  mdrtf->cap = 4096;
  mdrtf->len = 0;
  mdrtf->brd = 0;
}

/**
 * Free MD to Rich Edit Stream stream context
 */
static void md2rtf_ctx_free(MD2RTF_CTX* mdrtf)
{
  if(mdrtf->buf) {
    free(mdrtf->buf);
    mdrtf->buf = NULL;
  }
  mdrtf->cap = 0;
  mdrtf->len = 0;
  mdrtf->brd = 0;
}

/**
 * MD Parse / Render callback function
 */
static void md2rtf_write_cb(const MD_RTF_DATA* data, unsigned size, void* ptr)
{
  MD2RTF_CTX* mdrtf = (MD2RTF_CTX*)ptr;

  if((mdrtf->len + size) > mdrtf->cap) {

    if(mdrtf->cap > 0) {

      size_t need = (mdrtf->len + size);

      while(mdrtf->cap < need)
        mdrtf->cap *= 2;

      mdrtf->buf = (MD_RTF_DATA*)realloc(mdrtf->buf, mdrtf->cap);

    } else {
      return;
    }
  }

  memcpy(mdrtf->buf + mdrtf->len, data, size);

  mdrtf->len += size;
}

/**
 * Parse and render text source to RTF data
 */
static void md2rtf_parse_text(MD2RTF_CTX* mdrtf, const char* text, size_t size)
{
  if(text == NULL)
    return;

  unsigned parser_flags = MD_FLAG_UNDERLINE|MD_FLAG_TABLES|MD_FLAG_PERMISSIVEAUTOLINKS;
  unsigned renderer_flags = MD_RTF_FLAG_DEBUG|MD_RTF_FLAG_SKIP_UTF8_BOM;
  unsigned font_size = 12;
  unsigned doc_width = 210;

  md_rtf(text, size, md2rtf_write_cb, mdrtf, parser_flags, renderer_flags, font_size, doc_width);
}

/**
 * Save RTF data to file
 */
static size_t md2rtf_save_as_rtf(MD2RTF_CTX* mdrtf, const char* path)
{
  FILE* fp = fopen(path, "wb");
  if(fp) {
    size_t wb = fwrite(mdrtf->buf, 1, mdrtf->len, fp);
    fclose(fp);
    return wb;
  }
  return 0;
}

/**
 * Rich Edit input stream callback function
 */
static DWORD CALLBACK EditStreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb)
{
  MD2RTF_CTX* mdrtf = (MD2RTF_CTX*)dwCookie;

  if(mdrtf->len) {

    if(cb <= mdrtf->len) {

      memcpy(pbBuff, mdrtf->buf + mdrtf->brd, cb);

      mdrtf->brd += cb;
      mdrtf->len -= cb;

      *pcb = cb;

    } else {

      memcpy(pbBuff, mdrtf->buf + mdrtf->brd, mdrtf->len);

      *pcb = mdrtf->len;

      mdrtf->brd += mdrtf->len;
      mdrtf->len = 0;
    }

  } else {

    *pcb = 0;
  }

  return 0;
}

/**
 * Send RTF data to Rich Edit control
 */
static int md2rtf_stream_in_edit(MD2RTF_CTX* mdrtf, HWND hwnd)
{
  // send RTF data to Rich Edit
  EDITSTREAM es = { 0 };
  es.pfnCallback = EditStreamCallback;
  es.dwCookie    = (DWORD_PTR)mdrtf;

  SendMessage(hwnd, EM_STREAMIN, SF_RTF, (LPARAM)&es);

  return es.dwError;
}

/**
 * Some constant for Windows
 */
#define IDC_RICH_EDIT                   40001
const wchar_t MD2RTF_CLSNAME[] =  L"MD2RTF_VIEWER";

/**
 * Window Procedure callback function
 */
LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  LONG rect[4];

  char* text;
  size_t size;
  MD2RTF_CTX mdrtf;

  switch(uMsg)
  {
  case WM_SHOWWINDOW:

    /* load text file */
    text = md2rtf_load_text("hello.txt", &size);

    /* initialize md2rtf custom context */
    md2rtf_ctx_init(&mdrtf);

    /* parse and render text to RTF */
    md2rtf_parse_text(&mdrtf, text, size);

    /* save RTF data to file */
    md2rtf_save_as_rtf(&mdrtf, "hello.rtf");

    /* stream RTF data to Rich Edit */
    md2rtf_stream_in_edit(&mdrtf, GetDlgItem(hWnd, IDC_RICH_EDIT));

    /* free allocated data */
    md2rtf_ctx_free(&mdrtf);

    break;

  case WM_SIZE:
    GetClientRect(hWnd, (RECT*)&rect);
    SetWindowPos(GetDlgItem(hWnd, IDC_RICH_EDIT), 0, 0, 0, rect[2], rect[3], SWP_NOZORDER);
    break;

  case WM_CLOSE:
    DestroyWindow(hWnd);
    break;

  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  }

  return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

/**
 * Main entry
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  LoadLibrary(TEXT("Msftedit.dll"));

  MSG Msg;

  /* register class for main window */
  WNDCLASSEXW wcx;
  ZeroMemory(&wcx, sizeof(WNDCLASSEX));
  wcx.cbSize        = sizeof(WNDCLASSEX);
  wcx.lpfnWndProc   = WindowProc;
  wcx.hInstance     = hInstance;
  wcx.lpszClassName = MD2RTF_CLSNAME;

  if(!RegisterClassExW(&wcx)) {
    return -1;
  }

  /* create main window */
  HWND hwnd = CreateWindowExW( 0, MD2RTF_CLSNAME, L"MD4C-RTF Viewer",
                                  WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
                                  NULL, NULL, hInstance, NULL);

  if(hwnd == NULL) {
    return -1;
  }

  /* create Rich Edit Control child */
  CreateWindowExW( 0, MSFTEDIT_CLASS, L"",
                  WS_CHILD | ES_MULTILINE | WS_VISIBLE | WS_VSCROLL,
                  0, 0, 0, 0, hwnd, (HMENU)IDC_RICH_EDIT, hInstance, NULL);


  /* show and update main window */
  ShowWindow(hwnd, nCmdShow);
  UpdateWindow(hwnd);

  /* message loop */
  while(GetMessage(&Msg, NULL, 0, 0) > 0) {
    TranslateMessage(&Msg);
    DispatchMessage(&Msg);
  }

  return 0;
}
