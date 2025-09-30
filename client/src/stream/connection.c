#include "connection.h"

#include <gst/gstelement.h>
#include <gst/gstobject.h>
#include <stdbool.h>
#include <string.h>

#include "utils/logger.h"
#include "status.h"

#define GST_USE_UNSTABLE_API

#include <gst/webrtc/webrtc.h>

#undef GST_USE_UNSTABLE_API

#include <json-glib/json-glib.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-session.h>

#define DEFAULT_WEBSOCKET_URI "ws://192.168.31.187:5600/ws"

/*!
 * Data required for the handshake to complete and to maintain the connection.
 */
struct _MyConnection {
    GObject parent;
    SoupSession *soup_session;
    gchar *websocket_uri;

    /// Cancellable for websocket connection process
    GCancellable *ws_cancel;
    SoupWebsocketConnection *ws;

    GstPipeline *pipeline;

    enum my_status status;
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
    // PROP_STATUS,
    N_PROPERTIES
} MyConnectionProperty;

static GParamSpec *properties[N_PROPERTIES] = {
        NULL,
};

/* GObject method implementations */

static void my_connection_set_property(GObject *object, guint property_id, const GValue *value,
                                       GParamSpec *pspec) {
    MyConnection *self = MY_CONNECTION(object);

    switch ((MyConnectionProperty) property_id) {
        case PROP_WEBSOCKET_URI:
            g_free(self->websocket_uri);
            self->websocket_uri = g_value_dup_string(value);
            ALOGI("Websocket URI assigned: %s", self->websocket_uri);
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
    ALOGI("conn: state update: %s -> %s", my_status_to_string(conn->status),
          my_status_to_string(status));
    conn->status = status;
}

static void
my_connection_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    MyConnection *self = MY_CONNECTION(object);

    switch ((MyConnectionProperty) property_id) {
        case PROP_WEBSOCKET_URI:
            g_value_set_string(value, self->websocket_uri);
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
                                G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                                G_PARAM_STATIC_STRINGS));

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
                                                              G_CALLBACK(
                                                                      my_connection_set_pipeline),
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

#define MAKE_CASE(E) \
    case E:          \
        return #E

static const char *peer_connection_state_to_string(GstWebRTCPeerConnectionState state) {
    switch (state) {
        MAKE_CASE(GST_WEBRTC_PEER_CONNECTION_STATE_NEW);
        MAKE_CASE(GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTING);
        MAKE_CASE(GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED);
        MAKE_CASE(GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED);
        MAKE_CASE(GST_WEBRTC_PEER_CONNECTION_STATE_FAILED);
        MAKE_CASE(GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED);
        default:
            return "!Unknown!";
    }
}

static void conn_disconnect_internal(MyConnection *conn, enum my_status status) {
    if (conn->ws_cancel != NULL) {
        g_cancellable_cancel(conn->ws_cancel);
        gst_clear_object(&conn->ws_cancel);
    }
    // Stop the pipeline, if it exists
    if (conn->pipeline != NULL) {
        gst_element_set_state(GST_ELEMENT(conn->pipeline), GST_STATE_NULL);
        g_signal_emit(conn, signals[SIGNAL_ON_DROP_PIPELINE], 0);
    }
    if (conn->ws) {
        soup_websocket_connection_close(conn->ws, 0, "");
    }
    g_clear_object(&conn->ws);

    gst_clear_object(&conn->pipeline);
}

static void conn_connect_internal(MyConnection *conn, enum my_status status);

static void conn_webrtc_deep_notify_callback(GstObject *self,
                                             GstObject *prop_object,
                                             GParamSpec *prop,
                                             MyConnection *conn) {
    GstWebRTCPeerConnectionState state;
    g_object_get(prop_object, "connection-state", &state, NULL);
    ALOGI("deep-notify callback says peer connection state is %s - but it lies sometimes",
          peer_connection_state_to_string(state));
    //	conn_update_status_from_peer_connection_state(conn, state);
}


static void conn_on_ws_message_cb(SoupWebsocketConnection *connection, gint type, GBytes *message,
                                  MyConnection *conn) {
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

    conn->ws = g_object_ref_sink(
            soup_session_websocket_connect_finish(SOUP_SESSION(session), res, &error));

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

    ALOGI("Creating pipeline");
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

static void conn_connect_internal(MyConnection *conn, enum my_status status) {
    my_connection_disconnect(conn);
    if (!conn->ws_cancel) {
        conn->ws_cancel = g_cancellable_new();
    }
    g_cancellable_reset(conn->ws_cancel);

    ALOGI("calling soup_session_websocket_connect_async. websocket_uri = %s", conn->websocket_uri);

    soup_session_websocket_connect_async(
            conn->soup_session,                                     // session
            soup_message_new(SOUP_METHOD_GET, conn->websocket_uri), // message
            NULL,                                                   // origin
            NULL,                                                   // protocols
            0,                                                      // io_prority
            conn->ws_cancel,                                        // cancellable
            (GAsyncReadyCallback) conn_websocket_connected_cb,       // callback
            conn);                                                  // user_data

//    conn_update_status(conn, status);
}

/* public (non-GObject) methods */

MyConnection *my_connection_new(const gchar *websocket_uri) {
    return MY_CONNECTION(g_object_new(MY_TYPE_CONNECTION, "websocket-uri", websocket_uri, NULL));
}

MyConnection *my_connection_new_localhost() {
    return MY_CONNECTION(g_object_new(MY_TYPE_CONNECTION, NULL));
}

void my_connection_connect(MyConnection *conn) {
    conn_connect_internal(conn, MY_STATUS_CONNECTING);
}

void my_connection_disconnect(MyConnection *conn) {
    if (conn) {
        conn_disconnect_internal(conn, MY_STATUS_IDLE_NOT_CONNECTED);
    }
}

bool my_connection_send_bytes(MyConnection *conn, GBytes *bytes) {
    if (conn->status != MY_STATUS_CONNECTED) {
        ALOGW("Cannot send bytes when status is %s", my_status_to_string(conn->status));
        return false;
    }


    return TRUE;
}

void my_connection_send_input_event(MyConnection *conn, int type, float x, float y) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "msg");
    json_builder_add_string_value(builder, "input");

    json_builder_set_member_name(builder, "type");
    json_builder_add_int_value(builder, type);

    json_builder_set_member_name(builder, "x");
    json_builder_add_double_value(builder, x);

    json_builder_set_member_name(builder, "y");
    json_builder_add_double_value(builder, y);

    json_builder_end_object(builder);

    JsonNode *root = json_builder_get_root(builder);

    gchar *msg_str = json_to_string(root, TRUE);
    soup_websocket_connection_send_text(conn->ws, msg_str);
    g_clear_pointer(&msg_str, g_free);

    json_node_unref(root);
    g_object_unref(builder);

    ALOGI("Sent input event");
}
