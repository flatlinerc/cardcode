# ESP-IDF 6.0.1 `esp_http_server` — WebSocket & Work-Queue Reference

**Source:** raw `esp_http_server.h` from the `v6.0.1` branch of `espressif/esp-idf`.
**URL:** https://raw.githubusercontent.com/espressif/esp-idf/v6.0.1/components/esp_http_server/include/esp_http_server.h
**SPDX:** Apache-2.0, Espressif Systems (Shanghai) CO LTD 2018-2026.
**Why this exists:** the handoff at `docs/superpowers/plans/2026-06-07-doc-fixes-handoff.md` references these APIs. The official Espressif changelog for 6.0.1 is not available as a single static text file (the GitHub release page is a stub, the release-notes site at `release-notes.espressif.tools` is a client-rendered Next.js app, and the repo has no top-level `CHANGELOG.md`). This file is the next-best artifact: the actual API surface as it exists in the v6.0.1 source.

**If you are verifying whether a change to `esp32.md` matches IDF 6.0.1, read this file first.**

---

## Work-queue API (Group: "Work Queue")

The work-queue API is **unchanged** from IDF 5.x. `httpd_queue_work` is still the supported way to schedule a function on the httpd task. There is no `esp_workqueue` component in IDF 6.0.1's `esp_http_server` and no deprecation marker in the header.

```c
/**
 * @brief   Prototype of the HTTPD work function
 *          Please refer to httpd_queue_work() for more details.
 * @param[in] arg   The arguments for this work function
 */
typedef void (*httpd_work_fn_t)(void *arg);

/**
 * @brief   Queue execution of a function in HTTPD's context
 *
 * This API queues a work function for asynchronous execution
 *
 * @note    Some protocols require that the web server generate some asynchronous data
 *          and send it to the persistently opened connection. This facility is for use
 *          by such protocols.
 *
 * @param[in] handle    Handle to server returned by httpd_start
 * @param[in] work      Pointer to the function to be executed in the HTTPD's context
 * @param[in] arg       Pointer to the arguments that should be passed to this function
 *
 * @return
 *  - ESP_OK   : On successfully queueing the work
 *  - ESP_FAIL : Failure in ctrl socket
 *  - ESP_ERR_INVALID_ARG : Null arguments
 */
esp_err_t httpd_queue_work(httpd_handle_t handle, httpd_work_fn_t work, void *arg);
```

## WebSocket frame struct (changed in IDF 6)

**The only API change** in IDF 6.0.1 that affects `esp32.md` is two new fields in `httpd_ws_frame_t`. The struct now has:

```c
typedef struct httpd_ws_frame {
    bool final;                 /*!< Final frame:
                                     For received frames this field indicates whether the `FIN` flag was set.
                                     For frames to be transmitted, this field is only used if the `fragmented`
                                         option is set as well. If `fragmented` is false, the `FIN` flag is set
                                         by default, marking the ws_frame as a complete/unfragmented message
                                         (esp_http_server doesn't automatically fragment messages) */
    bool fragmented;            /*!< Indication that the frame allocated for transmission is a message fragment,
                                     so the `FIN` flag is set manually according to the `final` option.
                                     This flag is never set for received messages */
    httpd_ws_type_t type;       /*!< WebSocket frame type */
    uint8_t *payload;           /*!< Pre-allocated data buffer */
    size_t len;                 /*!< Length of the WebSocket data */
} httpd_ws_frame_t;
```

For the cardcode use case (single-frame text messages from the UI), the change is **transparent** if you initialize the struct with `= {}` — both new `bool` fields default to `false`. The handoff's recommended pattern in item 3.4 (zero-init the struct) is still correct; just be aware the struct is now 5 fields, not 3.

## WebSocket send / receive API (unchanged in 6.0.1)

```c
/**
 * @brief Receive and parse a WebSocket frame
 *
 * @note    Calling httpd_ws_recv_frame() with max_len as 0 will give actual frame size in pkt->len.
 *          The user can dynamically allocate space for pkt->payload as per this length and call httpd_ws_recv_frame() again to get the actual data.
 *
 * @param[in]   req         Current request
 * @param[out]  pkt         WebSocket packet
 * @param[in]   max_len     Maximum length for receive
 * @return
 *  - ESP_OK                    : On successful
 *  - ESP_FAIL                  : Socket errors occurs
 *  - ESP_ERR_INVALID_STATE     : Handshake was already done beforehand
 *  - ESP_ERR_INVALID_ARG       : Argument is invalid (null or non-WebSocket)
 */
esp_err_t httpd_ws_recv_frame(httpd_req_t *req, httpd_ws_frame_t *pkt, size_t max_len);

/**
 * @brief Construct and send a WebSocket frame
 * @param[in]   req     Current request
 * @param[in]   pkt     WebSocket frame
 */
esp_err_t httpd_ws_send_frame(httpd_req_t *req, httpd_ws_frame_t *pkt);

/**
 * @brief Low level send of a WebSocket frame out of the scope of current request
 * using internally configured httpd send function
 *
 * This API should rarely be called directly, with an exception of asynchronous send using httpd_queue_work.
 *
 * @param[in] hd      Server instance data
 * @param[in] fd      Socket descriptor for sending data
 * @param[in] frame     WebSocket frame
 */
esp_err_t httpd_ws_send_frame_async(httpd_handle_t hd, int fd, httpd_ws_frame_t *frame);

/**
 * @brief Checks the supplied socket descriptor if it belongs to any active client
 * of this server instance and if the websoket protocol is active
 */
httpd_ws_client_info_t httpd_ws_get_fd_info(httpd_handle_t hd, int fd);

esp_err_t httpd_ws_send_data(httpd_handle_t handle, int socket, httpd_ws_frame_t *frame);
esp_err_t httpd_ws_send_data_async(httpd_handle_t handle, int socket, httpd_ws_frame_t *frame,
                                   transfer_complete_cb callback, void *arg);
```

## WebSocket opcode enum (unchanged in 6.0.1)

```c
typedef enum {
    HTTPD_WS_TYPE_CONTINUE   = 0x0,
    HTTPD_WS_TYPE_TEXT       = 0x1,
    HTTPD_WS_TYPE_BINARY     = 0x2,
    HTTPD_WS_TYPE_CLOSE      = 0x8,
    HTTPD_WS_TYPE_PING       = 0x9,
    HTTPD_WS_TYPE_PONG       = 0xA
} httpd_ws_type_t;
```

## `httpd_req_to_sockfd` (unchanged in 6.0.1)

**Not deprecated.** The function is fully documented and is referenced by other docblocks in the header (`/components/esp_http_server/include/esp_http_server.h:788, 808, 828`) as the canonical way to get a socket fd from a request. No `[[deprecated]]` attribute, no "use X instead" comment.

```c
/**
 * @brief Get the Socket descriptor from httpd request
 *
 * The API returns the socket descriptor of the underlying
 * socket in the httpd request. This is useful for sending data
 * over the socket in a raw format (e.g. websockets) or for
 * retrieving the client's source IP address.
 *
 * @param[in] r   The request whose socket descriptor should be obtained
 * @return
 *  - The socket descriptor
 *  - (-1) if the request has no socket associated
 */
int httpd_req_to_sockfd(httpd_req_t *r);
```

## Summary of changes for the handoff

| API in `esp32.md` | Status in IDF 6.0.1 | Action |
|---|---|---|
| `httpd_queue_work` | **Still present**, unchanged signature | Keep as written. Do **not** substitute `esp_workqueue` (no such component exists in 6.0.1). |
| `httpd_ws_send_frame_async` | **Still present**, unchanged signature | Keep as written. |
| `httpd_ws_recv_frame` | **Still present**, unchanged signature | Keep as written. |
| `httpd_req_to_sockfd` | **Still present**, **not** deprecated | Keep as written. |
| `httpd_ws_frame_t` | **Two new `bool` fields**: `final`, `fragmented` | `= {}` zero-init still works; be aware the struct is 5 fields. |
| `HTTPD_WS_TYPE_TEXT` etc. | Unchanged | Keep as written. |

The handoff's earlier "is gone (or scheduled to be gone)" framing for `httpd_queue_work` was wrong, and has been removed in the corrected handoff.

## Where to verify other APIs

If you change `esp32.md` to add or change an API that is **not** listed above, verify against:

- The IDF 6.0.1 API reference (HTML): https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/protocols/esp_http_server.html
- The full `esp_http_server.h` source: https://raw.githubusercontent.com/espressif/esp-idf/v6.0.1/components/esp_http_server/include/esp_http_server.h
- The v6.0.1 → v6.0.1 patch notes (if any): https://github.com/espressif/esp-idf/compare/v6.0.0...v6.0.1 (likely empty; 6.0.0 was the breaking change)
- The v5.x → 6.x migration guide: https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/migration-guides/release-6.x.html (rendered HTML; check for `esp_http_server` mentions)
