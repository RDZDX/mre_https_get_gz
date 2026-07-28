#ifndef _VRE_APP_WIZARDTEMPLATE_
#define _VRE_APP_WIZARDTEMPLATE_

#include "vmio.h"
#include "vmgraph.h"
#include "vmstdlib.h"
#include <string.h>
#include "vmchset.h"
#include "vmpromng.h"
#include "stdint.h"
#include "vmsys.h"
#include <stdlib.h>
#include "vmres.h"
#include "vmtimer.h"
#include "vmhttp.h"
#include "vmsock.h"
#include <stdio.h>
#include "vmsim.h"
#include <ctype.h>

#include "bearssl.h"
//#include "certificates.h"
//#include "thread.h"

//VMINT layer_hdl[1];
extern VMINT layer_hdl[1];

typedef struct
{
    VMCHAR host[128];
    VMCHAR path[512];
    VMINT port;
} https_url_t;

typedef struct
{
    VMINT tcp_handle;

    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context ioc;

    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];

} https_context_t;

//static https_context_t https_ctx;
extern https_context_t https_ctx;

void handle_sysevt(VMINT message, VMINT param);
void handle_keyevt(VMINT event, VMINT keycode);
void save_text(VMINT state, VMWSTR text);
void let_download(void);
void break_download(VMINT handle);
static VMINT download_process(const VMCHAR* url,
                               VM_HTTP_PROXY_TYPE apn, HTTP_METHOD mth,
                               VMINT* handle,
                               void (*download_callback)(VMINT bResponse, void* pSession),
                               void (*notify_callback)(VMINT state, VMINT param, void* session));
static void download_callback(VMINT bResponse, void* pSession);
static void notify_callback(VMINT state, VMINT param, void* session);
VMINT read_data(VMWSTR file_name, VMINT offset, VMINT num, void* data);
VMINT string_width(VMWCHAR *whead, VMWCHAR *wtail);
void display_text_line(VMUINT8 *disp_buf, VMSTR str, VMINT x, VMINT y, VMINT width, VMINT height, VMINT betlines, VMINT startLine, VMINT color, VMBOOL fix_pos);
static void fill_white(void);
void extract_end_text(char *result_data, const char *inp_data, char separator);
VMINT get_con_handl(void);

VMINT https_parse_url(const VMCHAR *url, https_url_t *out);
VMINT https_connect(https_url_t *url);
//VMINT https_send_request(VMINT sock, https_url_t *url, VMUINT start, VMUINT end);
VMINT https_send_request(https_url_t *url);
//VMINT https_receive_file(VMINT sock, const VMCHAR *file_name);
VMINT https_receive_file(VMFILE file);
VMINT https_download(const VMCHAR *url);

#endif

