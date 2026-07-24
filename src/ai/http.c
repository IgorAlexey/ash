#include <string.h>

#include <curl/curl.h>

#include "ash/ai/http.h"
#include "ash/base/poison.h"

struct ash_http {
    CURL              *easy;
    CURLM             *multi;
    struct curl_slist *hdrs;
    ash_http_sink      sink;
    void              *ud;
    int                added;
};

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct ash_http *h = userdata;
    size_t n = size * nmemb;
    if (h->sink && n)
        h->sink(h->ud, ptr, n);
    return n;
}

ash_status ash_http_global_init(void)
{
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return ash_fail(ASH_ERR_IO, "curl_global_init failed");
    return ASH_OK;
}

void ash_http_global_cleanup(void)
{
    curl_global_cleanup();
}

ash_status ash_http_start(ash_http **out, ash_arena *arena, const char *url,
                          const char *body, size_t body_len,
                          const char *const *headers,
                          ash_http_sink sink, void *ud)
{
    if (out == NULL || arena == NULL || url == NULL)
        return ash_fail(ASH_ERR_RANGE, "ash_http_start: bad arguments");

    struct ash_http *h = ash_new(arena, struct ash_http);
    memset(h, 0, sizeof *h);
    h->sink = sink;
    h->ud = ud;

    h->easy = curl_easy_init();
    h->multi = curl_multi_init();
    if (h->easy == NULL || h->multi == NULL) {
        ash_http_close(h);
        return ash_fail(ASH_ERR_IO, "curl init failed");
    }

    curl_easy_setopt(h->easy, CURLOPT_URL, url);
    curl_easy_setopt(h->easy, CURLOPT_POST, 1L);
    if (body != NULL) {
        curl_easy_setopt(h->easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_len);
        if (curl_easy_setopt(h->easy, CURLOPT_COPYPOSTFIELDS, body) != CURLE_OK) {
            ash_http_close(h);
            return ash_fail(ASH_ERR_NOMEM, "CURLOPT_COPYPOSTFIELDS failed");
        }
    } else {
        curl_easy_setopt(h->easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)0);
        curl_easy_setopt(h->easy, CURLOPT_POSTFIELDS, "");
    }
    if (headers != NULL) {
        for (size_t i = 0; headers[i] != NULL; i++) {
            struct curl_slist *n = curl_slist_append(h->hdrs, headers[i]);
            if (n == NULL) {
                ash_http_close(h);
                return ash_fail(ASH_ERR_NOMEM, "curl_slist_append failed");
            }
            h->hdrs = n;
        }
        curl_easy_setopt(h->easy, CURLOPT_HTTPHEADER, h->hdrs);
    }
    curl_easy_setopt(h->easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(h->easy, CURLOPT_WRITEDATA, h);
    curl_easy_setopt(h->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h->easy, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(h->easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(h->easy, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(h->easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(h->easy, CURLOPT_LOW_SPEED_TIME, 120L);

    if (curl_multi_add_handle(h->multi, h->easy) != CURLM_OK) {
        ash_http_close(h);
        return ash_fail(ASH_ERR_IO, "curl_multi_add_handle failed");
    }
    h->added = 1;
    *out = h;
    return ASH_OK;
}

ash_status ash_http_perform(ash_http *h, int *running, long *http_status)
{
    int still = 0;
    CURLMcode mc = curl_multi_perform(h->multi, &still);
    if (mc != CURLM_OK)
        return ash_fail(ASH_ERR_IO, "curl_multi_perform: %s", curl_multi_strerror(mc));
    *running = still;

    int msgs = 0;
    CURLMsg *m;
    while ((m = curl_multi_info_read(h->multi, &msgs)) != NULL) {
        if (m->msg != CURLMSG_DONE)
            continue;
        if (m->data.result != CURLE_OK)
            return ash_fail(ASH_ERR_IO, "transfer failed: %s",
                            curl_easy_strerror(m->data.result));
        if (http_status != NULL) {
            long code = 0;
            curl_easy_getinfo(m->easy_handle, CURLINFO_RESPONSE_CODE, &code);
            *http_status = code;
        }
    }
    return ASH_OK;
}

ash_status ash_http_wait(ash_http *h, int extra_fd, int timeout_ms,
                         int *extra_readable)
{
    if (h == NULL)
        return ash_fail(ASH_ERR_RANGE, "ash_http_wait: bad arguments");

    struct curl_waitfd wf;
    unsigned nwf = 0;
    if (extra_fd >= 0) {
        wf.fd = extra_fd;
        wf.events = CURL_WAIT_POLLIN;
        wf.revents = 0;
        nwf = 1;
    }

    int nf = 0;
    CURLMcode mc = curl_multi_poll(h->multi, nwf ? &wf : NULL, nwf, timeout_ms, &nf);
    if (mc != CURLM_OK)
        return ash_fail(ASH_ERR_IO, "curl_multi_poll: %s", curl_multi_strerror(mc));

    if (extra_readable != NULL)
        *extra_readable = (nwf && (wf.revents & CURL_WAIT_POLLIN)) ? 1 : 0;
    return ASH_OK;
}

ash_status ash_http_run(ash_http *h, long *http_status)
{
    int running = 1;
    while (running) {
        int numfds = 0;
        CURLMcode mc = curl_multi_poll(h->multi, NULL, 0, 1000, &numfds);
        if (mc != CURLM_OK)
            return ash_fail(ASH_ERR_IO, "curl_multi_poll: %s", curl_multi_strerror(mc));
        ASH_TRY(ash_http_perform(h, &running, http_status));
    }
    return ASH_OK;
}

void ash_http_close(ash_http *h)
{
    if (h == NULL)
        return;
    if (h->added && h->multi != NULL && h->easy != NULL)
        curl_multi_remove_handle(h->multi, h->easy);
    if (h->easy != NULL)
        curl_easy_cleanup(h->easy);
    if (h->multi != NULL)
        curl_multi_cleanup(h->multi);
    if (h->hdrs != NULL)
        curl_slist_free_all(h->hdrs);
    h->easy = NULL;
    h->multi = NULL;
    h->hdrs = NULL;
    h->added = 0;
}
