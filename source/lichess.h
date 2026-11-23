// Lichess HTTP client: one worker thread running curl_multi; results are polled from the UI thread.
#pragma once
#include "app.h"

typedef enum { LIM_RESPONSE, LIM_LINE, LIM_END } LiMsgKind;

typedef struct LiMsg {
    struct LiMsg *next;
    int kind;       // LiMsgKind
    int req;        // request id returned by li_request
    int tag;        // caller-chosen tag
    long http;      // HTTP status (RESPONSE / END)
    int curl_rc;    // CURLcode (0 = ok)
    char *data;     // body (RESPONSE) or one NDJSON line (LINE); NUL terminated; owned by the message
    size_t len;
    char err[64];   // curl error text for END with curl_rc != 0
} LiMsg;

bool li_start(void);
void li_stop(void);
void li_set_token(const char *token);
void li_set_verify(bool verify);
// stream = true: the response is delivered line by line (NDJSON) and stays open until it ends or li_cancel.
int  li_request(int tag, const char *method, const char *path, const char *body, bool stream);
void li_cancel(int req);
LiMsg *li_poll(void);
void li_msg_free(LiMsg *m);
void li_url_encode(const char *in, char *out, size_t n);
int  li_active_count(void);
