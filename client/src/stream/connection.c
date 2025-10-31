#include "connection.h"

#include <gst/gstelement.h>
#include <gst/gstobject.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-session.h>
#include <stdbool.h>
#include <string.h>

#include "status.h"
#include "utils/logger.h"

// clang-format off
#define ENET_IMPLEMENTATION
#include "3rd/enet.h"
// clang-format on

#include "input.h"
#include "thread.h"

#define SERVER_ADDRESS "192.168.31.178"
#define DEFAULT_WEBSOCKET_URI "ws://" SERVER_ADDRESS ":5600/ws"

/*!
 * Data required for the handshake to complete and to maintain the connection.
 */
struct _MyConnection {
    GObject parent;
    SoupSession *soup_session;
    gchar *websocket_uri;
    gchar *host_address;

    /// Cancellable for websocket connection process
    GCancellable *ws_cancel;
    SoupWebsocketConnection *ws;

    // Can we not expose pipeline here?
    GstPipeline *pipeline;

    enum my_status status;

    ENetHost *client;
    ENetPeer *peer;
    struct os_thread_helper enet_thread;
    GAsyncQueue *packet_queue;
};

G_DEFINE_TYPE(MyConnection, my_connection, G_TYPE_OBJECT)

enum {
    // action signals
    SIGNAL_CONNECT,
    SIGNAL_DISCONNECT,
    SIGNAL_SET_PIPELINE,
    // signals
    SIGNAL_WEBSOCKET_CONNECTED,
    SIGNAL_WEBSOCKET_FAILED,
    SIGNAL_STATUS_CHANGE,
    SIGNAL_ON_NEED_PIPELINE,
    SIGNAL_ON_DROP_PIPELINE,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef enum {
    PROP_WEBSOCKET_URI = 1,
    PROP_HOST_ADDRESS,
    // PROP_STATUS,
    N_PROPERTIES
} MyConnectionProperty;

static GParamSpec *properties[N_PROPERTIES] = {
    NULL,
};

/* GObject method implementations */

static void my_connection_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    MyConnection *self = MY_CONNECTION(object);

    switch ((MyConnectionProperty)property_id) {
        case PROP_WEBSOCKET_URI:
            g_free(self->websocket_uri);
            self->websocket_uri = g_value_dup_string(value);
            ALOGI("Websocket URI assigned: %s", self->websocket_uri);
            break;
        case PROP_HOST_ADDRESS:
            g_free(self->host_address);
            self->host_address = g_value_dup_string(value);
            ALOGI("Host address assigned: %s", self->host_address);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

#undef MAKE_CASE

static void conn_update_status(MyConnection *conn, enum my_status status) {
    if (status == conn->status) {
        ALOGI("conn: state update: already in %s", my_status_to_string(conn->status));
        return;
    }
    ALOGI("conn: state update: %s -> %s", my_status_to_string(conn->status), my_status_to_string(status));
    conn->status = status;
}

static void my_connection_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    MyConnection *self = MY_CONNECTION(object);

    switch ((MyConnectionProperty)property_id) {
        case PROP_WEBSOCKET_URI:
            g_value_set_string(value, self->websocket_uri);
            break;
        case PROP_HOST_ADDRESS:
            g_value_set_string(value, self->host_address);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void my_connection_init(MyConnection *conn) {
    conn->ws_cancel = g_cancellable_new();
    conn->soup_session = soup_session_new();
    conn->websocket_uri = g_strdup(DEFAULT_WEBSOCKET_URI);
}

static void my_connection_dispose(GObject *object) {
    MyConnection *self = MY_CONNECTION(object);

    my_connection_disconnect(self);

    g_clear_object(&self->soup_session);
    g_clear_object(&self->ws_cancel);
}

static void my_connection_finalize(GObject *object) {
    MyConnection *self = MY_CONNECTION(object);

    g_free(self->websocket_uri);
}

static void my_connection_class_init(MyConnectionClass *klass) {
    ALOGI("%s: Begin", __FUNCTION__);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = my_connection_dispose;
    gobject_class->finalize = my_connection_finalize;

    gobject_class->set_property = my_connection_set_property;
    gobject_class->get_property = my_connection_get_property;

    /**
     * MyConnection:websocket-uri:
     *
     * The websocket URI for the signaling server
     */
    g_object_class_install_property(
        gobject_class,
        PROP_WEBSOCKET_URI,
        g_param_spec_string("websocket-uri",
                            "WebSocket URI",
                            "WebSocket URI for signaling server.",
                            DEFAULT_WEBSOCKET_URI /* default value */,
                            G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class,
        PROP_HOST_ADDRESS,
        g_param_spec_string("host-address",
                            "",
                            "",
                            SERVER_ADDRESS /* default value */,
                            G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /**
     * MyConnection::connect
     * @object: the #MyConnection
     *
     * Start the connection process
     */
    signals[SIGNAL_CONNECT] = g_signal_new_class_handler("connect",
                                                         G_OBJECT_CLASS_TYPE(klass),
                                                         G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                         G_CALLBACK(my_connection_connect),
                                                         NULL,
                                                         NULL,
                                                         NULL,
                                                         G_TYPE_NONE,
                                                         0);

    /**
     * MyConnection::disconnect
     * @object: the #MyConnection
     *
     * Stop the connection process or shutdown the connection
     */
    signals[SIGNAL_DISCONNECT] = g_signal_new_class_handler("disconnect",
                                                            G_OBJECT_CLASS_TYPE(klass),
                                                            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                            G_CALLBACK(my_connection_disconnect),
                                                            NULL,
                                                            NULL,
                                                            NULL,
                                                            G_TYPE_NONE,
                                                            0);

    /**
     * MyConnection::set-pipeline
     * @object: the #MyConnection
     * @pipeline: A #GstPipeline
     *
     * Sets the #GstPipeline containing a #GstWebRTCBin element and begins the WebRTC connection negotiation.
     * Should be signalled in response to @on-need-pipeline
     */
    signals[SIGNAL_SET_PIPELINE] = g_signal_new_class_handler("set-pipeline",
                                                              G_OBJECT_CLASS_TYPE(klass),
                                                              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                              G_CALLBACK(my_connection_set_pipeline),
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_TYPE_NONE,
                                                              1,
                                                              G_TYPE_POINTER);

    /**
     * MyConnection::websocket-connected
     * @object: the #MyConnection
     */
    signals[SIGNAL_WEBSOCKET_CONNECTED] = g_signal_new("websocket-connected",
                                                       G_OBJECT_CLASS_TYPE(klass),
                                                       G_SIGNAL_RUN_LAST,
                                                       0,
                                                       NULL,
                                                       NULL,
                                                       NULL,
                                                       G_TYPE_NONE,
                                                       0);

    /**
     * MyConnection::websocket-failed
     * @object: the #MyConnection
     */
    signals[SIGNAL_WEBSOCKET_FAILED] = g_signal_new("websocket-failed",
                                                    G_OBJECT_CLASS_TYPE(klass),
                                                    G_SIGNAL_RUN_LAST,
                                                    0,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    G_TYPE_NONE,
                                                    0);

    /**
     * MyConnection::on-need-pipeline
     * @object: the #MyConnection
     *
     * Your handler for this must emit @set-pipeline
     */
    signals[SIGNAL_ON_NEED_PIPELINE] = g_signal_new("on-need-pipeline",
                                                    G_OBJECT_CLASS_TYPE(klass),
                                                    G_SIGNAL_RUN_LAST,
                                                    0,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    G_TYPE_NONE,
                                                    0);

    /**
     * MyConnection::on-drop-pipeline
     * @object: the #MyConnection
     *
     * If you store any references in your handler for @on-need-pipeline you must make a handler for this signal to
     * drop them.
     */
    signals[SIGNAL_ON_DROP_PIPELINE] = g_signal_new("on-drop-pipeline",
                                                    G_OBJECT_CLASS_TYPE(klass),
                                                    G_SIGNAL_RUN_LAST,
                                                    0,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    G_TYPE_NONE,
                                                    0);
    ALOGI("%s: End", __FUNCTION__);
}

void my_connection_disconnect(MyConnection *conn) {
    if (conn->ws_cancel != NULL) {
        g_cancellable_cancel(conn->ws_cancel);
        gst_clear_object(&conn->ws_cancel);
    }

    // Notify stream app to drop the pipeline.
    ALOGI("Emit ON_DROP_PIPELINE upon WebSocket disconnection");
    g_signal_emit(conn, signals[SIGNAL_ON_DROP_PIPELINE], 0);

    if (conn->ws) {
        ALOGI("Closing WebSocket connection.");
        soup_websocket_connection_close(conn->ws, 0, "");
    }
    g_clear_object(&conn->ws);

    conn_update_status(conn, MY_STATUS_IDLE_NOT_CONNECTED);

    // ENet
    if (conn->peer) {
        enet_peer_disconnect(conn->peer, 0);

        // Graceful shutdown
        if (0) {
            ENetEvent event = {0};

            uint8_t disconnected = false;

            /* Allow up to 3 seconds for the disconnect to succeed
             * and drop any packets received packets.
             */
            while (enet_host_service(conn->client, &event, 3000) > 0) {
                switch (event.type) {
                    case ENET_EVENT_TYPE_RECEIVE: {
                        enet_packet_destroy(event.packet);
                    } break;
                    case ENET_EVENT_TYPE_DISCONNECT: {
                        ALOGI("ENet disconnected.");
                        disconnected = true;
                    } break;
                }
            }

            // Drop connection, since disconnection didn't succeed.
            if (!disconnected) {
                enet_peer_reset(conn->peer);
            }
        }
        // Quick shutdown
        else {
            enet_peer_reset(conn->peer);
        }

        os_thread_helper_stop(&conn->enet_thread);
        ALOGI("ENet thread stopped.");

        // Drop the queue after stopping the ENet thread.
        g_async_queue_unref(conn->packet_queue);

        enet_host_destroy(conn->client);
        enet_deinitialize();
    }
}

static void conn_on_ws_message_cb(SoupWebsocketConnection *connection, gint type, GBytes *message, MyConnection *conn) {
    // ALOGD("%s", __FUNCTION__);
    gsize length = 0;
    const gchar *msg_data = g_bytes_get_data(message, &length);
    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    // TODO convert gsize to gssize after range check

    //    if (json_parser_load_from_data(parser, msg_data, length, &error)) {
    //        JsonObject *msg = json_node_get_object(json_parser_get_root(parser));
    //        const gchar *msg_type;
    //
    //        if (!json_object_has_member(msg, "msg")) {
    //            // Invalid message
    //            goto out;
    //        }
    //
    //        msg_type = json_object_get_string_member(msg, "msg");
    //        // ALOGI("Websocket message received: %s", msg_type);
    //
    //        if (g_str_equal(msg_type, "offer")) {
    //            const gchar *offer_sdp = json_object_get_string_member(msg, "sdp");
    //            conn_webrtc_process_sdp_offer(conn, offer_sdp);
    //        } else if (g_str_equal(msg_type, "candidate")) {
    //            JsonObject *candidate;
    //
    //            candidate = json_object_get_object_member(msg, "candidate");
    //
    //            conn_webrtc_process_candidate(conn,
    //                                          json_object_get_int_member(candidate, "sdpMLineIndex"),
    //                                          json_object_get_string_member(candidate, "candidate"));
    //        }
    //    } else {
    //        g_debug("Error parsing message: %s", error->message);
    //        g_clear_error(&error);
    //    }

out:
    g_object_unref(parser);
}

static void conn_websocket_connected_cb(GObject *session, GAsyncResult *res, MyConnection *conn) {
    GError *error = NULL;

    g_assert(!conn->ws);

    conn->ws = g_object_ref_sink(soup_session_websocket_connect_finish(SOUP_SESSION(session), res, &error));

    if (error) {
        ALOGE("Websocket connection failed, error: '%s'", error->message);
        g_signal_emit(conn, signals[SIGNAL_WEBSOCKET_FAILED], 0);
        //        conn_update_status(conn, MY_STATUS_WEBSOCKET_FAILED);
        return;
    }
    g_assert_no_error(error);
    GstBus *bus;

    ALOGI("WebSocket connected");
    g_signal_connect(conn->ws, "message", G_CALLBACK(conn_on_ws_message_cb), conn);
    g_signal_emit(conn, signals[SIGNAL_WEBSOCKET_CONNECTED], 0);

    ALOGI("Creating pipeline upon WebSocket connection");
    g_assert_null(conn->pipeline);
    g_signal_emit(conn, signals[SIGNAL_ON_NEED_PIPELINE], 0);
    if (conn->pipeline == NULL) {
        ALOGE("on-need-pipeline signal did not return a pipeline!");
        my_connection_disconnect(conn);
        return;
    }

    // OK, if we get here, we have a websocket connection, and a pipeline fully configured
    // so we can start the pipeline playing

    ALOGI("Setting pipeline state to PLAYING");
    gst_element_set_state(GST_ELEMENT(conn->pipeline), GST_STATE_PLAYING);
    ALOGI("%s: Done with function", __FUNCTION__);
}

void my_connection_set_pipeline(MyConnection *conn, GstPipeline *pipeline) {
    g_assert_nonnull(pipeline);
    if (conn->pipeline) {
        // Stop old pipeline if applicable
        gst_element_set_state(GST_ELEMENT(conn->pipeline), GST_STATE_NULL);
    }
    gst_clear_object(&conn->pipeline);
    conn->pipeline = gst_object_ref_sink(pipeline);

    //    conn_update_status(conn, MY_STATUS_NEGOTIATING);
}

void handle_enet_event(ENetEvent *event) {
    switch (event->type) {
        case ENET_EVENT_TYPE_RECEIVE: {
            ALOGI("ENet received a packet.");
            enet_packet_destroy(event->packet);
        } break;
        case ENET_EVENT_TYPE_DISCONNECT: {
            ALOGI("ENet disconnected.");
        } break;
        case ENET_EVENT_TYPE_NONE: {
            ALOGI("ENet none event.");
        } break;
        case ENET_EVENT_TYPE_CONNECT: {
            ALOGI("ENet connected.");
        } break;
        case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT: {
            ALOGI("ENet disconnect timeout.");
        } break;
    }
}

static void *enet_thread_func(void *ptr) {
    MyConnection *conn = ptr;

    ENetEvent event = {0};

    while (conn->enet_thread.running) {
        if (!conn->client) {
            continue;
        }

        ENetPacket *packet;

        while ((packet = g_async_queue_try_pop(conn->packet_queue)) != NULL) {
            int channel_id = 0;
            int ret = enet_peer_send(conn->peer, channel_id, packet);
            if (ret) {
                ALOGE("enet_peer_send error: %d", ret);
                // Destroy the packet because ENet didn't accept it.
                enet_packet_destroy(packet);
            }
        }

        // Flush the host to ensure the packet is sent immediately
        enet_host_flush(conn->client);

        // Block for up to 10 milliseconds, or until an event occurs
        if (enet_host_service(conn->client, &event, 10) > 0) {
            // Handle the event
            handle_enet_event(&event);

            // Check for more events that might have arrived quickly
            while (enet_host_service(conn->client, &event, 0) > 0) {
                handle_enet_event(&event);
            }
        }
    }

    return NULL;
}

void my_connection_connect(MyConnection *conn) {
    // Reset previous connection.
    my_connection_disconnect(conn);
    if (!conn->ws_cancel) {
        conn->ws_cancel = g_cancellable_new();
    }
    g_cancellable_reset(conn->ws_cancel);

    ALOGI("Calling soup_session_websocket_connect_async. WebSocket URI: %s", conn->websocket_uri);

    soup_session_websocket_connect_async(conn->soup_session,                                     // session
                                         soup_message_new(SOUP_METHOD_GET, conn->websocket_uri), // message
                                         NULL,                                                   // origin
                                         NULL,                                                   // protocols
                                         0,                                                      // io_priority
                                         conn->ws_cancel,                                        // cancellable
                                         (GAsyncReadyCallback)conn_websocket_connected_cb,       // callback
                                         conn);                                                  // user_data

    conn_update_status(conn, MY_STATUS_CONNECTING);

    // ENet
    {
        ENetHost *client = {0};
        client = enet_host_create(NULL /* create a client host */,
                                  1 /* only allow 1 outgoing connection */,
                                  2 /* allow up 2 channels to be used, 0 and 1 */,
                                  0 /* assume any amount of incoming bandwidth */,
                                  0 /* assume any amount of outgoing bandwidth */);
        if (client == NULL) {
            ALOGE("An error occurred while trying to create an ENet client host.");
            exit(EXIT_FAILURE);
        }
        conn->client = client;

        ENetAddress address = {0};
        ENetPeer *peer = {0};
        enet_address_set_host(&address, conn->host_address);
        address.port = 7777;

        /* Initiate the connection, allocating the two channels 0 and 1. */
        peer = enet_host_connect(client, &address, 2, 0);
        if (peer == NULL) {
            ALOGE("No available peers for initiating an ENet connection.");
            exit(EXIT_FAILURE);
        }
        conn->peer = peer;

        conn->packet_queue = g_async_queue_new_full((GDestroyNotify)enet_packet_destroy);

        int ret = os_thread_helper_start(&conn->enet_thread, &enet_thread_func, conn);
        (void)ret;
        g_assert(ret == 0);
    }
}

/* public (non-GObject) methods */

MyConnection *my_connection_new(const gchar *websocket_uri, const gchar *host_address) {
    if (enet_initialize() != 0) {
        printf("An error occurred while initializing ENet.\n");
        abort();
    }

    ALOGI("New connection to: %s", websocket_uri);

    MyConnection *conn = MY_CONNECTION(
        g_object_new(MY_TYPE_CONNECTION, "websocket-uri", websocket_uri, "host-address", host_address, NULL));

    g_assert(os_thread_helper_init(&conn->enet_thread) >= 0);

    return conn;
}

MyConnection *my_connection_new_localhost() {
    if (enet_initialize() != 0) {
        printf("An error occurred while initializing ENet.\n");
        abort();
    }

    MyConnection *conn = MY_CONNECTION(g_object_new(MY_TYPE_CONNECTION, NULL));

    g_assert(os_thread_helper_init(&conn->enet_thread) >= 0);

    return conn;
}

bool my_connection_send_bytes(MyConnection *conn, GBytes *bytes) {
    if (conn->status != MY_STATUS_CONNECTED) {
        ALOGW("Cannot send bytes when status is %s", my_status_to_string(conn->status));
        return false;
    }

    return TRUE;
}

void my_connection_send_input_event_via_json(MyConnection *conn, int type, float x, float y) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "msg-type");
    json_builder_add_string_value(builder, "input");

    json_builder_set_member_name(builder, "input-type");
    json_builder_add_int_value(builder, type);

    json_builder_set_member_name(builder, "x");
    json_builder_add_double_value(builder, x);

    json_builder_set_member_name(builder, "y");
    json_builder_add_double_value(builder, y);

    json_builder_end_object(builder);

    JsonNode *root = json_builder_get_root(builder);

    gchar *msg_str = json_to_string(root, TRUE);
    ALOGI("Sent input message: %s", msg_str);

    soup_websocket_connection_send_text(conn->ws, msg_str);

    g_clear_pointer(&msg_str, g_free);

    json_node_unref(root);
    g_object_unref(builder);
}

const int COMMAND_SIZE = sizeof(InputCommand);

void my_connection_send_input_command_via_enet(MyConnection *conn, InputCommand *input_data) {
    ENetPacketFlag flag = ENET_PACKET_FLAG_RELIABLE;

    // For input commands that are not important.
    if (input_data->type == CursorMove || input_data->type == GamepadLeftStick ||
        input_data->type == GamepadRightStick) {
        flag = ENET_PACKET_FLAG_UNSEQUENCED;
    }

    // To guarantee that the first byte of your ENet packet is exactly input_type and to avoid any compiler-imposed
    // memory alignment headaches on ARM, you must create a plain char[] buffer and manually copy/serialize the fields
    // one by one on the C++ client side.
    //
    // This is the only way to be 100% certain of the memory layout on an ambiguous architecture like ARM/Android.
    uint8_t buffer[sizeof(InputCommand)];
    size_t offset = 0;
    {
        // 1. input_type (u8): Copy raw byte
        // This MUST be the first byte in the packet.
        buffer[offset++] = input_data->type;

        // 2. x_value (i32): Copy raw bytes
        // We assume ARM/Android is Little-Endian, so raw copy is sufficient.
        // Use htonl/ntohl if you wanted Big-Endian (Network Byte Order).
        memcpy(buffer + offset, &input_data->data0, sizeof(int32_t));
        offset += sizeof(int32_t);

        // 3. y_value (i32): Copy raw bytes
        memcpy(buffer + offset, &input_data->data1, sizeof(int32_t));
        offset += sizeof(int32_t);
    }

    // 5. Create and send the ENet packet from the guaranteed-clean buffer
    if (offset == COMMAND_SIZE) {
        ENetPacket *packet = enet_packet_create(buffer, COMMAND_SIZE, flag);
        if (packet) {
            // We cannot send it directly from here, as ENet is not thread safe.
            g_async_queue_push(conn->packet_queue, packet);
        }
    } else {
        ALOGE("Wrong command size: %zu", offset);
    }
}

void my_connection_send_input_event(MyConnection *conn, int type, float x, float y) {
    InputCommand cmd = {0};
    cmd.type = type;
    memcpy(&cmd.data0, &x, sizeof(uint32_t));
    memcpy(&cmd.data1, &y, sizeof(uint32_t));

    my_connection_send_input_command_via_enet(conn, &cmd);
}
