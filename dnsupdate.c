/*
 *  OpenVPN-GUI -- A Windows GUI for OpenVPN.
 *
 *  Copyright (C) 2026 OpenVPN-GUI contributors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program (see the file COPYING included with this
 *  distribution); if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Update the server address of a connection's config file from a DNS TXT
 * record. The TXT record is expected to hold the latest server address in
 * "IP:port" format, e.g.:  203.0.113.7:1194
 *
 * The pure-logic helpers below (ParseServerTxt, RewriteRemoteLines) have no
 * Windows dependency so they can be unit-tested natively; define
 * DNSUPDATE_STANDALONE_TEST to build the test harness.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* pure logic (also used by the standalone test harness)               */
/* ------------------------------------------------------------------ */

/*
 * Find the first "a.b.c.d:port" or "a.b.c.d port" occurrence in txt.
 * Both colon-separated (203.0.113.7:1194) and space-separated
 * (203.0.113.7 1194) forms are accepted.
 * Returns 1 and fills ip/port on success, 0 otherwise.
 */
int
ParseServerTxt(const char *txt, char *ip, size_t ip_len, int *port)
{
    const char *p;

    if (!txt || !ip || ip_len == 0 || !port)
        return 0;

    for (p = txt; *p; p++)
    {
        unsigned int a, b, c, d;
        int n = 0;

        if (!isdigit((unsigned char)*p))
            continue;

        /* do not match inside a longer numeric token, e.g. "111.2.3.4" */
        if (p != txt && (isdigit((unsigned char)p[-1]) || p[-1] == '.'))
            continue;

        if (sscanf(p, "%u.%u.%u.%u%n", &a, &b, &c, &d, &n) != 4)
            continue;
        if (a >= 256 || b >= 256 || c >= 256 || d >= 256)
            continue;

        /* separator after the IP: ':' or whitespace, then the port digits */
        {
            const char *s = p + n;
            int po = 0;
            int digits = 0;

            if (*s == ':')
            {
                s++;
                while (*s == ' ' || *s == '\t')
                    s++;
            }
            else if (*s == ' ' || *s == '\t')
            {
                while (*s == ' ' || *s == '\t')
                    s++;
            }
            else
                continue;

            while (isdigit((unsigned char)*s))
            {
                po = po * 10 + (*s - '0');
                s++;
                digits++;
            }
            if (digits == 0 || po <= 0 || po >= 65536)
                continue;

            snprintf(ip, ip_len, "%u.%u.%u.%u", a, b, c, d);
            *port = po;
            return 1;
        }
    }

    return 0;
}

/*
 * Rewrite every "remote" line in src as "remote <ip> <port>[ <rest>]",
 * preserving leading whitespace, trailing options (e.g. proto) and the
 * original line endings. Comment lines (#, ;) are left untouched.
 *
 * Returns a malloc'd buffer with the new content, or NULL if no remote
 * line was found (or on allocation failure). *count receives the number
 * of rewritten lines. The caller frees the result.
 */
char *
RewriteRemoteLines(const char *src, const char *ip, int port, int *count)
{
    const char *p = src;
    char *out;
    size_t len = 0, cap;
    char portstr[16];
    int n = 0;

    if (count)
        *count = 0;
    if (!src || !ip)
        return NULL;

    snprintf(portstr, sizeof(portstr), "%d", port);

    cap = strlen(src) + 256;
    out = malloc(cap);
    if (!out)
        return NULL;

    while (*p)
    {
        const char *e = p;          /* end of line content (excl. ending) */
        const char *le;
        const char *ending = "";
        size_t ending_len = 0;

        while (*e && *e != '\n' && *e != '\r')
            e++;
        le = e;
        if (*e == '\r' && e[1] == '\n')
        {
            ending = "\r\n";
            ending_len = 2;
            e += 2;
        }
        else if (*e == '\n' || *e == '\r')
        {
            ending = e;
            ending_len = 1;
            e += 1;
        }

        /* try to parse a "remote" directive on this line */
        const char *q = p;
        const char *newline = NULL;
        size_t newline_len = 0;
        int is_remote = 0;

        while (q < le && (*q == ' ' || *q == '\t'))
            q++;

        if (q < le && *q != '#' && *q != ';' && (size_t)(le - q) >= 6
            && strncmp(q, "remote", 6) == 0
            && (q + 6 == le || q[6] == ' ' || q[6] == '\t'))
        {
            const char *h = q + 6;   /* host token */
            const char *he;

            while (h < le && (*h == ' ' || *h == '\t'))
                h++;
            he = h;
            while (he < le && *he != ' ' && *he != '\t')
                he++;

            if (he > h)
            {
                /* optional port token: only treated as port if all digits */
                const char *t = he;
                const char *te;
                const char *rest;
                int all_digits;

                while (t < le && (*t == ' ' || *t == '\t'))
                    t++;
                te = t;
                while (te < le && *te != ' ' && *te != '\t')
                    te++;

                all_digits = (te > t);
                {
                    const char *d;
                    for (d = t; d < te; d++)
                    {
                        if (!isdigit((unsigned char)*d))
                        {
                            all_digits = 0;
                            break;
                        }
                    }
                }
                /* if the token after host is not numeric, it is not a port:
                 * keep it (and the whitespace before it) as part of rest,
                 * e.g. "remote host udp" keeps its proto */
                rest = (all_digits ? te : he);

                {
                    size_t lead_len = (size_t)(q - p);
                    size_t rest_len = (size_t)(le - rest);
                    char *nl;
                    char *w;

                    newline_len = lead_len + 7 + strlen(ip) + 1 + strlen(portstr) + rest_len;
                    nl = malloc(newline_len + 1);
                    if (nl)
                    {
                        w = nl;
                        memcpy(w, p, lead_len);
                        w += lead_len;
                        memcpy(w, "remote ", 7);
                        w += 7;
                        strcpy(w, ip);
                        w += strlen(ip);
                        *w++ = ' ';
                        strcpy(w, portstr);
                        w += strlen(portstr);
                        memcpy(w, rest, rest_len);
                        w += rest_len;
                        *w = '\0';

                        newline = nl;
                        is_remote = 1;
                    }
                }
            }
        }

        {
            const char *emit = is_remote ? newline : p;
            size_t emit_len = is_remote ? newline_len : (size_t)(le - p);
            size_t need = emit_len + ending_len;

            if (len + need + 1 > cap)
            {
                char *nout;

                cap = (len + need + 1) * 2;
                nout = realloc(out, cap);
                if (!nout)
                {
                    free(out);
                    free((void *)newline);
                    return NULL;
                }
                out = nout;
            }
            memcpy(out + len, emit, emit_len);
            len += emit_len;
            memcpy(out + len, ending, ending_len);
            len += ending_len;
            free((void *)newline);
        }

        if (is_remote)
            n++;
        p = e;
    }

    out[len] = '\0';
    if (count)
        *count = n;
    if (n == 0)
    {
        free(out);
        return NULL;
    }
    return out;
}

#ifndef DNSUPDATE_STANDALONE_TEST
/* ------------------------------------------------------------------ */
/* Windows-specific part                                                */
/* ------------------------------------------------------------------ */

#include <windows.h>
#include <windns.h>

#include "options.h"
#include "dnsupdate.h"
#include "localization.h"
#include "registry.h"
#include "openvpn-gui-res.h"

extern options_t o;

/* registry value name for the remembered DNS domain (per connection) */
#define DNS_DOMAIN_VALUE_NAME L"dns_update_domain"

/* dialog item ids (dialog-local) */
#define IDC_DNS_HINT 1001

#define DOMAIN_MAX 256

typedef struct
{
    WCHAR initial[DOMAIN_MAX]; /* pre-filled domain (may be empty) */
    WCHAR domain[DOMAIN_MAX];  /* result */
    UINT title_id;             /* dialog title string id */
    int confirmed;
} domain_dlg_ctx_t;

/* ---- in-memory dialog template builder ---- */

typedef struct
{
    BYTE *buf;
    size_t len;
    size_t cap;
} tmpl_buf_t;

static void
tmpl_reserve(tmpl_buf_t *t, size_t need)
{
    if (t->len + need > t->cap)
    {
        size_t ncap = t->cap ? t->cap * 2 : 512;

        while (ncap < t->len + need)
            ncap *= 2;
        t->buf = realloc(t->buf, ncap);
        t->cap = ncap;
    }
}

static void
tmpl_put16(tmpl_buf_t *t, WORD v)
{
    tmpl_reserve(t, 2);
    t->buf[t->len++] = (BYTE)(v & 0xff);
    t->buf[t->len++] = (BYTE)(v >> 8);
}

static void
tmpl_put32(tmpl_buf_t *t, DWORD v)
{
    tmpl_put16(t, (WORD)(v & 0xffff));
    tmpl_put16(t, (WORD)(v >> 16));
}

static void
tmpl_put_wstr(tmpl_buf_t *t, const WCHAR *s)
{
    size_t n = (wcslen(s) + 1) * sizeof(WCHAR);

    tmpl_reserve(t, n);
    memcpy(t->buf + t->len, s, n);
    t->len += n;
}

static void
tmpl_align32(tmpl_buf_t *t)
{
    size_t pad = (4 - (t->len % 4)) % 4;

    tmpl_reserve(t, pad);
    memset(t->buf + t->len, 0, pad);
    t->len += pad;
}

static void
tmpl_add_item(tmpl_buf_t *t, DWORD style, int x, int y, int cx, int cy,
              WORD id, WORD class_atom, const WCHAR *text)
{
    tmpl_align32(t);
    tmpl_put32(t, style);
    tmpl_put32(t, 0); /* extended style */
    tmpl_put16(t, (WORD)x);
    tmpl_put16(t, (WORD)y);
    tmpl_put16(t, (WORD)cx);
    tmpl_put16(t, (WORD)cy);
    tmpl_put16(t, id);
    tmpl_put16(t, 0xFFFF);
    tmpl_put16(t, class_atom);
    tmpl_put_wstr(t, text);
    tmpl_put16(t, 0); /* no creation data */
}

/*
 * Build a simple "enter domain name" dialog template in memory, so no
 * per-language .rc changes are needed (texts are set from string
 * resources at WM_INITDIALOG).
 */
static DLGTEMPLATE *
BuildDomainDialog(void)
{
    tmpl_buf_t t = { 0 };

    tmpl_put32(&t, WS_POPUP | WS_BORDER | WS_SYSMENU | DS_MODALFRAME | WS_CAPTION
                      | DS_CENTER | DS_SETFONT);
    tmpl_put32(&t, 0);   /* dwExtendedStyle */
    tmpl_put16(&t, 4);   /* cdit */
    tmpl_put16(&t, 0);   /* x */
    tmpl_put16(&t, 0);   /* y */
    tmpl_put16(&t, 256); /* cx */
    tmpl_put16(&t, 100); /* cy */
    tmpl_put16(&t, 0);   /* menu: none */
    tmpl_put16(&t, 0);   /* class: default dialog */
    tmpl_put_wstr(&t, L"");
    tmpl_put16(&t, 8);   /* font size */
    tmpl_put_wstr(&t, L"Microsoft Sans Serif");

    /* hint text (set from resources at runtime) */
    tmpl_add_item(&t, WS_CHILD | WS_VISIBLE | SS_LEFT,
                  8, 8, 240, 30, IDC_DNS_HINT, 0x0082, L"");
    /* domain edit */
    tmpl_add_item(&t, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                  8, 42, 240, 13, ID_EDT_DOMAIN, 0x0081, L"");
    /* OK / Cancel (texts set from resources at runtime) */
    tmpl_add_item(&t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                  140, 78, 50, 14, IDOK, 0x0080, L"");
    tmpl_add_item(&t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                  198, 78, 50, 14, IDCANCEL, 0x0080, L"");

    return (DLGTEMPLATE *)t.buf;
}

static INT_PTR CALLBACK
DomainDlgProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    domain_dlg_ctx_t *ctx = (domain_dlg_ctx_t *)GetWindowLongPtrW(hwndDlg, DWLP_USER);

    switch (msg)
    {
        case WM_INITDIALOG:
            ctx = (domain_dlg_ctx_t *)lParam;
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)ctx);
            SetWindowTextW(hwndDlg, LoadLocalizedString(
                                       ctx->title_id ? ctx->title_id
                                                     : IDS_DLG_UPDATE_SERVER_TITLE));
            SetDlgItemTextW(hwndDlg, IDC_DNS_HINT,
                            LoadLocalizedString(IDS_DLG_UPDATE_SERVER_HINT));
            SetDlgItemTextW(hwndDlg, IDOK, LoadLocalizedString(IDS_DLG_BTN_OK));
            SetDlgItemTextW(hwndDlg, IDCANCEL, LoadLocalizedString(IDS_DLG_BTN_CANCEL));
            if (ctx->initial[0])
            {
                SetDlgItemTextW(hwndDlg, ID_EDT_DOMAIN, ctx->initial);
                /* put focus on OK so Enter confirms immediately */
                SetFocus(GetDlgItem(hwndDlg, IDOK));
                return FALSE;
            }
            return TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK)
            {
                WCHAR *s, *e;

                GetDlgItemTextW(hwndDlg, ID_EDT_DOMAIN, ctx->domain, DOMAIN_MAX);
                /* trim whitespace in place */
                s = ctx->domain;
                while (*s == L' ' || *s == L'\t')
                    s++;
                if (s != ctx->domain)
                    wmemmove(ctx->domain, s, wcslen(s) + 1);
                e = ctx->domain + wcslen(ctx->domain);
                while (e > ctx->domain
                       && (e[-1] == L' ' || e[-1] == L'\t' || e[-1] == L'\r'
                           || e[-1] == L'\n'))
                    *--e = L'\0';

                if (ctx->domain[0] == L'\0')
                {
                    MessageBeep(MB_ICONWARNING);
                    SetFocus(GetDlgItem(hwndDlg, ID_EDT_DOMAIN));
                    return TRUE;
                }
                ctx->confirmed = 1;
                EndDialog(hwndDlg, IDOK);
                return TRUE;
            }
            else if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hwndDlg, IDCANCEL);
                return TRUE;
            }
            break;

        case WM_CLOSE:
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
    }

    return FALSE;
}

/* ---- DNS TXT lookup ---- */

/*
 * Query the TXT records of domain and return the raw text of the first
 * record in out (character-strings concatenated with spaces).
 */
#ifndef DNS_TYPE_TXT
#define DNS_TYPE_TXT DNS_TYPE_TEXT /* mingw-w64 only defines DNS_TYPE_TEXT */
#endif

static int
QueryTxtRecord(const WCHAR *domain, char *out, size_t out_len)
{
    PDNS_RECORD recs = NULL;
    PDNS_RECORD r;
    int ok = 0;

    if (DnsQuery_W(domain, DNS_TYPE_TXT, DNS_QUERY_STANDARD, NULL, &recs, NULL)
        != ERROR_SUCCESS)
        return 0;

    for (r = recs; r; r = r->pNext)
    {
        DWORD i;
        size_t pos = 0;

        if (r->wType != DNS_TYPE_TXT)
            continue;

        for (i = 0; i < r->Data.TXT.dwStringCount; i++)
        {
            const WCHAR *s = r->Data.TXT.pStringArray[i];
            char tmp[512];
            int n;

            if (!s)
                continue;
            /* TXT payload ("IP:port") is ASCII; convert defensively */
            n = WideCharToMultiByte(CP_UTF8, 0, s, -1, tmp, sizeof(tmp) - 1,
                                    NULL, NULL);
            if (n <= 0)
                continue;
            n--; /* strip terminating NUL */
            if (pos + (size_t)n + 2 > out_len)
                break;
            memcpy(out + pos, tmp, n);
            pos += (size_t)n;
            out[pos++] = ' ';
        }
        out[pos] = '\0';
        ok = 1;
        break; /* use the first TXT record */
    }

    DnsRecordListFree(recs, DnsFreeRecordList);
    return ok;
}

/* ---- config file helpers ---- */

static char *
ReadWholeFile(const WCHAR *path, size_t *size_out)
{
    HANDLE h;
    LARGE_INTEGER sz;
    char *buf;
    DWORD got = 0;
    size_t off = 0;

    /* strip a UTF-8 BOM on read; re-added on write */
    static const char bom[3] = { '\xEF', '\xBB', '\xBF' };

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 32 * 1024 * 1024)
    {
        CloseHandle(h);
        return NULL;
    }

    buf = malloc((size_t)sz.QuadPart + 1);
    if (!buf)
    {
        CloseHandle(h);
        return NULL;
    }

    while (off < (size_t)sz.QuadPart)
    {
        if (!ReadFile(h, buf + off, (DWORD)((size_t)sz.QuadPart - off), &got, NULL)
            || got == 0)
        {
            free(buf);
            CloseHandle(h);
            return NULL;
        }
        off += got;
    }
    CloseHandle(h);

    /* strip UTF-8 BOM if present */
    if (off >= 3 && memcmp(buf, bom, 3) == 0)
    {
        memmove(buf, buf + 3, off - 3);
        off -= 3;
    }
    buf[off] = '\0';

    if (size_out)
        *size_out = off;
    return buf;
}

static int
WriteWholeFile(const WCHAR *path, const char *data, size_t len, int add_bom)
{
    WCHAR tmp[MAX_PATH];
    HANDLE h;
    DWORD written = 0;
    size_t off = 0;
    static const char bom[3] = { '\xEF', '\xBB', '\xBF' };

    if (_snwprintf(tmp, _countof(tmp), L"%s.tmp", path) < 0)
        return 0;

    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    if (add_bom)
    {
        if (!WriteFile(h, bom, 3, &written, NULL) || written != 3)
        {
            CloseHandle(h);
            DeleteFileW(tmp);
            return 0;
        }
    }

    while (off < len)
    {
        if (!WriteFile(h, data + off, (DWORD)(len - off), &written, NULL)
            || written == 0)
        {
            CloseHandle(h);
            DeleteFileW(tmp);
            return 0;
        }
        off += written;
    }
    CloseHandle(h);

    /* atomic replace */
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(tmp);
        return 0;
    }
    return 1;
}

/* ---- remembered domain (per connection, stored in registry) ---- */

static int
GetRememberedDomain(connection_t *c, WCHAR *domain, size_t n)
{
    BYTE buf[DOMAIN_MAX * sizeof(WCHAR)];
    DWORD len;

    domain[0] = L'\0';
    len = GetConfigRegistryValue(c->config_name, DNS_DOMAIN_VALUE_NAME,
                                 buf, sizeof(buf) - sizeof(WCHAR));
    if (len == 0)
        return 0;
    /* ensure NUL termination */
    buf[len] = 0;
    buf[len + 1] = 0;
    wcsncpy(domain, (const WCHAR *)buf, n);
    domain[n - 1] = L'\0';
    return domain[0] != L'\0';
}

static void
RememberDomain(connection_t *c, const WCHAR *domain)
{
    SetConfigRegistryValueBinary(c->config_name, DNS_DOMAIN_VALUE_NAME,
                                (const BYTE *)domain,
                                (DWORD)((wcslen(domain) + 1) * sizeof(WCHAR)));
}

/*
 * Show the domain input dialog. initial may be NULL. Returns 1 and fills
 * domain when the user confirms, 0 on cancel.
 */
static int
AskDomain(const WCHAR *initial, UINT title_id, WCHAR *domain, size_t n)
{
    domain_dlg_ctx_t ctx;
    DLGTEMPLATE *tmpl;
    INT_PTR r;

    memset(&ctx, 0, sizeof(ctx));
    if (initial)
        wcsncpy(ctx.initial, initial, DOMAIN_MAX - 1);
    ctx.title_id = title_id;

    tmpl = BuildDomainDialog();
    if (!tmpl)
        return 0;
    r = DialogBoxIndirectParamW(o.hInstance, tmpl, NULL, DomainDlgProc, (LPARAM)&ctx);
    free(tmpl);
    if (r != IDOK || !ctx.confirmed || ctx.domain[0] == L'\0')
        return 0; /* cancelled */

    wcsncpy(domain, ctx.domain, n);
    domain[n - 1] = L'\0';
    return 1;
}

/* ---- entry points ---- */

/*
 * Check whether a DNS domain is remembered for this connection and, if so,
 * silently update the server address from its DNS TXT record before
 * connecting. Fully silent and fail-open: any failure just keeps the
 * existing config so the connection attempt can proceed.
 */
void
AutoUpdateServerFromDNS(connection_t *c)
{
    WCHAR domain[DOMAIN_MAX];
    char txt[2048];
    char ip[64];
    int port;
    char *content;
    char *updated;
    int nremote = 0;
    int had_bom = 0;
    WCHAR full_path[MAX_PATH];

    if (!c)
        return;

    /* no domain configured -> nothing to do (do not prompt here) */
    if (!GetRememberedDomain(c, domain, _countof(domain)))
        return;

    /* config_file holds just the file name; build the full path */
    _snwprintf(full_path, _countof(full_path), L"%s\\%s",
               c->config_dir, c->config_file);
    full_path[_countof(full_path) - 1] = L'\0';

    if (!QueryTxtRecord(domain, txt, sizeof(txt)))
        return;

    if (!ParseServerTxt(txt, ip, sizeof(ip), &port))
        return;

    {
        /* detect BOM so we can preserve it */
        HANDLE h = CreateFileW(full_path, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            char head[3];
            DWORD got = 0;

            if (ReadFile(h, head, 3, &got, NULL) && got == 3
                && memcmp(head, "\xEF\xBB\xBF", 3) == 0)
                had_bom = 1;
            CloseHandle(h);
        }
    }

    content = ReadWholeFile(full_path, NULL);
    if (!content)
        return;

    updated = RewriteRemoteLines(content, ip, port, &nremote);
    free(content);
    if (!updated || nremote == 0)
    {
        free(updated);
        return;
    }

    if (!WriteWholeFile(full_path, updated, strlen(updated), had_bom))
    {
        free(updated);
        return;
    }
    free(updated);

    /* success is silent by design; the new address is used right away */
}

/*
 * Let the user set (or change) the remembered DNS domain for a connection.
 */
void
SetDNSDomain(connection_t *c)
{
    WCHAR current[DOMAIN_MAX] = { 0 };
    WCHAR domain[DOMAIN_MAX];

    if (!c)
        return;

    GetRememberedDomain(c, current, _countof(current));
    if (!AskDomain(current[0] ? current : NULL, IDS_DLG_SET_DNS_TITLE,
                   domain, _countof(domain)))
        return; /* cancelled */

    RememberDomain(c, domain);
}

#endif /* DNSUPDATE_STANDALONE_TEST */

#ifdef DNSUPDATE_STANDALONE_TEST
/* ------------------------------------------------------------------ */
/* native test harness (Linux/macOS): gcc -DDNSUPDATE_STANDALONE_TEST  */
/* ------------------------------------------------------------------ */
static int failures = 0;

#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                      \
            failures++;                                                        \
        }                                                                      \
    } while (0)

int
main(void)
{
    char ip[64];
    int port;
    char *out;
    int n;

    /* --- ParseServerTxt --- */
    CHECK(ParseServerTxt("203.0.113.7:1194", ip, sizeof(ip), &port)
          && strcmp(ip, "203.0.113.7") == 0 && port == 1194);
    CHECK(ParseServerTxt("125.73.179.4 26337", ip, sizeof(ip), &port)  /* space-separated */
          && strcmp(ip, "125.73.179.4") == 0 && port == 26337);
    CHECK(ParseServerTxt("203.0.113.7: 1194", ip, sizeof(ip), &port)   /* colon + space */
          && strcmp(ip, "203.0.113.7") == 0 && port == 1194);
    CHECK(ParseServerTxt("latest server 203.0.113.7:1194 enjoy", ip, sizeof(ip), &port)
          && strcmp(ip, "203.0.113.7") == 0 && port == 1194);
    CHECK(ParseServerTxt("\"10.0.0.1:443\"", ip, sizeof(ip), &port)  /* quoted TXT */
          && strcmp(ip, "10.0.0.1") == 0 && port == 443);
    CHECK(!ParseServerTxt("no address here", ip, sizeof(ip), &port));
    CHECK(!ParseServerTxt("999.1.1.1:80", ip, sizeof(ip), &port));   /* bad octet */
    CHECK(!ParseServerTxt("999.1.1.1 80", ip, sizeof(ip), &port));  /* bad octet, space form */
    CHECK(!ParseServerTxt("1.2.3.4:0", ip, sizeof(ip), &port));       /* bad port */
    CHECK(!ParseServerTxt("1.2.3.4 0", ip, sizeof(ip), &port));       /* bad port, space form */
    CHECK(!ParseServerTxt("1.2.3.4:99999", ip, sizeof(ip), &port));   /* bad port */
    CHECK(!ParseServerTxt("1.2.3.4:", ip, sizeof(ip), &port));        /* no port */
    CHECK(!ParseServerTxt("1.2.3.4 abc", ip, sizeof(ip), &port));     /* no digits after space */
    CHECK(!ParseServerTxt("", ip, sizeof(ip), &port));

    /* --- RewriteRemoteLines --- */
    {
        const char *cfg =
            "client\r\n"
            "dev tun\r\n"
            "remote 1.1.1.1 1194 udp\r\n"
            "# a comment\r\n"
            "; another comment\r\n"
            "remote 2.2.2.2\r\n"
            "remote old.example.com udp\r\n"
            "resolv-retry infinite\r\n";
        const char *expect =
            "client\r\n"
            "dev tun\r\n"
            "remote 9.9.9.9 5555 udp\r\n"
            "# a comment\r\n"
            "; another comment\r\n"
            "remote 9.9.9.9 5555\r\n"
            "remote 9.9.9.9 5555 udp\r\n"
            "resolv-retry infinite\r\n";

        out = RewriteRemoteLines(cfg, "9.9.9.9", 5555, &n);
        CHECK(out != NULL && n == 3 && strcmp(out, expect) == 0);
        free(out);
    }
    /* LF endings + leading whitespace preserved */
    {
        const char *cfg = "  remote 1.1.1.1 1194\nremote 2.2.2.2 443 tcp\n";
        const char *expect = "  remote 8.8.8.8 1\nremote 8.8.8.8 1 tcp\n";

        out = RewriteRemoteLines(cfg, "8.8.8.8", 1, &n);
        CHECK(out != NULL && n == 2 && strcmp(out, expect) == 0);
        free(out);
    }
    /* no remote line -> NULL */
    {
        out = RewriteRemoteLines("client\ndev tun\n", "8.8.8.8", 1, &n);
        CHECK(out == NULL && n == 0);
    }
    /* <connection> block remotes are rewritten too */
    {
        const char *cfg = "<connection>\nremote 1.1.1.1 1194\n</connection>\n";
        const char *expect = "<connection>\nremote 5.5.5.5 1194\n</connection>\n";

        out = RewriteRemoteLines(cfg, "5.5.5.5", 1194, &n);
        CHECK(out != NULL && n == 1 && strcmp(out, expect) == 0);
        free(out);
    }

    if (failures == 0)
        printf("ALL TESTS PASSED\n");
    return failures != 0;
}
#endif /* DNSUPDATE_STANDALONE_TEST */
