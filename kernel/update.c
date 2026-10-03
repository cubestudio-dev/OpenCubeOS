/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-09-fix5
 * File: kernel/update.c
 * Purpose: online update check (checkupdate) over HTTP or HTTPS.
 *
 * Transport is chosen by the update_url scheme from /etc/opencube.conf:
 *   https://... -> TLS 1.2 client (tls_https_get)
 *   http://...  -> plain TCP + HTTP/1.0 GET (net_socket/net_connect/...)
 *   other       -> OC_UPDATE_E_PREFIX
 *
 * All buffers used while receiving live on the heap / contiguous physical
 * pages - kernel thread stacks are one page (4096 bytes) and must never
 * hold response buffers (WP-09-FIX BUG-002 lesson).
 */
#include "update.h"
#include "ab_update.h"
#include "ext.h"
#include "config.h"
#include "net.h"
#include "tls.h"
#include "heap.h"
#include "pmm.h"
#include "sched.h"
#include "timer.h"
#include "console.h"
#include "log.h"
#include "string.h"

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* URL parsing                                                         */
/* ------------------------------------------------------------------ */

/* Parse scheme://host[:port][/path].  Returns 0 on success,
 * OC_UPDATE_E_PREFIX when the scheme is neither http:// nor https://. */
int oc_update_url_parse(const char *url, oc_update_url_t *out) {
    if (!url || !out) return OC_UPDATE_E_PREFIX;
    out->use_tls = 0;
    out->port = 0;
    out->host[0] = 0;
    out->path[0] = 0;

    const char *p = url;
    if (oc_strncmp(p, "https://", 8) == 0) {
        out->use_tls = 1;
        out->port = 443;
        p += 8;
    } else if (oc_strncmp(p, "http://", 7) == 0) {
        out->use_tls = 0;
        out->port = 80;
        p += 7;
    } else {
        return OC_UPDATE_E_PREFIX;
    }

    int i = 0;
    while (p[i] && p[i] != '/' && p[i] != ':' && i < (int)sizeof(out->host) - 1) {
        out->host[i] = p[i];
        i++;
    }
    out->host[i] = 0;
    if (out->host[0] == 0) return OC_UPDATE_E_PREFIX;

    if (p[i] == ':') {
        i++;
        int pd = 0;
        while (p[i] >= '0' && p[i] <= '9') {
            pd = pd * 10 + (p[i] - '0');
            i++;
        }
        if (pd > 0 && pd < 65536) out->port = pd;
    }
    if (p[i] == '/') {
        int j = 0;
        while (p[i] && j < (int)sizeof(out->path) - 1) out->path[j++] = p[i++];
        out->path[j] = 0;
    } else {
        oc_strcpy(out->path, "/");
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* JSON field extraction (top-level string fields, ASCII only)         */
/* ------------------------------------------------------------------ */

/* Find "key" : "value" in a flat JSON object and copy value (handling
 * simple backslash escapes by taking the next character literally).
 * Returns 0 on success, -1 when key/value not found, -8 when value does
 * not fit out.  This is a deliberately scoped parser: the update manifest
 * is a flat object (documented in docs/CONFIG.md); it is not a general
 * JSON parser. */
int oc_update_json_string(const char *json, const char *key,
                          char *out, int outlen) {
    if (!json || !key || !out || outlen <= 0) return -8;
    int klen = 0;
    while (key[klen]) klen++;

    const char *p = json;
    while ((p = oc_strchr(p, '"')) != NULL) {
        p++;                                   /* past opening quote */
        if (oc_strncmp(p, key, klen) == 0 && p[klen] == '"') {
            const char *q = p + klen + 1;
            while (*q == ' ' || *q == ':' || *q == '\t' || *q == '\r' ||
                   *q == '\n') q++;
            if (*q != '"') return -1;
            q++;                               /* past opening quote of value */
            int n = 0;
            while (*q && *q != '"') {
                char c = *q;
                if (c == '\\' && q[1]) {       /* escape: take next char literally */
                    q++;
                    c = *q;
                }
                if (n >= outlen - 1) return -8;
                out[n++] = c;
                q++;
            }
            if (*q != '"') return -1;          /* unterminated value */
            out[n] = 0;
            return 0;
        }
        /* not our key: skip to end of this string */
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) p++;
            p++;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* HTTP / HTTPS GET (body only)                                        */
/* ------------------------------------------------------------------ */

/* Extract a response header value (case-insensitive name match) from an
 * HTTP response header block.  Returns 0 and copies the value (leading
 * whitespace trimmed, NUL-terminated, truncated to cap) when found,
 * -1 when the header is absent. */
static int http_header_get(const char *hdr, int hdr_len, const char *name,
                           char *out, int cap) {
    if (!hdr || !name || !out || cap <= 0) return -1;
    int nlen = (int)oc_strlen(name);
    for (int k = 0; k + nlen + 1 < hdr_len; k++) {
        if (k != 0 && hdr[k - 1] != '\n') continue;   /* line start only */
        int m = 0;
        while (m < nlen) {
            char a = hdr[k + m], b = name[m];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            m++;
        }
        if (m != nlen || hdr[k + nlen] != ':') continue;
        int v = k + nlen + 1;
        while (v < hdr_len && (hdr[v] == ' ' || hdr[v] == '\t')) v++;
        int e = v;
        while (e < hdr_len && hdr[e] != '\r' && hdr[e] != '\n') e++;
        int len = e - v;
        if (len > cap - 1) len = cap - 1;
        for (int i = 0; i < len; i++) out[i] = hdr[v + i];
        out[len] = 0;
        return 0;
    }
    return -1;
}

/* Parse the status code from a "HTTP/1.x NNN ..." status line. */
static int http_status_code(const char *hdr) {
    const char *sp = hdr;
    while (*sp && *sp != ' ') sp++;
    while (*sp == ' ') sp++;
    int code = 0;
    while (*sp >= '0' && *sp <= '9') code = code * 10 + (*sp++ - '0');
    return code;
}

/* Fetch url, copy the response body into body (NUL-terminated).
 * Follows up to 3 HTTP redirects (301/302/303/307/308 with an absolute
 * Location URL - github.com release downloads respond 302 to a CDN
 * host, so without this every `update` download failed).
 * Returns body length >= 0, or OC_UPDATE_E_* negative code. */
static int http_get_body(const char *url, char *body, int body_cap) {
    static char cur_url[2048];   /* static: CDN redirect URLs reach ~1.4 KB; single-threaded shell */
    {
        int n = 0;
        while (url[n] && n < (int)sizeof(cur_url) - 1) {
            cur_url[n] = url[n];
            n++;
        }
        cur_url[n] = 0;
    }

    for (int hop = 0; hop < 4; hop++) {
        oc_update_url_t u;
        int rc = oc_update_url_parse(cur_url, &u);
        if (rc != 0) return rc;

        /* Resolve host: dotted-quad first, then DNS. */
        u32 ip = net_parse_ip(u.host);
        if (ip == 0) {
            if (dns_resolve(u.host, &ip) != 0) return OC_UPDATE_E_DNS;
        }

        if (u.use_tls) {
            /* HTTPS via the TLS 1.2 client (16 KiB contiguous pages, as wget). */
            enum { TLS_BUFSZ = 16384 };
            u8 *resp = (u8 *)(uintptr_t)pmm_alloc_contig(TLS_BUFSZ / PMM_PAGE_SIZE);
            if (!resp) return OC_UPDATE_E_BUFSIZE;
            int n = tls_https_get(ip, (u16)u.port, u.host, u.path, resp, TLS_BUFSZ);
            if (n <= 0) {
                for (int pg = 0; pg < TLS_BUFSZ / PMM_PAGE_SIZE; pg++)
                    pmm_free_frame((u64)(uintptr_t)resp + (u64)pg * PMM_PAGE_SIZE);
                return OC_UPDATE_E_CONNECT;
            }
            /* strip headers */
            int body_start = 0;
            for (int k = 0; k < n - 3; k++) {
                if (resp[k] == '\r' && resp[k + 1] == '\n' &&
                    resp[k + 2] == '\r' && resp[k + 3] == '\n') {
                    body_start = k + 4;
                    break;
                }
            }
            if (body_start == 0) {
                for (int pg = 0; pg < TLS_BUFSZ / PMM_PAGE_SIZE; pg++)
                    pmm_free_frame((u64)(uintptr_t)resp + (u64)pg * PMM_PAGE_SIZE);
                return OC_UPDATE_E_HTTP;
            }
            int code = http_status_code((const char *)resp);
            if (code == 301 || code == 302 || code == 303 ||
                code == 307 || code == 308) {
                char loc[2048];
                int hr = http_header_get((const char *)resp, body_start,
                                         "location", loc, (int)sizeof(loc));
                for (int pg = 0; pg < TLS_BUFSZ / PMM_PAGE_SIZE; pg++)
                    pmm_free_frame((u64)(uintptr_t)resp + (u64)pg * PMM_PAGE_SIZE);
                if (hr != 0) return OC_UPDATE_E_HTTP;   /* no Location */
                int m = 0;
                while (loc[m] && m < (int)sizeof(cur_url) - 1) {
                    cur_url[m] = loc[m];
                    m++;
                }
                cur_url[m] = 0;
                continue;                               /* follow redirect */
            }
            if (code < 200 || code > 299) {
                for (int pg = 0; pg < TLS_BUFSZ / PMM_PAGE_SIZE; pg++)
                    pmm_free_frame((u64)(uintptr_t)resp + (u64)pg * PMM_PAGE_SIZE);
                return OC_UPDATE_E_HTTP;
            }
            int blen = n - body_start;
            if (blen > body_cap - 1) blen = body_cap - 1;
            for (int i = 0; i < blen; i++) body[i] = (char)resp[body_start + i];
            body[blen] = 0;
            for (int pg = 0; pg < TLS_BUFSZ / PMM_PAGE_SIZE; pg++)
                pmm_free_frame((u64)(uintptr_t)resp + (u64)pg * PMM_PAGE_SIZE);
            return blen;
        }

        /* Plain HTTP over a TCP socket. */
        int sock = net_socket(SOCK_TCP);
        if (sock < 0) return OC_UPDATE_E_CONNECT;
        if (net_connect(sock, ip, (u16)u.port) < 0) {
            net_close(sock);
            return OC_UPDATE_E_CONNECT;
        }

        char request[256];
        oc_strcpy(request, "GET ");
        oc_strcat(request, u.path);
        oc_strcat(request, " HTTP/1.0\r\nHost: ");
        oc_strcat(request, u.host);
        oc_strcat(request, "\r\nUser-Agent: opencube-checkupdate\r\n\r\n");
        net_send(sock, request, oc_strlen(request));

        /* Buffer must hold a full TLS record / long header block:
         * github.com's 302 redirect headers alone are ~5.3 KB, and a
         * record that does not fit the buffer is truncated by tls_recv
         * (its tail is dropped), losing the \r\n\r\n terminator. */
        enum { RBUF_CAP = 20480 };
        char *rbuf = (char *)kmalloc(RBUF_CAP);
        if (!rbuf) { net_close(sock); return OC_UPDATE_E_BUFSIZE; }

        int total = 0, header_end = -1, content_length = -1;
        u64 start = oc_timer_ticks();
        /* Phase 1: headers. */
        while (header_end < 0) {
            if (oc_timer_ticks() - start > 500) {   /* ~5 s, same budget as DNS */
                kfree(rbuf); net_close(sock);
                return OC_UPDATE_E_TIMEOUT;
            }
            int n = net_recv(sock, rbuf + total, RBUF_CAP - 1 - total);
            if (n <= 0) {
                kfree(rbuf); net_close(sock);
                return OC_UPDATE_E_TIMEOUT;
            }
            total += n;
            rbuf[total] = 0;
            for (int k = 0; k <= total - 4; k++) {
                if (rbuf[k] == '\r' && rbuf[k + 1] == '\n' &&
                    rbuf[k + 2] == '\r' && rbuf[k + 3] == '\n') {
                    header_end = k;
                    break;
                }
            }
        }

        /* Status code from the first line: HTTP/1.x NNN */
        int code = http_status_code(rbuf);
        if (code == 301 || code == 302 || code == 303 ||
            code == 307 || code == 308) {
            char loc[2048];
            int hr = http_header_get(rbuf, header_end,
                                     "location", loc, (int)sizeof(loc));
            kfree(rbuf);
            net_close(sock);
            if (hr != 0) return OC_UPDATE_E_HTTP;   /* no Location */
            int m = 0;
            while (loc[m] && m < (int)sizeof(cur_url) - 1) {
                cur_url[m] = loc[m];
                m++;
            }
            cur_url[m] = 0;
            continue;                               /* follow redirect */
        }
        if (code < 200 || code > 299) {
            kfree(rbuf); net_close(sock);
            return OC_UPDATE_E_HTTP;
        }

        /* Content-Length (optional). */
        for (int k = 0; k <= header_end - 16; k++) {
            if (oc_strncmp(rbuf + k, "Content-Length:", 15) == 0) {
                content_length = 0;
                const char *cp = rbuf + k + 15;
                while (*cp == ' ') cp++;
                while (*cp >= '0' && *cp <= '9')
                    content_length = content_length * 10 + (*cp++ - '0');
                break;
            }
        }

        /* Phase 2: receive the rest of the body (into rbuf at
         * body_start + blen, i.e. where the next body byte belongs). */
        int body_start = header_end + 4;
        int blen = total - body_start;
        u64 t1 = oc_timer_ticks();
        while (blen < body_cap - 1 &&
               (content_length < 0 || blen < content_length)) {
            if (oc_timer_ticks() - t1 > 500) break;   /* no more data coming */
            int room = RBUF_CAP - 1 - (body_start + blen);
            if (room <= 0) break;
            int n = net_recv(sock, rbuf + body_start + blen, room);
            if (n <= 0) break;
            blen += n;
        }
        net_close(sock);

        int copy = blen;
        if (copy > body_cap - 1) copy = body_cap - 1;
        for (int i = 0; i < copy; i++) body[i] = rbuf[body_start + i];
        body[copy] = 0;
        kfree(rbuf);
        return copy;
    }
    return OC_UPDATE_E_HTTP;   /* too many redirects */
}

/* ------------------------------------------------------------------ */
/* changes v2 (long changelog, dual-manifest protocol)                 */
/* ------------------------------------------------------------------ */

/* Server-side compatibility protocol ("Plan D", rule 8: stay compatible
 * with old versions without losing the new-version experience):
 *
 *   /update.json      SHORT "changes" (<= 127 bytes) so OLD kernels
 *                     (WP-09: 128-byte buffer + hard error on overflow)
 *                     keep parsing the manifest, plus an OPTIONAL
 *                     "changes_v2_url" string field pointing at the
 *                     long-changelog manifest.
 *   /update-v2.json   the same manifest with the FULL-LENGTH "changes".
 *
 * New kernels (WP-10a+/WP-10c with this protocol) fetch changes_v2_url
 * after the base manifest parses and upgrade out->changes in place.
 * Every failure on this path (missing field, non-absolute URL, network
 * error, parse error) silently keeps the short value: "changes" is
 * display-only and must never fail the check itself. */

/* Extract the changes_v2_url field from a manifest body.  Returns 0 and
 * fills url (NUL-terminated, truncated to cap) when the field is present,
 * -1 when the field is missing or empty. */
int oc_update_changes_v2_url(const char *body, char *url, int cap) {
    if (!body || !url || cap <= 0) return -1;
    if (oc_update_json_string(body, "changes_v2_url", url, cap) != 0)
        return -1;
    if (url[0] == 0) return -1;
    return 0;
}

/* Fetch the changes-v2 manifest at v2url and copy its long "changes"
 * value into out (outcap bytes, truncated to fit).  Only absolute
 * http:// or https:// URLs are accepted - anything else is ignored.
 * Returns 0 when out was upgraded, -1 when the URL was rejected,
 * or the OC_UPDATE_E_* transport code of a failed fetch.  Never
 * modifies out unless the long value was parsed successfully. */
int oc_update_fetch_changes_v2(const char *v2url, char *out, int outcap) {
    if (!v2url || !out || outcap <= 0) return -1;
    if (oc_strncmp(v2url, "http://", 7) != 0 &&
        oc_strncmp(v2url, "https://", 8) != 0)
        return -1;                             /* not an absolute URL */

    char *v2body = (char *)kmalloc(2048);
    if (!v2body) return OC_UPDATE_E_BUFSIZE;
    int blen = http_get_body(v2url, v2body, 2048);
    int rc = (blen >= 0) ? -1 : blen;          /* blen<0: transport error */
    if (blen >= 0) {
        char *tmp = (char *)kmalloc(1024);
        if (tmp) {
            if (oc_update_json_string(v2body, "changes", tmp, 1024) == 0 &&
                tmp[0] != 0) {
                int n = 0;
                while (tmp[n] && n < outcap - 1) { out[n] = tmp[n]; n++; }
                out[n] = 0;
                rc = 0;
            }
            kfree(tmp);
        } else {
            rc = OC_UPDATE_E_BUFSIZE;
        }
    }
    kfree(v2body);
    return rc;
}

/* Plan D step for a parsed base manifest: when body offers a usable
 * changes_v2_url, fetch the long changelog and upgrade changes in place.
 * Silent best-effort: any failure keeps the short value. */
static void update_apply_changes_v2(const char *body, char *changes,
                                    int changescap) {
    char v2url[192];
    if (oc_update_changes_v2_url(body, v2url, (int)sizeof(v2url)) != 0)
        return;
    (void)oc_update_fetch_changes_v2(v2url, changes, changescap);
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

int oc_check_update(oc_update_info_t *out) {
    if (out) {
        out->version[0] = 0;
        out->time[0] = 0;
        out->changes[0] = 0;
    }

    /* 1. read update_url.
     * Documented semantics: a MISSING CONFIG FILE is an error
     * (OC_UPDATE_E_CONFIG) so the user gets an explicit prompt; a
     * missing/empty key falls back to the built-in default URL. */
    char url[OC_CONFIG_VAL_MAX];
    int crc = oc_config_read(OC_CONFIG_KEY_URL, url, sizeof(url));
    if (crc == OC_CONFIG_E_NOFILE) return OC_UPDATE_E_CONFIG;
    if (crc == OC_CONFIG_E_NOKEY || crc == OC_CONFIG_E_TOOLONG) {
        oc_config_read_default(OC_CONFIG_KEY_URL, url, sizeof(url),
                               oc_config_default_url());
    } else if (crc != 0) {
        return OC_UPDATE_E_CONFIG;
    }

    /* 2. fetch manifest body (scheme decides HTTP vs HTTPS) */
    char body[2048];
    int blen = http_get_body(url, body, (int)sizeof(body));
    if (blen < 0) return blen;

    /* 3. parse JSON fields */
    if (!out) return OC_UPDATE_OK;
    int r1 = oc_update_json_string(body, "version", out->version,
                             (int)sizeof(out->version));
    int r2 = oc_update_json_string(body, "time", out->time,
                             (int)sizeof(out->time));
    int r3 = oc_update_json_string(body, "changes", out->changes,
                             (int)sizeof(out->changes));
    if (r3 == -8) {
        /* changes is display-only: a longer server-side changelog must
         * not fail the whole check.  Re-parse into a heap buffer and
         * truncate (heap, never the 4 KiB kernel thread stack). */
        char *tmp = kmalloc(1024);
        if (tmp) {
            if (oc_update_json_string(body, "changes", tmp, 1024) == 0) {
                oc_memcpy(out->changes, tmp, sizeof(out->changes) - 1);
                out->changes[sizeof(out->changes) - 1] = 0;
                r3 = 0;
            }
            kfree(tmp);
        }
        /* kmalloc failure or still unparsable: keep r3 != 0 so the
         * caller sees the error - never silently report success with
         * garbage in out->changes. */
    }
    if (r1 != 0 || out->version[0] == 0) return OC_UPDATE_E_JSON;
    if (r2 != 0 || r3 != 0) return OC_UPDATE_E_JSON;

    /* Plan D: the base manifest parsed.  When it offers a long-changelog
     * URL, upgrade the display-only changes value in place (best-effort,
     * never fails the check). */
    update_apply_changes_v2(body, out->changes, (int)sizeof(out->changes));

    /* 4. compare versions (exact string match, ASCII) */
    if (oc_strcmp(out->version, OC_UPDATE_CURRENT_VERSION) == 0) {
        oc_update_cache_check(OC_UPDATE_OK, out->version);
        return OC_UPDATE_OK;
    }
    oc_update_cache_check(OC_UPDATE_NEW, out->version);
    return OC_UPDATE_NEW;
}

/* Parse a non-negative decimal JSON number ("key": 12345).  Returns 0
 * on success, -1 when not found, -2 when the field is not a number. */
int oc_update_json_uint(const char *json, const char *key, u64 *out) {
    if (!json || !key || !out) return -1;
    int klen = 0;
    while (key[klen]) klen++;

    const char *p = json;
    while ((p = oc_strchr(p, '"')) != NULL) {
        p++;
        if (oc_strncmp(p, key, klen) == 0 && p[klen] == '"') {
            const char *q = p + klen + 1;
            while (*q == ' ' || *q == ':' || *q == '\t' || *q == '\r' ||
                   *q == '\n') q++;
            if (*q < '0' || *q > '9') return -2;
            u64 v = 0;
            while (*q >= '0' && *q <= '9') {
                v = v * 10u + (u64)(*q - '0');
                q++;
            }
            *out = v;
            return 0;
        }
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) p++;
            p++;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* WP-10u: package manifest check                                      */
/* ------------------------------------------------------------------ */

int oc_update_check_pkg(oc_update_pkg_info_t *out) {
    if (out) {
        out->info.version[0] = 0;
        out->info.time[0] = 0;
        out->info.changes[0] = 0;
        out->package_url[0] = 0;
        out->package_sha256[0] = 0;
        out->package_size = 0;
        out->have_package = 0;
    }

    char url[OC_CONFIG_VAL_MAX];
    int crc = oc_config_read(OC_CONFIG_KEY_URL, url, sizeof(url));
    if (crc == OC_CONFIG_E_NOFILE) return OC_UPDATE_E_CONFIG;
    if (crc == OC_CONFIG_E_NOKEY || crc == OC_CONFIG_E_TOOLONG) {
        oc_config_read_default(OC_CONFIG_KEY_URL, url, sizeof(url),
                               oc_config_default_url());
    } else if (crc != 0) {
        return OC_UPDATE_E_CONFIG;
    }

    char body[2048];
    int blen = http_get_body(url, body, (int)sizeof(body));
    if (blen < 0) return blen;
    if (!out) return OC_UPDATE_OK;

    int r1 = oc_update_json_string(body, "version", out->info.version,
                                   (int)sizeof(out->info.version));
    int r2 = oc_update_json_string(body, "time", out->info.time,
                                   (int)sizeof(out->info.time));
    int r3 = oc_update_json_string(body, "changes", out->info.changes,
                                   (int)sizeof(out->info.changes));
    if (r3 == -8) {
        /* display-only field: truncate via a heap buffer, never fail */
        char *tmp = kmalloc(1024);
        if (tmp) {
            if (oc_update_json_string(body, "changes", tmp, 1024) == 0) {
                oc_memcpy(out->info.changes, tmp, sizeof(out->info.changes) - 1);
                out->info.changes[sizeof(out->info.changes) - 1] = 0;
                r3 = 0;
            }
            kfree(tmp);
        }
    }
    if (r1 != 0 || out->info.version[0] == 0) return OC_UPDATE_E_JSON;
    if (r2 != 0 || r3 != 0) return OC_UPDATE_E_JSON;

    /* Plan D: upgrade to the long changelog when the server offers one
     * (display-only, best-effort, never fails the check). */
    update_apply_changes_v2(body, out->info.changes,
                            (int)sizeof(out->info.changes));

    /* package fields (optional; required by the `update` command) */
    int rp = oc_update_json_string(body, "package_url", out->package_url,
                                   (int)sizeof(out->package_url));
    int rs = oc_update_json_string(body, "package_sha256",
                                   out->package_sha256,
                                   (int)sizeof(out->package_sha256));
    int rz = oc_update_json_uint(body, "package_size", &out->package_size);
    out->have_package = (rp == 0 && rs == 0 && rz == 0 &&
                         out->package_url[0] != 0);

    /* cache for update --status */
    if (oc_strcmp(out->info.version, OC_UPDATE_CURRENT_VERSION) == 0) {
        oc_update_cache_check(OC_UPDATE_OK, out->info.version);
        return OC_UPDATE_OK;
    }
    oc_update_cache_check(OC_UPDATE_NEW, out->info.version);
    return OC_UPDATE_NEW;
}

const char *oc_check_update_strerror(int rc) {
    switch (rc) {
    case OC_UPDATE_OK:          return "up to date";
    case OC_UPDATE_NEW:         return "new version available";
    case OC_UPDATE_E_CONFIG:    return "config file missing or unreadable";
    case OC_UPDATE_E_PREFIX:    return "invalid URL prefix (must be http:// or https://)";
    case OC_UPDATE_E_DNS:       return "DNS resolution failed";
    case OC_UPDATE_E_CONNECT:   return "connect failed";
    case OC_UPDATE_E_TIMEOUT:   return "no response (timeout)";
    case OC_UPDATE_E_HTTP:      return "malformed HTTP response";
    case OC_UPDATE_E_JSON:      return "JSON parse failed";
    case OC_UPDATE_E_BUFSIZE:   return "response too large";
    default:                    return "unknown error";
    }
}

/* ---- asynchronous check (boot auto check) ------------------------- */

/* Print helper: kernel threads share the boot console (same pattern as
 * the spawn/multi test threads in kmain.c). */
static void au_print(const char *s) {
    oc_console_puts("[autoupdate] ");
    oc_console_puts(s);
    oc_console_puts("\n");
}

static void update_check_thread(void *arg) {
    (void)arg;

    /* Wait up to 10 s for the network link; skip (do not block, do not
     * fail the boot) when it never comes up. */
    u64 t0 = oc_timer_now_ms();
    while (net_get_link_status() != 1) {
        if (oc_timer_now_ms() - t0 > 10000) {
            oc_log_info("[autoupdate] skipped: network not ready");
            return;
        }
        for (int i = 0; i < 50; i++) sched_yield();   /* ~50 ms slice */
    }

    /* Give DHCP/ARP a moment to settle, then run the real check. */
    oc_update_info_t info;
    char line2[160];
    int rc = oc_check_update(&info);
    if (rc == OC_UPDATE_OK) {
        au_print("Current version is up to date.");
        oc_strcpy(line2, "Version: "); oc_strcat(line2, info.version);
        au_print(line2);
        oc_strcpy(line2, "Time: ");    oc_strcat(line2, info.time);
        au_print(line2);
        oc_log_info("[autoupdate] check complete: up to date");
    } else if (rc == OC_UPDATE_NEW) {
        au_print("New version available.");
        oc_strcpy(line2, "Version: "); oc_strcat(line2, info.version);
        au_print(line2);
        oc_strcpy(line2, "Time: ");    oc_strcat(line2, info.time);
        au_print(line2);
        if (info.changes[0]) {
            oc_strcpy(line2, "Changes: "); oc_strcat(line2, info.changes);
            au_print(line2);
        }
        oc_log_info("[autoupdate] check complete: new version available");
    } else {
        /* record to log + console, never block or panic */
        char line[96]; char num[12];
        oc_strcpy(line, "[autoupdate] check failed (code ");
        oc_u64_to_str((u64)(-rc), num);
        oc_strcat(line, num);
        oc_strcat(line, "): ");
        oc_strcat(line, oc_check_update_strerror(rc));
        oc_log_info(line);
        return;
    }
}

int oc_check_update_async(void) {
    tid_t tid = kthread_create(update_check_thread, NULL, "checkupdate",
                               TASK_PRIO_DEFAULT);
    return (int)tid;
}

/* ------------------------------------------------------------------ */
/* shell: checkupdate command                                          */
/* ------------------------------------------------------------------ */

/* checkupdate - run the update check now, print a full-English report.
 * All output strings are ASCII (0x20..0x7E) by design. */
int cmd_checkupdate(const char *args) {
    (void)args;
    oc_update_info_t info;
    oc_console_puts("checkupdate: reading /etc/opencube.conf\n");
    int rc = oc_check_update(&info);
    if (rc == OC_UPDATE_OK) {
        oc_console_puts("Current version is up to date.\n");
        oc_console_puts("Version: "); oc_console_puts(info.version); oc_console_puts("\n");
        oc_console_puts("Time: ");     oc_console_puts(info.time);    oc_console_puts("\n");
        return 0;
    }
    if (rc == OC_UPDATE_NEW) {
        oc_console_puts("New version available.\n");
        oc_console_puts("Version: "); oc_console_puts(info.version); oc_console_puts("\n");
        oc_console_puts("Time: ");    oc_console_puts(info.time);    oc_console_puts("\n");
        oc_console_puts("Changes: "); oc_console_puts(info.changes); oc_console_puts("\n");
        return 0;
    }
    oc_console_puts("checkupdate: ");
    oc_console_puts(oc_check_update_strerror(rc));
    oc_console_puts("\n");
    return 1;
}

/* ------------------------------------------------------------------ */
/* shell: checkupdate_test                                             */
/* ------------------------------------------------------------------ */

/* Unit-level self-test of the pieces that do not depend on the network
 * (URL scheme parsing + JSON field extraction, including failure paths),
 * then one live probe using the configured update_url. */
int cmd_checkupdate_test(const char *args) {
    (void)args;
    int pass = 0, total = 0;
    const char *ok;

    /* 1. https:// prefix -> TLS */
    total++;
    ok = "FAIL";
    {
        oc_update_url_t u;
        if (oc_update_url_parse("https://10.0.2.2:8443/update.json", &u) == 0 &&
            u.use_tls == 1 && u.port == 8443 &&
            oc_strcmp(u.host, "10.0.2.2") == 0 &&
            oc_strcmp(u.path, "/update.json") == 0) { ok = "PASS"; pass++; }
    }
    oc_console_puts("[checkupdate_test] https:// prefix -> TLS: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 2. http:// prefix -> plain HTTP */
    total++;
    ok = "FAIL";
    {
        oc_update_url_t u;
        if (oc_update_url_parse("http://10.0.2.2:8008/update.json", &u) == 0 &&
            u.use_tls == 0 && u.port == 8008) { ok = "PASS"; pass++; }
    }
    oc_console_puts("[checkupdate_test] http:// prefix -> HTTP: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 3. other prefix -> rejected */
    total++;
    ok = "FAIL";
    {
        oc_update_url_t u;
        if (oc_update_url_parse("ftp://10.0.2.2/update.json", &u) == OC_UPDATE_E_PREFIX)
            { ok = "PASS"; pass++; }
    }
    oc_console_puts("[checkupdate_test] ftp:// prefix rejected: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 4. JSON: valid manifest parses all three fields */
    total++;
    ok = "FAIL";
    {
        static const char *doc =
            "{\n  \"version\": \"WP-10\",\n  \"time\": \"2026-10-15\","
            "\n  \"changes\": \"driver optimization + config file\"\n}\n";
        char v[32], t[32], c[128];
        if (oc_update_json_string(doc, "version", v, sizeof(v)) == 0 &&
            oc_strcmp(v, "WP-10") == 0 &&
            oc_update_json_string(doc, "time", t, sizeof(t)) == 0 &&
            oc_strcmp(t, "2026-10-15") == 0 &&
            oc_update_json_string(doc, "changes", c, sizeof(c)) == 0 &&
            oc_strcmp(c, "driver optimization + config file") == 0)
            { ok = "PASS"; pass++; }
    }
    oc_console_puts("[checkupdate_test] JSON manifest parses: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 5. JSON: broken manifest is rejected */
    total++;
    ok = "FAIL";
    {
        static const char *bad = "this is not json at all";
        char v[32];
        if (oc_update_json_string(bad, "version", v, sizeof(v)) != 0)
            { ok = "PASS"; pass++; }
    }
    oc_console_puts("[checkupdate_test] broken JSON rejected: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 6. 300-byte changes value parses into a 1 KiB buffer (the heap
     * truncation path of oc_check_update relies on this) */
    total++;
    ok = "FAIL";
    {
        char doc[512];
        char big[301];
        for (int i = 0; i < 300; i++) big[i] = 'a';
        big[300] = 0;
        oc_strcpy(doc, "{\"version\":\"V\",\"time\":\"T\",\"changes\":\"");
        oc_strcat(doc, big);
        oc_strcat(doc, "\"}");
        char c[1024];
        if (oc_update_json_string(doc, "changes", c, sizeof(c)) == 0 &&
            (int)oc_strlen(c) == 300) { ok = "PASS"; pass++; }
    }
    oc_console_puts("[checkupdate_test] 300-byte changes parses into 1 KiB buffer: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 7. the old 128-byte buffer rejects a 300-byte value with -8 --
     * the exact failure mode that broke checkupdate against the WP-10a
     * manifest (174-byte changes) and motivated the truncation fix */
    total++;
    ok = "FAIL";
    {
        char doc[512];
        char big[301];
        for (int i = 0; i < 300; i++) big[i] = 'a';
        big[300] = 0;
        oc_strcpy(doc, "{\"version\":\"V\",\"time\":\"T\",\"changes\":\"");
        oc_strcat(doc, big);
        oc_strcat(doc, "\"}");
        char c[128];
        if (oc_update_json_string(doc, "changes", c, sizeof(c)) == -8)
            { ok = "PASS"; pass++; }
    }
    oc_console_puts("[checkupdate_test] 300-byte changes rejected by 128-byte buffer (-8): "); oc_console_puts(ok); oc_console_puts("\n");

    /* 8. Plan D: changes_v2_url extracts from a manifest that carries it
     * (the field the server adds so new kernels can upgrade to the long
     * changelog while old kernels keep reading the short one) */
    total++;
    ok = "FAIL";
    {
        static const char *doc =
            "{\"version\":\"V\",\"time\":\"T\","
            "\"changes\":\"short\","
            "\"changes_v2_url\":\"https://host.example/OpenCubeOS/update-v2.json\"}";
        char u[192];
        if (oc_update_changes_v2_url(doc, u, (int)sizeof(u)) == 0 &&
            oc_strcmp(u, "https://host.example/OpenCubeOS/update-v2.json") == 0)
            { ok = "PASS"; pass++; }
    }
    oc_console_puts("[checkupdate_test] changes_v2_url extracts from manifest: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 9. Plan D failure paths: a manifest without changes_v2_url yields
     * -1, and oc_update_fetch_changes_v2 rejects non-absolute URLs
     * without touching the output buffer (no network access needed) */
    total++;
    ok = "FAIL";
    {
        static const char *doc =
            "{\"version\":\"V\",\"time\":\"T\",\"changes\":\"short\"}";
        char u[192];
        char out[64];
        oc_strcpy(out, "keep");
        if (oc_update_changes_v2_url(doc, u, (int)sizeof(u)) == -1 &&
            oc_update_fetch_changes_v2("ftp://host.example/v2.json",
                                       out, (int)sizeof(out)) == -1 &&
            oc_strcmp(out, "keep") == 0 &&
            oc_update_fetch_changes_v2("notaurl", out, (int)sizeof(out)) == -1 &&
            oc_strcmp(out, "keep") == 0)
            { ok = "PASS"; pass++; }
    }
    oc_console_puts("[checkupdate_test] changes_v2 missing/rejected URLs keep short changes: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 10. live probe with the configured update_url (HTTP or HTTPS) */
    total++;
    {
        oc_update_info_t info;
        int rc = oc_check_update(&info);
        ok = (rc == OC_UPDATE_OK || rc == OC_UPDATE_NEW) ? "PASS" : "FAIL";
        if (rc == OC_UPDATE_OK || rc == OC_UPDATE_NEW) pass++;
        oc_console_puts("[checkupdate_test] live probe (");
        oc_console_puts(oc_check_update_strerror(rc));
        oc_console_puts("): ");
        oc_console_puts(ok);
        oc_console_puts("\n");
    }

    char line[64]; char num[12];
    oc_strcpy(line, "[checkupdate_test] ");
    oc_u64_to_str((u64)pass, num); oc_strcat(line, num);
    oc_strcat(line, "/");
    oc_u64_to_str((u64)total, num); oc_strcat(line, num);
    oc_strcat(line, pass == total ? " PASS" : " FAIL");
    oc_console_puts(line);
    oc_console_puts("\n");
    return (pass == total) ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* L1 extension surface (kernel/ext.h)                                 */
/* ------------------------------------------------------------------ */

int oc_ext_check_update(oc_ext_update_info_t *out) {
    oc_update_info_t info;
    int rc = oc_check_update(&info);
    if (out) {
        for (int i = 0; i < (int)sizeof(out->version); i++)
            out->version[i] = info.version[i];
        for (int i = 0; i < (int)sizeof(out->time); i++)
            out->time[i] = info.time[i];
        for (int i = 0; i < (int)sizeof(out->changes); i++)
            out->changes[i] = info.changes[i];
    }
    return rc;
}

int oc_ext_check_update_async(void) {
    return oc_check_update_async();
}
