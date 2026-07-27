#include "mre_http_get.h"
#include "thread.h"

#define CHUNK_SIZE 2048 // 200 1024 2048 4096 8192

VMINT layer_hdl[1];
VMUINT8* screenbuf = NULL;
VMINT nscreen_width = -1;
VMINT nscreen_height = -1;
VMINT filledDsplByLines = 0;
VMINT drv;
VMBOOL second = VM_FALSE;
VMBOOL third = VM_FALSE;
VMBOOL flightMode = VM_FALSE;
VMWCHAR title_name[22] = {0};
VMWCHAR title_name1[22] = {0};
VMCHAR new_data[1201] = {0};
static VMUINT file_size = 0;
static VMCHAR file_name[128] = {0};
static VMFILE file_hdl = -1;
static VMINT http_hdl[12] = {-1};
VMCHAR download_fname[128];
VMCHAR n_without_ext[128];
VMCHAR extension_name[5];

static VMBOOL connected = VM_FALSE;
static VMBOOL network_err = VM_FALSE;

https_context_t https_ctx;

static int http_status = 0;
static int header_is_chunked = 0;
static int header_is_gzip = 0;
static int content_length = -1;

static https_url_t g_url;
//static int counter; //?????????????????????????????
static VMINT main_timer_id = -1;
static VMBOOL new_download_request = VM_FALSE;
static VMFILE g_file = -1;

//static volatile VMINT tcp_event = -1; //???????????????
//static volatile VMINT tcp_count = 0;  //???????????????
//static VMINT dbg_timer = -1; //?????????????????????????
//static VMINT sock = -1; //?????????????????
vm_time_t t;
static char redirect_url[512];
static int redirect_count = 0;
VMBOOL resume_download = VM_FALSE;

typedef struct
{
    char host[256];
    char path[512];
    int port;
} http_url_t;

static http_url_t g_http_url;
static int http_is_gzip = 0;
static VMUINT start_size = 0;
volatile VMBOOL cancel_download = VM_FALSE;

VMINT http_parse_url(const char *url, http_url_t *out);
static VMBOOL is_https_url(const char *url);
static VMBOOL is_http_url(const char *url);
void build_download_name(const char *path, int gzip, char *out, int out_size);
int safe_atoi(const char *s);
void log_text(const char *msg);
static int sock_read(void *ctx, unsigned char *buf, size_t len);
static int sock_write(void *ctx, const unsigned char *buf, size_t len);
static int read_headers(br_sslio_context *ioc);
static VMINT https_receive_chunked(VMFILE file);
static int https_read_line(br_sslio_context *ioc, char *buf, int maxlen);
static unsigned long is_leap(int y);
static unsigned long bearssl_days(int year, int month, int day);
void tcp_callback(VMINT handle, VMINT event);
void timer(VMINT tid);
void https_worker(void);

void vm_main(void)
{
    vm_reg_sysevt_callback(handle_sysevt);
    vm_reg_keyboard_callback(handle_keyevt);

    vm_font_set_font_size(VM_SMALL_FONT);

    nscreen_width = vm_graphic_get_screen_width();
    nscreen_height = vm_graphic_get_screen_height();

    vm_ascii_to_ucs2(title_name, (strlen("Input URL:") + 1) * 2, "Input URL:");
    vm_ascii_to_ucs2(title_name1, (strlen("https://") + 1) * 2, "https://");

    vm_input_set_editor_title(title_name);

    if (flightMode == VM_FALSE)
        vm_input_text3(title_name1, 1200, 8, save_text);

    drv = vm_get_removable_driver();

    if (drv < 0)
    {
        drv = vm_get_system_driver();
    }

    thread_init();
    thread_create(0xFFFF, https_worker);
    main_timer_id = vm_create_timer(100, timer);

}

void handle_sysevt(VMINT message, VMINT param) {

    switch (message) {
    case VM_MSG_CREATE:
    case VM_MSG_ACTIVE:
        layer_hdl[0] = vm_graphic_create_layer(0, 0, vm_graphic_get_screen_width(), vm_graphic_get_screen_height(), -1);
        vm_graphic_set_clip(0, 0, vm_graphic_get_screen_width(), vm_graphic_get_screen_height());
        break;

    case VM_MSG_PAINT:
        vm_switch_power_saving_mode(turn_off_mode);
        screenbuf = vm_graphic_get_layer_buffer(layer_hdl[0]);
        if (flightMode == VM_TRUE) {
            fill_white();
            log_text("Please turn Flight mode off !");
        }
        break;

    case VM_MSG_INACTIVE:
        vm_switch_power_saving_mode(turn_on_mode);
        if (layer_hdl[0] != -1)
            vm_graphic_delete_layer(layer_hdl[0]);
        break;

    case VM_MSG_QUIT:
        if (layer_hdl[0] != -1)
            vm_graphic_delete_layer(layer_hdl[0]);
        break;
    }
}

void handle_keyevt(VMINT event, VMINT keycode) {

    if (event == VM_KEY_EVENT_UP && keycode == VM_KEY_RIGHT_SOFTKEY) {
        if (layer_hdl[0] != -1) {
            vm_graphic_delete_layer(layer_hdl[0]);
            layer_hdl[0] = -1;
        }
        vm_exit_app();
    }

    if (event == VM_KEY_EVENT_UP && keycode == VM_KEY_LEFT_SOFTKEY) {
        cancel_download = VM_TRUE;
        break_download(get_con_handl());
    }

}

void save_text(VMINT state, VMWSTR text) {

    VMINT lenght = wstrlen(text);

    if (state == VM_INPUT_OK && lenght > 0) {
        fill_white();

        vm_ucs2_to_ascii(new_data, lenght + 1, text);
        log_text(new_data);

if (!is_https_url(new_data))
{
    http_parse_url(new_data, &g_http_url);
}

let_download();

    } else {
        vm_exit_app();
    }
}

static void fill_white(void) {

    vm_graphic_color color;
    color.vm_color_565 = VM_COLOR_WHITE;
    vm_graphic_setcolor(&color);
    vm_graphic_fill_rect_ex(layer_hdl[0], 0, 0, vm_graphic_get_screen_width(), vm_graphic_get_screen_height());
    vm_graphic_flush_layer(layer_hdl, 1);
    filledDsplByLines = 0;

}

VMINT string_width(VMWCHAR *whead, VMWCHAR *wtail) {

    VMWCHAR *wtemp = NULL;
    VMINT width = 0;
    if (whead == NULL || wtail == NULL)
        return 0;
    wtemp = (VMWCHAR *)vm_malloc((wtail - whead) * 2 + 2);
    if (wtemp == NULL)
        return 0;
    memset(wtemp, 0, (wtail - whead) * 2 + 2);
    memcpy(wtemp, whead, (wtail - whead) * 2);
    width = vm_graphic_get_string_width(wtemp);
    vm_free(wtemp);
    return width;

}

void display_text_line(VMUINT8 *disp_buf, VMSTR str, VMINT x, VMINT y, VMINT width, VMINT height, VMINT betlines, VMINT startLine, VMINT color, VMBOOL fix_pos) {

    VMWCHAR *ucstr;
    VMWCHAR *ucshead;
    VMWCHAR *ucstail;
    VMINT is_end = FALSE;
    VMINT nheight = y;
    VMINT nlines = 0;

    if (y == 0) { fill_white(); }

    if (str == NULL || disp_buf == NULL || betlines < 0) { return; }

    VMINT nline_height = vm_graphic_get_character_height() + betlines;

    if (third == VM_TRUE && fix_pos == VM_FALSE) {
        nheight = nheight + nline_height;
        third = VM_FALSE;
    }

    if (fix_pos == VM_TRUE) {
        vm_graphic_fill_rect(screenbuf, 0, filledDsplByLines, nscreen_width, nline_height, VM_COLOR_WHITE, VM_COLOR_WHITE);
        third = VM_TRUE;
    }

    ucstr = (VMWCHAR*)vm_malloc(2 * (strlen(str) + 1));
    if (ucstr == NULL) { return; }

    if (0 != vm_ascii_to_ucs2(ucstr, 2 * (strlen(str) + 1), str)) {
        vm_free(ucstr);
        return;
    }

    ucshead = ucstr;
    ucstail = ucshead + 1;

    while (is_end == FALSE) {
        if (nheight > y + height) {
            fill_white();
            nheight = 0;
        }

        while (1) {
            if (string_width(ucshead, ucstail) <= width) {
                ucstail++;
            } else {
                nlines++;
                ucstail--;
                break;
            }
            if (0 == vm_wstrlen(ucstail)) {
                is_end = TRUE;
                nlines++;
                break;
            }
        }

        if (nlines >= startLine) {
            vm_graphic_textout(disp_buf, x, nheight, ucshead, (ucstail - ucshead), (VMUINT16)(color));
            vm_graphic_flush_layer(layer_hdl, 1);
            if (fix_pos == VM_FALSE) { nheight += nline_height; }
            filledDsplByLines = nheight;
        }

        ucshead = ucstail;
        ucstail++;
    }

    vm_free(ucstr);
}

void extract_end_text(char *result_data, const char *inp_data, char separator) {

    int i = 0;
    int last_sep_pos = -1;

    while (inp_data[i] != '\0') {
        if (inp_data[i] == separator) {
            last_sep_pos = i;
        }
        i++;
    }

    i = 0;
    int j = last_sep_pos + 1;
    while (inp_data[j] != '\0') {
        result_data[i++] = inp_data[j++];
    }
    result_data[i] = '\0';

}

void break_download(VMINT handle) {

//    VMCHAR text[255] = {0};
//    if (!vm_cancel_asyn_http_req(handle)) {
//        http_hdl[0] = -1;
//        sprintf(text, "Interrupt HTTP download");
//        log_text(text);
//    }

if (handle >= 0 &&
    !vm_cancel_asyn_http_req(handle))
{
    http_hdl[0] = -1;
    log_text("Interrupt HTTP download");
}


}

static VMINT download_process(const VMCHAR* url,
                               VM_HTTP_PROXY_TYPE apn, HTTP_METHOD mth,
                               VMINT* handle,
                               void (*download_callback)(VMINT bResponse, void* pSession),
                               void (*notify_callback)(VMINT state, VMINT param, void* session)) {

    asyn_http_req_t req;
    VMINT ret;
    http_head_t head[2];
    VMCHAR full_name[128];
//    VMINT find_hdl;
//    VMUINT start_size = 0;
//    struct vm_fileinfo_t info;
//    VMCHAR text[255] = {0};
    VMWCHAR w_full_name[258];

memset(download_fname, 0, sizeof(download_fname));

    extract_end_text(download_fname, url, '/');

    /* Bug 3 fix: no path segment → default to index.html like wget */
    if (strlen(download_fname) == 0 || strlen(url) == strlen(download_fname)) {
//        strncpy(download_fname, "index.html", sizeof(download_fname) - 1);
        strncpy(download_fname, "download.bin", sizeof(download_fname) - 1);
        download_fname[sizeof(download_fname) - 1] = '\0';
    }

    extract_end_text(extension_name, download_fname, '.');
    if (strlen(extension_name) == 0 || strcmp(extension_name, download_fname) == 0) {
        strncpy(extension_name, "gz", sizeof(extension_name) - 1);
        extension_name[sizeof(extension_name) - 1] = '\0';
        strncat(download_fname, ".gz", sizeof(download_fname) - strlen(download_fname) - 1);
    }

    if (second == VM_FALSE) {

       log_text("Connecting...");
       second = VM_TRUE;
    }

char *dot;

dot = strrchr(download_fname, '.');

if (dot)
{
    size_t len = dot - download_fname;

if (len >= sizeof(n_without_ext))
{
    len = sizeof(n_without_ext) - 1;
}

memcpy(
    n_without_ext,
    download_fname,
    len);

    n_without_ext[len] = '\0';
}
else
{
    strncpy(
        n_without_ext,
        download_fname,
        sizeof(n_without_ext) - 1);

    n_without_ext[sizeof(n_without_ext) - 1] = '\0';
}

//sprintf(
//    full_name,
//    "%c:\\%s.tmp",
//    drv,
//    n_without_ext);

snprintf(
    full_name,
    sizeof(full_name),
    "%c:\\%s.tmp",
    drv,
    n_without_ext);

vm_ascii_to_ucs2(
    w_full_name,
    (strlen(full_name) + 1) * 2,
    full_name);

resume_download = VM_FALSE;
start_size = 0;

VMFILE fp;
VMUINT temp_size = 0;

fp = vm_file_open(
        w_full_name,
        MODE_READ,
        TRUE);

if (fp >= 0)
{
    if (vm_file_getfilesize(fp, &temp_size) == 0)
    {
        start_size = temp_size;

        if (start_size > 0)
        {
            resume_download = VM_TRUE;

            char msg[64];

            sprintf(
                msg,
                "resume from %u",
                start_size);

            log_text(msg);
        }
    }

    vm_file_close(fp);
}


    req.req_method = mth;
    req.use_proxy = apn;

    req.http_request = (http_request_t*)vm_calloc(sizeof(http_request_t));
    if (NULL == req.http_request) {
        return -1;
    }

//    if (strncmp(url, "http://", strlen("http://"))) {
//       strcat(req.http_request->url, "http://");
//    }
//    strcat(req.http_request->url, url);


if (strncmp(url, "http://", strlen("http://")))
{
    snprintf(
        req.http_request->url,
        sizeof(req.http_request->url),
        "http://%s",
        url);
}
else
{
    snprintf(
        req.http_request->url,
        sizeof(req.http_request->url),
        "%s",
        url);
}


if (file_size > 0 &&
    start_size >= file_size)
{
    log_text("File already complete");

//    vm_file_delete(w_full_name); //delete the .tmp file ??????????????????????????????

    resume_download = VM_FALSE;
    start_size = 0;
    file_size = 0;

    vm_free(req.http_request);

    return -1;
}

if (resume_download)
{
    VMUINT end_pos =
        ((start_size + CHUNK_SIZE - 1) >= file_size)
            ? (file_size - 1)
            : (start_size + CHUNK_SIZE - 1);

    sprintf(head[0].name, "RANGE");

    sprintf(
        head[0].value,
        "bytes=%u-%u",
        start_size,
        end_pos);
}

else if (file_size == 0)
{
    sprintf(head[0].name, "RANGE");
    sprintf(head[0].value, "bytes=-1");
}
else
{
    sprintf(head[0].name, "RANGE");

VMUINT end_pos;

if (start_size >= file_size)
{
    end_pos = file_size - 1;
}
else
{
    end_pos =
        ((start_size + CHUNK_SIZE - 1) >= file_size)
            ? (file_size - 1)
            : (start_size + CHUNK_SIZE - 1);
}


sprintf(
    head[0].value,
    "bytes=%u-%u",
    start_size,
    end_pos);

}

    /* Header 1: Accept-Encoding */
    sprintf(head[1].name, "Accept-Encoding");
    sprintf(head[1].value, "gzip");

    req.http_request->nhead = 2;
    req.http_request->heads = head;

//char dbg[128];

//sprintf(
//    dbg,
//    "Range=%s",
//    head[0].value);

//log_text(dbg);


//sprintf(
//    dbg,
//    "resume=%d start=%u size=%u",
//    resume_download,
//    start_size,
//    file_size);

//log_text(dbg);

//log_text(head[0].value);

    ret = vm_asyn_http_req(&req, download_callback, notify_callback);

    if (ASYN_HTTP_REQ_ACCEPT_SUCCESS == ret) {
        ret = vm_get_asyn_http_req_handle(&req, handle);
    }

    vm_free(req.http_request);
    return ret;
}

static void download_callback(VMINT bResponse, void* pSession) {

    VMCHAR head[255] = {0};
    VMCHAR text[255] = {0};
    VMUINT written;
    VMWCHAR w_file_name[258] = {0};
    VMWCHAR w_file_name1[258] = {0};

    if (0 == bResponse) {
        http_session_t* session = (http_session_t*)pSession;

//        char textrr[128];

//        sprintf(textrr, "code=%d body=%d", session->res_code, session->nresbody);

//        log_text(textrr);

switch (session->res_code)
{
    case 400:
        log_text("Bad request");
        return;

    case 404:
        log_text("File not found");
        return;

    case 403:
        log_text("Access denied");
        return;

    case 401:
        log_text("Authentication required");
        return;

    case 416:
        log_text("Invalid range");
        return;

    case 500:
        log_text("Server error");
        return;
}

        /* Server error */
        if (0 > session->res_code || 500 == session->res_code) {
            strcpy(text, "Server Error");
            log_text(text);
            return;
        }

        /* Redirect — follow Location header */

        if (file_size == 0) {
            http_is_gzip = 0;

            if (get_http_head(session, "Content-Encoding", head) == 0) {
                if (!strcmp(head, "gzip")) {
                    http_is_gzip = 1;
                }
            }
        }

        if (301 == session->res_code || 302 == session->res_code) {
            if (0 == get_http_head(session, "Location", head)) {
                strncpy(new_data, head, sizeof(new_data) - 1);

                new_data[sizeof(new_data) - 1] = '\0';

                file_size = 0;
                second = VM_FALSE;

                memset(download_fname, 0, sizeof(download_fname));
                memset(n_without_ext, 0, sizeof(n_without_ext));
                memset(extension_name, 0, sizeof(extension_name));

                log_text("Redirecting...");

                if (is_https_url(new_data)) {
                    let_download();
                } else if (is_http_url(new_data)) {
                    http_parse_url(new_data, &g_http_url);

//                    char texttt[64];
//                    sprintf(texttt, "file_size=%u", file_size);

//                    log_text(texttt);

                    let_download();
                }

                else if (new_data[0] == '/') {
                    char url[1024];

                    snprintf(url, sizeof(url), "http://%s%s", g_http_url.host, new_data);

                    strncpy(new_data, url, sizeof(new_data) - 1);

                    new_data[sizeof(new_data) - 1] = '\0';

                    http_parse_url(new_data, &g_http_url);

                    let_download();
                } else {
                    char path[512];
                    char* last;
                    char url[1024];

                    strncpy(path, g_http_url.path, sizeof(path) - 1);

                    path[sizeof(path) - 1] = '\0';

                    last = strrchr(path, '/');
                    if (last)
                        *(last + 1) = '\0';
                    else
                        strcpy(path, "/");

                    strncat(path, new_data, sizeof(path) - strlen(path) - 1);

                    snprintf(url, sizeof(url), "http://%s%s", g_http_url.host, path);

                    strncpy(new_data, url, sizeof(new_data) - 1);

                    new_data[sizeof(new_data) - 1] = '\0';

                    http_parse_url(new_data, &g_http_url);

                    let_download();
                }

            } else {
                log_text("Redirect failed: no Location");
            }

            return;
        }

        /* WAP proxy reconnect */
        if (get_http_head(session, "Content-Type", head)) {
            strcpy(text, "Failed to obtain Content-Type");
            log_text("Failed to obtain Content-Type");
            return;
        }
        if (!strncmp(head, "text/vnd.wap.wml", strlen("text/vnd.wap.wml"))) {
            strcpy(text, "CMWAP Reconnect");
            log_text(text);
            let_download();
            return;
        }

        sprintf(file_name, "%c:\\%s.tmp", drv, n_without_ext);

        vm_ascii_to_ucs2(w_file_name, (strlen(file_name) + 1) * 2, file_name);

/* First request — determine total file size */
if (0 == file_size)
{
    /*
     * NEW DOWNLOAD:
     * Create fresh .tmp file.
     */
    if (!resume_download)
    {
       file_hdl =
            vm_file_open(
                w_file_name,
                MODE_CREATE_ALWAYS_WRITE,
                TRUE);

        if (file_hdl < 0)
        {
            log_text("Cannot create tmp file");
            return;
        }
    }

    /*
     * RESUME DOWNLOAD:
     * Keep existing .tmp file untouched.
     */

if (resume_download &&
    session->res_code != 206)
{
    log_text("Server does not support resume");

    vm_file_delete(w_file_name);

    resume_download = VM_FALSE;
    start_size = 0;
    file_size = 0;

    let_download();
    return;
}

    if (0 == get_http_head(session, "Content-Range", head))
    {
//        char textoo[64];

//        sprintf(textoo, "CR=[%s]", head);
//        log_text(textoo);

        char *slash_ptr = strstr(head, "/");

        if (slash_ptr && isdigit(*(slash_ptr + 1)))
        {
            file_size = safe_atoi(slash_ptr + 1);

if (resume_download && session->nresbody > 0)
{
    file_hdl =
        vm_file_open(
            w_file_name,
            MODE_WRITE,
            TRUE);

if (file_hdl < 0)
{
    log_text("Cannot open tmp file");
    return;
}

    vm_file_seek(
        file_hdl,
        0,
        BASE_END);

VMUINT bytes_to_write = session->nresbody;

if (start_size + bytes_to_write > file_size)
{
    bytes_to_write = file_size - start_size;
}

vm_file_write(
    file_hdl,
    session->resbody,
    bytes_to_write,
    &written);

    vm_file_close(file_hdl);

start_size += bytes_to_write;

}

//            char textjj[64];

//            sprintf(textjj, "file_size=%u", file_size);
//            log_text(textjj);
        }
        else
        {
            log_text("Invalid Content-Range");

if (!resume_download && file_hdl >= 0)
{
    vm_file_close(file_hdl);
    file_hdl = -1;
}

            return;
        }
    }
    else
    {
        if (get_http_head(session, "Content-Length", head))
        {
            if (!resume_download)
            {
    if (file_hdl >= 0) { vm_file_close(file_hdl); }
//                vm_file_close(file_hdl);
                vm_file_delete(w_file_name);
            }

            log_text("Failed to get file size");
            return;
        }

        file_size = (VMUINT)safe_atoi(head);

//        sprintf(text, "file_size=%u", file_size);
//        log_text(text);
    }

if (!resume_download && file_hdl >= 0)
{
    vm_file_close(file_hdl);
    file_hdl = -1;
}

}
else
{
    /* Subsequent chunks */

    VMUINT bytes_to_write = session->nresbody;

    if (start_size + bytes_to_write > file_size)
    {
        bytes_to_write =
            file_size - start_size;
    }

    start_size += bytes_to_write;

    int progress =
        (int)(((float)start_size / file_size) * 100);

    sprintf(
        text,
        "Download progress: %d%%",
        progress);

    log_text(text);

    file_hdl =
        vm_file_open(
            w_file_name,
            MODE_WRITE,
            TRUE);

    if (file_hdl < 0)
    {
        log_text("Cannot open tmp file");
        return;
    }

    vm_file_seek(
        file_hdl,
        0,
        BASE_END);

    vm_file_write(
        file_hdl,
        session->resbody,
        bytes_to_write,
        &written);

    vm_file_close(file_hdl);

    if (start_size >= file_size)
    {
        char final_name[128];

        build_download_name(
            g_http_url.path,
            http_is_gzip,
            final_name,
            sizeof(final_name));

        vm_ascii_to_ucs2(
            w_file_name1,
            sizeof(w_file_name1),
            final_name);

        vm_file_delete(w_file_name1);

        if (vm_file_rename(
                w_file_name,
                w_file_name1) != 0)
        {
            log_text("rename failed");
            return;
        }

        http_hdl[0] = -1;

        VMUINT total = file_size;

        file_size = 0;
        start_size = 0;
        http_is_gzip = 0;
        second = VM_FALSE;
        resume_download = VM_FALSE;

        sprintf(
            text,
            "Download successful, %u bytes",
            total);

        log_text(text);

        return;
    }
}

    }

    let_download();
}

static void notify_callback(VMINT state, VMINT param, void* session) {

    VMCHAR text[255] = {0};

    switch (state) {
        case HTTP_STATE_GET_HOSTNAME:
        case HTTP_STATE_CONNECTING:
        case HTTP_STATE_SENDING:
        case HTTP_STATE_RECV_STATUS:
        case HTTP_STATE_RECV_HEADS:
        case HTTP_STATE_RECV_BODY:
            break;
        default:
            strcpy(text, "Unknown Event");
            log_text(text);
            break;
    }

}

VMINT read_data(VMWSTR file_name, VMINT offset, VMINT num, void* data) {

    VMFILE file_hdl;
    VMUINT nread;
    file_hdl = vm_file_open(file_name, MODE_READ, TRUE);
    if (file_hdl >= 0) {
        vm_file_seek(file_hdl, offset, BASE_BEGIN);
        vm_file_read(file_hdl, data, num, &nread);
        vm_file_close(file_hdl);
        return 0;
    }
    return -1;

}

VMINT get_con_handl(void) {

    return http_hdl[0];

}

void let_download(void)
{

    cancel_download = VM_FALSE;

    if (is_https_url(new_data))
    {
        if (https_parse_url(new_data, &g_url) == 0)
        {
            redirect_count = 0;

            if (main_timer_id < 0)
            {
                main_timer_id = vm_create_timer(
                    100,
                    timer);
            }

            new_download_request = VM_TRUE;
        }
        else
        {
            log_text("bad https url");
        }
    }
    else
    {
        download_process(
            new_data,
            HTTP_USE_CMNET_PRIORITY,
            GET,
            &http_hdl[0],
            download_callback,
            notify_callback);
    }
}


VMINT https_parse_url(const VMCHAR *url, https_url_t *out)
{
    const char *p;
    const char *slash;

    if (!url || !out)
        return -1;

    memset(out, 0, sizeof(https_url_t));

    if (strncmp(url, "https://", 8))
        return -1;

    p = url + 8;

    slash = strchr(p, '/');

    if (slash)
    {
        memcpy(out->host, p, slash - p);
        out->host[slash - p] = 0;

        strncpy(out->path,
                slash,
                sizeof(out->path) - 1);
    }
    else
    {
        strcpy(out->host, p);
        strcpy(out->path, "/");
    }

    out->port = 443;

    return 0;
}

VMINT https_send_request(https_url_t *url)
{
    char req[1024];

    sprintf(
        req,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: Mozilla/5.0\r\n"
        "Accept-Encoding: gzip\r\n"
        "Connection: close\r\n"
        "\r\n",
        url->path,
        url->host);

if (br_sslio_write_all(&https_ctx.ioc, req, strlen(req)) < 0)
{
//char msg[64];

//sprintf(
//    msg,
//    "ssl=%d x509=%d",
//    br_ssl_engine_last_error(&https_ctx.sc.eng),
//    https_ctx.xc.err);

//   log_text(msg);

if (https_ctx.xc.err != 0) {

char msggg[64];

sprintf(
    msggg,
    "SSL err=%d",
    https_ctx.xc.err);

log_text(msggg);

log_text("Download failed !");

}

//sprintf(
//    msg,
//    "iobuf=%u",
//    (unsigned)sizeof(https_ctx.iobuf));

//   log_text(msg);

    return -1;
}

//   log_text("after ssl write");


if (br_sslio_flush(&https_ctx.ioc) < 0)
{
    log_text("SSL flush failed");
    return -1;
}

//char text[64];

//sprintf(text, "state=%lu err=%d", (unsigned long)br_ssl_engine_current_state(&https_ctx.sc.eng), br_ssl_engine_last_error(
//&https_ctx.sc.eng));

//        log_text(text);

        int err = br_ssl_engine_last_error(&https_ctx.sc.eng);

        if (err != 0)
        {
            char msg[64];

            sprintf(msg, "SSL err=%d", err);

            log_text(msg);
            log_text("Download failed !");

            vm_tcp_close(https_ctx.tcp_handle);
            https_ctx.tcp_handle = -1;
            return -1;
        }

    return 0;
}

VMINT https_receive_file(
    VMFILE file)
{

    if (file < 0)
        return -1;

    char buf[1024];

    VMUINT written;

    int len;

    if (read_headers(
            &https_ctx.ioc))
    {
        return -1;
    }

//char text[128];

//sprintf(
//    text,
//    "HTTP=%d LEN=%d CH=%d GZ=%d",
//    http_status,
//    content_length,
//    header_is_chunked,
//    header_is_gzip);

//log_text(text);


if (http_status == 301 ||
    http_status == 302)
{
//    char msg[600];

//    sprintf(
//        msg,
//        "REDIRECT=[%s]",
//        redirect_url);

//    log_text(msg);

return http_status;
}

if (http_status == 404)
{
    log_text("File not found");
    return -1;
}

if (http_status != 200 &&
    http_status != 206)
{
    return -1;
}

if (header_is_gzip)
{
   log_text("Gzip content detected");
}

    if (header_is_chunked)
    {
        return https_receive_chunked(
            file);
    }

    while ((len = br_sslio_read(&https_ctx.ioc, buf, sizeof(buf))) > 0)
    {

//    if (cancel_download)
//    {
//        vm_tcp_close(https_ctx.tcp_handle);
//        return -1;
//    }


if (cancel_download)
{
    vm_tcp_close(https_ctx.tcp_handle);
    https_ctx.tcp_handle = -1;
    return -1;
}


        vm_file_write(
            file,
            buf,
            len,
            &written);
    }

    return 0;
}

VMINT https_connect(
    https_url_t *url)
{
    memset(
        &https_ctx,
        0,
        sizeof(https_ctx));

    connected = VM_FALSE;
    network_err = VM_FALSE;

VMINT ret = vm_srv_nw_info_get_protocol(0x1);

//char buf[32];
//sprintf(buf, "net=%d", ret);
//log_text(buf);

vm_apn_info_ext apn;
//char txt[64];

if (vm_get_default_apn_info(&apn) == 0)
{
//    sprintf(
//        txt,
//        "apn id=%d",
//        apn.apn_info_id);

//     log_text(txt);
    vm_dtacct_set(
        0,
        apn.apn_info_id);

    https_ctx.tcp_handle =
        vm_tcp_connect(
            url->host,
            443,
            VM_APN_USER_DEFINE,
            tcp_callback);
}
else
{
   log_text("No default APN");

    return -1;
}

//char txt_x[64];

//sprintf(txt_x, "sock=%d conn=%d net=%d",
//        https_ctx.tcp_handle,
//        connected,
//        network_err);

//log_text(txt_x);

    if (https_ctx.tcp_handle < 0)
        return -1;

    return 0;
}

static int sock_read(
    void *ctx,
    unsigned char *buf,
    size_t len)
{
    for (;;)
    {
        int rlen =
            vm_tcp_read(
                *(VMINT *)ctx,
                buf,
                len);

//        char msg[64];
//        sprintf(msg, "r=%d", rlen);
//        log_text(msg);

        if (rlen <= 0)
        {
            if (!network_err)
            {
                thread_next();
                continue;
            }

            return -1;
        }

        return rlen;
    }
}


static int sock_write(
    void *ctx,
    const unsigned char *buf,
    size_t len)
{
    static int counter = 0;

    for (;;)
    {
        int wlen =
            vm_tcp_write(
                *(VMINT *)ctx,
                (void *)buf,
                len);

        if ((counter++ % 20) == 0)
        {
//            char msg[64];

//            sprintf(
//                msg,
//                "w=%d evt=%d",
//                wlen,
//                tcp_event);

//           log_text(msg);
        }

        if (wlen <= 0)
        {
            if (!network_err)
            {
                thread_next();
                continue;
            }

            return -1;
        }

        return wlen;
    }
}

void tcp_callback(VMINT handle, VMINT event)
{
//    tcp_count++; //for debuging ?
//    tcp_event = event; //for debuging ?

    switch(event)
    {
        case VM_TCP_EVT_CONNECTED:
            connected = VM_TRUE;
            break;

        case VM_TCP_EVT_HOST_NOT_FOUND:
            network_err = VM_TRUE;
            log_text("Invalid address");
            break;

        case VM_TCP_EVT_PIPE_BROKEN:
            network_err = VM_TRUE;
            log_text("Connection broken");
            break;

        case VM_TCP_EVT_PIPE_CLOSED:
            network_err = VM_TRUE;
            log_text("Connection closed");
            break;
    }
}

static int https_read_line(
    br_sslio_context *ioc,
    char *buf,
    int maxlen)
{
    int pos = 0;
    char c;

    while (pos < maxlen - 1) {
        if (br_sslio_read(ioc, &c, 1) <= 0)
            return -1;
        if (c == '\r')
           continue;

        if (c == '\n')
            break;

        buf[pos++] = c;
    }

    buf[pos] = 0;

    return pos;
}

static VMUINT hex_to_uint(
    const char *s)
{
    VMUINT value = 0;

    while (*s)
    {
        char c = *s++;

        if (c >= '0' && c <= '9')
        {
            value =
                (value << 4)
                + (c - '0');
        }
        else if (c >= 'A' && c <= 'F')
        {
            value =
                (value << 4)
                + (c - 'A' + 10);
        }
        else if (c >= 'a' && c <= 'f')
        {
            value =
                (value << 4)
                + (c - 'a' + 10);
        }
        else
        {
            break;
        }
    }

    return value;
}

static VMINT https_receive_chunked(
    VMFILE file)
{
    char line[64];

    char buf[1024];

    VMUINT written;

    while (1)
    {

    if (cancel_download)
    {
        vm_tcp_close(https_ctx.tcp_handle);
        https_ctx.tcp_handle = -1;
        return -1;
    }

        VMUINT chunk_size;

        if (https_read_line(
                &https_ctx.ioc,
                line,
                sizeof(line)) < 0)
        {
            return -1;
        }

        chunk_size =
            hex_to_uint(line);

        if (chunk_size == 0)
        {
            break;
        }

        while (chunk_size > 0)
        {


    if (cancel_download)
    {
        vm_tcp_close(https_ctx.tcp_handle);
        https_ctx.tcp_handle = -1;
        return -1;
    }


            VMUINT part;
            int len;

            part =
                (chunk_size > sizeof(buf))
                ? sizeof(buf)
                : chunk_size;

            len =
                br_sslio_read(
                    &https_ctx.ioc,
                    buf,
                    part);

            if (len <= 0)
                return -1;

            vm_file_write(
                file,
                buf,
                len,
                &written);

            chunk_size -= len;
        }

        /* consume CRLF after chunk */

        br_sslio_read(
            &https_ctx.ioc,
            line,
            2);
    }

    return 0;
}

static int read_headers(
    br_sslio_context *ioc)
{
    char line[256];
    char lower[256];

redirect_url[0] = 0;

    http_status = 0;
    header_is_chunked = 0;
    header_is_gzip = 0;
    content_length = -1;

    if (https_read_line(
            ioc,
            line,
            sizeof(line)) < 0)
    {
        return -1;
    }

{

//    char msg[300];
//    sprintf(msg, "STATUS=[%s]", line);
//    log_text(msg);

}

    if (!strncmp(line, "HTTP/", 5))
    {
        char *p = strchr(line, ' ');

        if (p)
            http_status = safe_atoi(p + 1);

//        char dbg[64];
//        sprintf(dbg, "PARSED=%d", http_status);
//        log_text(dbg);

    }

    while (1)
    {
        if (https_read_line(
                ioc,
                line,
                sizeof(line)) < 0)
        {

    log_text("First line failed");

            return -1;
        }

{

//    char msg[300];
//    sprintf(msg, "FIRST=[%s]", line);
//    log_text(msg);

}

        if (line[0] == 0)
            break;

        vm_lower_case(lower, line);

if (!strncmp(lower, "location:", 9))
{
    char *p = line + 9;

    while (*p == ' ')
        p++;

    strncpy(
        redirect_url,
        p,
        sizeof(redirect_url) - 1);

    redirect_url[sizeof(redirect_url) - 1] = 0;
}


        if (!strncmp(
                lower,
                "transfer-encoding:",
                18))
        {
            if (strstr(lower, "chunked"))
                header_is_chunked = 1;
        }

        if (!strncmp(
                lower,
                "content-encoding:",
                17))
        {
            if (strstr(lower, "gzip"))
                header_is_gzip = 1;
        }

        if (!strncmp(
                lower,
                "content-length:",
                15))
        {
            content_length =
                safe_atoi(lower + 15);
        }
    }

    return 0;
}

void timer(VMINT tid)
{
//    char txt[64];

//sprintf(
//    txt,
//    "cb=%d evt=%d",
//    tcp_count,
//    tcp_event);
//    if(tcp_event != -1 & tcp_count != 0){
//    log_text(txt);
//    }
    thread_next();
}

void https_worker(void)
{
    for (;;)
    {

        int rc;

        thread_next();

        if (!new_download_request)
            continue;

        cancel_download = VM_FALSE;
        new_download_request = VM_FALSE;



if (g_file < 0)
{
    VMWCHAR wname[64];

    char tmp_name[64];
    sprintf(tmp_name, "%c:\\https_download.bin", drv);
    vm_ascii_to_ucs2(wname, sizeof(wname), tmp_name);

    g_file =
        vm_file_open(
            wname,
            MODE_CREATE_ALWAYS_WRITE,
            TRUE);

    if (g_file < 0)
    {
        log_text("Cannot create file");
        continue;
    }
}


if (https_connect(&g_url) < 0)
{
    log_text("TCP connect failed");

    if (main_timer_id >= 0)
    {
        vm_delete_timer(main_timer_id);
        main_timer_id = -1;
    }

    continue;
}

        while (!connected)
        {
            if (network_err)
            {
//               log_text("TCP failed");

                goto next_download;
            }

            thread_next();
        }

          log_text("TCP connected");

        br_ssl_client_init_full(
            &https_ctx.sc,
            &https_ctx.xc,
            TAs,
            TAs_NUM);

//       br_ssl_client_init_full( //disable certificate check !
//           &https_ctx.sc,
//            &https_ctx.xc,
//            NULL,
//            0);

vm_get_time(&t);
unsigned long days = bearssl_days(t.year, t.mon, t.day);
unsigned long seconds = t.hour * 3600UL + t.min  * 60UL + t.sec;
br_x509_minimal_set_time(&https_ctx.xc, days, seconds);

//char msguu[64];
//sprintf(msguu, "TAs=%d", (int)TAs_NUM);
//log_text(msguu);

        br_ssl_engine_set_buffer(
            &https_ctx.sc.eng,
            https_ctx.iobuf,
            sizeof(https_ctx.iobuf),
            1);

        br_ssl_client_reset(
            &https_ctx.sc,
            g_url.host,
            0);

        br_sslio_init(
            &https_ctx.ioc,
            &https_ctx.sc.eng,
            sock_read,
            &https_ctx.tcp_handle,
            sock_write,
            &https_ctx.tcp_handle);

        if (https_send_request(&g_url) < 0)
            goto next_download;

        rc = https_receive_file(g_file);

        /*
         * Redirect (301/302)
         */
        if (rc == 301 || rc == 302)
        {


redirect_count++;

if (redirect_count > 10)
{
    log_text("too many redirects");
    goto next_download;
}

            char msg[600];

            sprintf(
                msg,
                "-> %s",
                redirect_url);

            log_text(msg);


if (!strncmp(redirect_url, "https://", 8))
{
    if (https_parse_url(
            redirect_url,
            &g_url) == 0)
    {
        new_download_request = VM_TRUE;
    }
    else
    {
        log_text("bad redirect url");
    }

}

else if (!strncmp(redirect_url, "http://", 7))
{
    strncpy(
        new_data,
        redirect_url,
        sizeof(new_data) - 1);

    new_data[sizeof(new_data) - 1] = '\0';

    http_parse_url(new_data, &g_http_url);

    if (g_file >= 0)
    {
        vm_file_close(g_file);
        g_file = -1;
    }

    let_download();

    goto next_download;
}

else if (redirect_url[0] == '/')
{
    /*
     * Relative redirect:
     * Location: /new/path/file.gz
     */

    strncpy(
        g_url.path,
        redirect_url,
        sizeof(g_url.path) - 1);

    g_url.path[sizeof(g_url.path) - 1] = 0;

    new_download_request = VM_TRUE;
}
else
{
    /*
     * Relative redirect:
     * Location: file.gz
     */

    char path[512];
    char *last;

    strncpy(
        path,
        g_url.path,
        sizeof(path) - 1);

    path[sizeof(path) - 1] = '\0';

    last = strrchr(path, '/');

    if (last)
    {
        *(last + 1) = '\0';
    }
    else
    {
        strcpy(path, "/");
    }

    strncat(
        path,
        redirect_url,
        sizeof(path) - strlen(path) - 1);

    strncpy(
        g_url.path,
        path,
        sizeof(g_url.path) - 1);

    g_url.path[sizeof(g_url.path) - 1] = '\0';

    new_download_request = VM_TRUE;
}

    if (new_download_request)
    {
        if (g_file >= 0)
        {
            vm_file_close(g_file);
            g_file = -1;
        }

        VMWCHAR wname[64];

        char tmp_name[64];
        sprintf(tmp_name, "%c:\\https_download.bin", drv);
        vm_ascii_to_ucs2(wname, sizeof(wname), tmp_name);

        vm_file_delete(wname);
    }

            goto next_download;
        }

if (rc < 0) //get Error ???
{
    if (main_timer_id >= 0)
    {
        vm_delete_timer(main_timer_id);
        main_timer_id = -1;
    }

if (cancel_download)
{
    log_text("Download cancelled");
}

    goto next_download;
}

       log_text("HTTPS download complete");
redirect_count = 0;
if (main_timer_id >= 0)
{
    vm_delete_timer(main_timer_id);
    main_timer_id = -1;
}

if (g_file >= 0)
{
    vm_file_close(g_file);
    g_file = -1;
}

VMWCHAR old_name[128];
VMWCHAR new_name[128];
char new_name_ascii[128];

build_download_name(g_url.path, header_is_gzip, new_name_ascii, sizeof(new_name_ascii));

char tmp_name[64];
sprintf(tmp_name, "%c:\\https_download.bin", drv);
vm_ascii_to_ucs2(old_name, sizeof(old_name), tmp_name);

vm_ascii_to_ucs2(new_name, sizeof(new_name), new_name_ascii);

vm_file_delete(new_name);

if (vm_file_rename(old_name, new_name) == 0)
{
//    log_text("rename ok");
}
else
{
//    log_text("rename failed");
}

next_download:

if (g_file >= 0)
{
    vm_file_close(g_file);
    g_file = -1;
}

if (https_ctx.tcp_handle >= 0)
{
    vm_tcp_close(https_ctx.tcp_handle);
    https_ctx.tcp_handle = -1;
}

    }
}

static unsigned long is_leap(int y)
{
    return ((y % 4) == 0)
        && (((y % 100) != 0) || ((y % 400) == 0));
}

static unsigned long bearssl_days(
    int year,
    int month,
    int day)
{
    static const int mdays[12] = {
        31,28,31,30,31,30,
        31,31,30,31,30,31
    };

    unsigned long d = 0;
    int y, m;

    for (y = 0; y < year; y++) {
        d += is_leap(y) ? 366 : 365;
    }

    for (m = 1; m < month; m++) {
        d += mdays[m - 1];
        if (m == 2 && is_leap(year)) {
            d++;
        }
    }

    d += day - 1;

    return d;
}

void log_text(const char *msg)
{
    if (filledDsplByLines >
        (nscreen_height - vm_graphic_get_character_height() - 2))
    {
        fill_white();
    }

    display_text_line(
        screenbuf,
        msg,
        0,
        filledDsplByLines,
        nscreen_width,
        nscreen_height,
        2,
        1,
        VM_COLOR_BLACK,
        VM_FALSE);
}

int safe_atoi(const char *s)
{
    int value = 0;
    int found_digit = 0;

    if (!s)
        return -1;

    while (*s == ' ' || *s == '\t')
        s++;

    while (*s >= '0' && *s <= '9')
    {
        found_digit = 1;

int digit = *s - '0';

if (value > 214748364)
    return -1;

if (value == 214748364 && digit > 7)
    return -1;
value = value * 10 + digit;

        s++;
    }

    return found_digit ? value : -1;
}

void build_download_name(
    const char *path,
    int gzip,
    char *out,
    int out_size)
{
    const char *name;
    char temp[128];
    char *q;

    name = strrchr(path, '/');

    if (name)
        name++;
    else
        name = path;

    /*
     * URL ends with '/'
     */
    if (*name == '\0')
    {
        snprintf(
            out,
            out_size,
            gzip
                ? "%c:\\download.html.gz"
                : "%c:\\download.html",
            drv);

        return;
    }

    strncpy(
        temp,
        name,
        sizeof(temp) - 1);

    temp[sizeof(temp) - 1] = '\0';

    q = strchr(temp, '?');

    if (q)
        *q = '\0';

    q = strchr(temp, '#');

    if (q)
        *q = '\0';

    if (gzip)
    {

//        char *p = temp;
//        while ((p = strchr(p, '.')) != NULL)
//        {
//            *p = '_';
//            p++;
//        }

        snprintf(
            out,
            out_size,
            "%c:\\%s.gz",
            drv,
            temp);
    }
    else
    {
        snprintf(
            out,
            out_size,
            "%c:\\%s",
            drv,
            temp);
    }
}

static VMBOOL is_https_url(const char *url)
{
    return !strncmp(url, "https://", 8);
}

static VMBOOL is_http_url(const char *url)
{
    return !strncmp(url, "http://", 7);
}

VMINT http_parse_url(const char *url, http_url_t *out)
{
    const char *p;
    const char *slash;
    const char *colon;

    if (!url || !out)
        return -1;

    memset(out, 0, sizeof(http_url_t));

    if (!strncmp(url, "http://", 7))
    {
        p = url + 7;
        out->port = 80;
    }
    else
    {
        /*
         * Allow:
         * example.com/file.bin
         */
        p = url;
        out->port = 80;
    }

    slash = strchr(p, '/');
    colon = strchr(p, ':');

    /*
     * host:port/path
     */
    if (colon && (!slash || colon < slash))
    {
        memcpy(
            out->host,
            p,
            colon - p);

        out->host[colon - p] = '\0';

        out->port =
            safe_atoi(colon + 1);

        if (slash)
        {
            strncpy(
                out->path,
                slash,
                sizeof(out->path) - 1);
        }
        else
        {
            strcpy(out->path, "/");
        }
    }
    else
    {
        if (slash)
        {
            memcpy(
                out->host,
                p,
                slash - p);

            out->host[slash - p] = '\0';

            strncpy(
                out->path,
                slash,
                sizeof(out->path) - 1);
        }
        else
        {
            strcpy(out->host, p);
            strcpy(out->path, "/");
        }
    }

    return 0;
}
