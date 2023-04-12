//
// Copyright (c) 2021-2022 dushin.net
// All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "atomvm_mqtt_client.h"

#include <atom.h>
#include <bif.h>
#include <context.h>
#include <debug.h>
#include <esp32_sys.h>
#include <defaultatoms.h>
#include <globalcontext.h>
#include <interop.h>
#include <mailbox.h>
#include <module.h>
#include <port.h>
#include <scheduler.h>
#include <term.h>
#include <utils.h>

#include <esp_log.h>
#include <esp_mac.h>
#include <mqtt_client.h>

// #define ENABLE_TRACE
#include <trace.h>

#define TAG "atomvm_mqtt"

// static const char *const cert_atom =                    ATOM_STR("\x4", "cert");
static const char *const connected_atom =               ATOM_STR("\x9", "connected");
static const char *const data_atom =                    ATOM_STR("\x4", "data");
static const char *const disconnected_atom =            ATOM_STR("\xC", "disconnected");
static const char *const host_atom =                    ATOM_STR("\x4", "host");
static const char *const mqtt_atom =                    ATOM_STR("\x4", "mqtt");
static const char *const password_atom =                ATOM_STR("\x8", "password");
static const char *const port_atom =                    ATOM_STR("\x4", "port");
static const char *const published_atom =               ATOM_STR("\x9", "published");
static const char *const receiver_atom =                ATOM_STR("\x8", "receiver");
static const char *const subscribed_atom =              ATOM_STR("\xA", "subscribed");
// static const char *const transport_atom =               ATOM_STR("\x9", "transport");
static const char *const unsubscribed_atom =            ATOM_STR("\xC", "unsubscribed");
static const char *const url_atom =                     ATOM_STR("\x3", "url");
static const char *const username_atom =                ATOM_STR("\x8", "username");

// error codes
static const char *const bad_username_atom =            ATOM_STR("\x0C", "bad_username");
static const char *const connection_accepted_atom =     ATOM_STR("\x13", "connection_accepted");
static const char *const connection_refused_atom =      ATOM_STR("\x12", "connection_refused");
static const char *const esp_tls_atom =                 ATOM_STR("\x07", "esp_tls");
static const char *const id_rejected_atom =             ATOM_STR("\x0B", "id_rejected");
static const char *const not_authorized_atom =          ATOM_STR("\x0E", "not_authorized");
static const char *const protocol_atom =                ATOM_STR("\x08", "protocol");
static const char *const server_unavailable_atom =      ATOM_STR("\x12", "server_unavailable");

enum mqtt_cmd
{
    MQTTInvalidCmd = 0,
    MQTTStopCmd,
    MQTTDisconnectCmd,
    MQTTReconnectCmd,
    MQTTPublishCmd,
    MQTTSubscribeCmd,
    MQTTUnSubscribeCmd
};

static const AtomStringIntPair cmd_table[] = {
    { ATOM_STR("\x4", "stop"), MQTTStopCmd },
    { ATOM_STR("\xA", "disconnect"), MQTTDisconnectCmd },
    { ATOM_STR("\x9", "reconnect"), MQTTReconnectCmd },
    { ATOM_STR("\x7", "publish"), MQTTPublishCmd },
    { ATOM_STR("\x9", "subscribe"), MQTTSubscribeCmd },
    { ATOM_STR("\xB", "unsubscribe"), MQTTUnSubscribeCmd },
    SELECT_INT_DEFAULT(MQTTInvalidCmd)
};

// TODO support configuration of MQTT transport

// enum mqtt_transport
// {
//     MQTTInvalidTransport = 0,
//     MQTTMQTTTransport,
//     MQTTMQTTSTransport,
//     MQTTWSTransport,
//     MQTTWSSTransport
// };

// static const AtomStringIntPair transport_table[] = {
//     { ATOM_STR("\x4", "mqtt"), MQTTMQTTTransport },
//     { ATOM_STR("\x5", "mqtts"), MQTTMQTTSTransport },
//     { ATOM_STR("\x2", "ws"), MQTTWSTransport },
//     { ATOM_STR("\x3", "wss"), MQTTWSSTransport },
//     SELECT_INT_DEFAULT(MQTTInvalidTransport)
// };

struct platform_data {
    esp_mqtt_client_handle_t client;
    term receiver;
};

static term make_atom(GlobalContext *global, const char *string)
{
    int global_atom_index = globalcontext_insert_atom(global, (AtomString) string);
    return term_from_atom_index(global_atom_index);
}

static term create_tuple4(Context *ctx, term a, term b, term c, term d)
{
    term terms[4];
    terms[0] = a;
    terms[1] = b;
    terms[2] = c;
    terms[3] = d;

    return port_create_tuple_n(ctx, 4, terms);
}

static term create_tuple5(Context *ctx, term a, term b, term c, term d, term e)
{
    term terms[4];
    terms[0] = a;
    terms[1] = b;
    terms[2] = c;
    terms[3] = d;
    terms[4] = e;

    return port_create_tuple_n(ctx, 5, terms);
}

static term error_type_to_atom(GlobalContext *global, esp_mqtt_error_type_t error_type)
{
    switch (error_type) {
        case MQTT_ERROR_TYPE_ESP_TLS:
            return make_atom(global, esp_tls_atom);
        case MQTT_ERROR_TYPE_CONNECTION_REFUSED:
            return make_atom(global, connection_refused_atom);
        default:
            return UNDEFINED_ATOM;
    }
}

static char *get_default_client_id()
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    size_t buf_size = strlen("atomvm-") + 12 + 1;
    char *buf = malloc(buf_size);
    if (IS_NULL_PTR(buf)) {
        ESP_LOGE(TAG, "Failed to allocate client_id buf");
        return NULL;
    }
    snprintf(buf, buf_size,
        "atomvm-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

static term connect_return_code_to_atom(GlobalContext *global, esp_mqtt_connect_return_code_t connect_return_code)
{
    switch (connect_return_code) {
        case MQTT_CONNECTION_ACCEPTED:
            return make_atom(global, connection_accepted_atom);
        case MQTT_CONNECTION_REFUSE_PROTOCOL:
            return make_atom(global, protocol_atom);
        case MQTT_CONNECTION_REFUSE_ID_REJECTED:
            return make_atom(global, id_rejected_atom);
        case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE:
            return make_atom(global, server_unavailable_atom);
        case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
            return make_atom(global, bad_username_atom);
        case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
            return make_atom(global, not_authorized_atom);
        default:
            return UNDEFINED_ATOM;
    }
}

static term do_publish(Context *ctx, term topic, term data, term qos, term retain)
{
    struct platform_data *plfdat = (struct platform_data *) ctx->platform_data;
    esp_mqtt_client_handle_t client = plfdat->client;

    int ok;
    char *topic_str = interop_term_to_string(topic, &ok);
    if (!ok) {
        return BADARG_ATOM;
    }

    TRACE(TAG ": do_publish topic=%s\n", topic_str);
    int msg_id = esp_mqtt_client_publish(
        client,
        topic_str,
        term_binary_data(data),
        term_binary_size(data),
        term_to_int(qos),
        retain == TRUE_ATOM ? 1 : 0
    );
    free(topic_str);

    if (msg_id == -1) {
        ESP_LOGE(TAG, "Error: unable to publish to topic.\n");
        return ERROR_ATOM;
    }

    return term_from_int(msg_id);
}


static term do_subscribe(Context *ctx, term topic, term qos)
{
    struct platform_data *plfdat = (struct platform_data *) ctx->platform_data;
    esp_mqtt_client_handle_t client = plfdat->client;

    int ok;
    char *topic_str = interop_term_to_string(topic, &ok);
    if (!ok) {
        return BADARG_ATOM;
    }

    TRACE(TAG ": do_subscribe topic=%s\n", topic_str);
    int msg_id = esp_mqtt_client_subscribe(
        client,
        topic_str,
        term_to_int(qos)
    );
    free(topic_str);

    if (msg_id == -1) {
        ESP_LOGE(TAG, "Error: unable to subscribe to topic.\n");
        return ERROR_ATOM;
    }

    return term_from_int(msg_id);
}


static term do_unsubscribe(Context *ctx, term topic)
{
    struct platform_data *plfdat = (struct platform_data *) ctx->platform_data;
    esp_mqtt_client_handle_t client = plfdat->client;

    int ok;
    char *topic_str = interop_term_to_string(topic, &ok);
    if (!ok) {
        return BADARG_ATOM;
    }

    TRACE(TAG ": do_unsubscribe topic=%s\n", topic_str);
    int msg_id = esp_mqtt_client_unsubscribe(
        client,
        topic_str
    );
    free(topic_str);

    if (msg_id == -1) {
        ESP_LOGE(TAG, "Error: unable to unsubscribe from topic.\n");
        return ERROR_ATOM;
    }

    return term_from_int(msg_id);
}


static void do_stop(Context *ctx)
{
    struct platform_data *plfdat = (struct platform_data *) ctx->platform_data;
    esp_mqtt_client_handle_t client = plfdat->client;

    TRACE(TAG ": do_stop\n");
    esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
    scheduler_terminate(ctx);
    free(plfdat);
}


static term do_disconnect(Context *ctx)
{
    struct platform_data *plfdat = (struct platform_data *) ctx->platform_data;
    esp_mqtt_client_handle_t client = plfdat->client;

    TRACE(TAG ": do_disconnect\n");
    esp_err_t status = esp_mqtt_client_disconnect(client);

    if (status == ESP_OK) {
        ESP_LOGE(TAG, "Error: unable to disconnect from MQTT Broker.\n");
        return ERROR_ATOM;
    }

    return OK_ATOM;
}


static term do_reconnect(Context *ctx)
{
    struct platform_data *plfdat = (struct platform_data *) ctx->platform_data;
    esp_mqtt_client_handle_t client = plfdat->client;

    TRACE(TAG ": do_reconnect\n");
    esp_err_t status = esp_mqtt_client_reconnect(client);

    if (status == ESP_OK) {
        ESP_LOGE(TAG, "Error: unable to reconnect to MQTT Broker.\n");
        return ERROR_ATOM;
    }

    return OK_ATOM;
}


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    Context *ctx = (Context *) event->user_context;
    struct platform_data *plfdat = (struct platform_data *) ctx->platform_data;
    int pid = term_to_local_process_id(plfdat->receiver);
    Context *target = globalcontext_get_process(ctx->global, pid);

    esp_mqtt_event_id_t mqtt_event_id = (esp_mqtt_event_id_t) event_id;
    switch (mqtt_event_id) {

        case MQTT_EVENT_CONNECTED: {
            TRACE(TAG ": MQTT_EVENT_CONNECTED\n");

            if (UNLIKELY(memory_ensure_free(ctx, TUPLE_SIZE(2)) != MEMORY_GC_OK)) {
                mailbox_send(target, MEMORY_ATOM);
                return;
            }

            term msg = port_create_tuple2(
                ctx,
                make_atom(ctx->global, mqtt_atom),
                make_atom(ctx->global, connected_atom)
            );

            mailbox_send(target, msg);
            break;
        }

        case MQTT_EVENT_DISCONNECTED: {
            TRACE(TAG ": MQTT_EVENT_DISCONNECTED\n");

            if (UNLIKELY(memory_ensure_free(ctx, TUPLE_SIZE(2)) != MEMORY_GC_OK)) {
                mailbox_send(target, MEMORY_ATOM);
                return;
            }

            term msg = port_create_tuple2(
                ctx,
                make_atom(ctx->global, mqtt_atom),
                make_atom(ctx->global, disconnected_atom)
            );

            mailbox_send(target, msg);
            break;
        }

        case MQTT_EVENT_SUBSCRIBED: {
            TRACE(TAG ": MQTT_EVENT_SUBSCRIBED, msg_id=%d\n", event_id);

            if (UNLIKELY(memory_ensure_free(ctx, TUPLE_SIZE(3)) != MEMORY_GC_OK)) {
                mailbox_send(target, MEMORY_ATOM);
                return;
            }

            term msg = port_create_tuple3(
                ctx,
                make_atom(ctx->global, mqtt_atom),
                make_atom(ctx->global, subscribed_atom),
                term_from_int(event->msg_id)
            );

            mailbox_send(target, msg);
            break;
        }

        case MQTT_EVENT_UNSUBSCRIBED: {
            TRACE(TAG ": MQTT_EVENT_UNSUBSCRIBED, msg_id=%d\n", event_id);

            if (UNLIKELY(memory_ensure_free(ctx, TUPLE_SIZE(3)) != MEMORY_GC_OK)) {
                mailbox_send(target, MEMORY_ATOM);
                return;
            }

            term msg = port_create_tuple3(
                ctx,
                make_atom(ctx->global, mqtt_atom),
                make_atom(ctx->global, unsubscribed_atom),
                term_from_int(event->msg_id)
            );

            mailbox_send(target, msg);
            break;
        }

        case MQTT_EVENT_PUBLISHED: {
            TRACE(TAG ": MQTT_EVENT_PUBLISHED, msg_id=%d\n", event->msg_id);

            if (UNLIKELY(memory_ensure_free(ctx, TUPLE_SIZE(3)) != MEMORY_GC_OK)) {
                mailbox_send(target, MEMORY_ATOM);
                return;
            }

            term msg = port_create_tuple3(
                ctx,
                make_atom(ctx->global, mqtt_atom),
                make_atom(ctx->global, published_atom),
                term_from_int(event->msg_id)
            );

            mailbox_send(target, msg);
            break;
        }

        case MQTT_EVENT_DATA: {
            TRACE(TAG ": MQTT_EVENT_DATA, event_id=%d\n", event_id);
            TRACE(TAG ": TOPIC=%.*s\n", event->topic_len, event->topic);
            TRACE(TAG ": DATA=%.*s\n", event->data_len, event->data);

            int topic_size = term_binary_data_size_in_terms(event->topic_len);
            int data_size = term_binary_data_size_in_terms(event->data_len);
            if (UNLIKELY(memory_ensure_free(ctx, TUPLE_SIZE(4) + topic_size + data_size) != MEMORY_GC_OK)) {
                mailbox_send(target, MEMORY_ATOM);
                return;
            }

            term topic = term_from_literal_binary(event->topic, event->topic_len, ctx);
            term data = term_from_literal_binary(event->data, event->data_len, ctx);
            term msg = create_tuple4(
                ctx,
                make_atom(ctx->global, mqtt_atom),
                make_atom(ctx->global, data_atom),
                topic, data
            );

            mailbox_send(target, msg);
            break;
        }

        case MQTT_EVENT_ERROR: {
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");

            // {mqtt, {ErrorType :: atom(), ConnectReturnCode :: atom(), tls_last_esp_err :: integer(), tls_stack_err :: integer(), tls_cert_verify_flags :: integer()}}
            size_t heap_size = TUPLE_SIZE(3) + TUPLE_SIZE(5);
            if (UNLIKELY(memory_ensure_free(ctx, heap_size) != MEMORY_GC_OK)) {
                mailbox_send(target, MEMORY_ATOM);
                return;
            }

            esp_mqtt_error_codes_t *mqtt_error = event->error_handle;
            term error = create_tuple5(
                ctx,
                error_type_to_atom(ctx->global, mqtt_error->error_type),
                connect_return_code_to_atom(ctx->global, mqtt_error->connect_return_code),
                term_from_int(mqtt_error->esp_tls_last_esp_err),
                term_from_int(mqtt_error->esp_tls_stack_err),
                term_from_int(mqtt_error->esp_tls_cert_verify_flags)
            );

            term msg = port_create_tuple3(
                ctx,
                make_atom(ctx->global, mqtt_atom),
                ERROR_ATOM,
                error
            );

            mailbox_send(target, msg);
            break;
        }

        case MQTT_EVENT_BEFORE_CONNECT: {
            ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT event_id: %d", event_id);
            break;
        }

        default:
            ESP_LOGW(TAG, "Other event.  event_id: %d", event_id);
            break;
    }
}


static void consume_mailbox(Context *ctx)
{
    Message *message = mailbox_dequeue(ctx);
    term msg = message->message;
    term pid = term_get_tuple_element(msg, 0);
    term ref = term_get_tuple_element(msg, 1);
    term req = term_get_tuple_element(msg, 2);

    int local_process_id = term_to_local_process_id(pid);
    Context *target = globalcontext_get_process(ctx->global, local_process_id);

    term ret = ERROR_ATOM;


    if (term_is_atom(req)) {

        int cmd = interop_atom_term_select_int(cmd_table, req, ctx->global);
        switch (cmd) {

            case MQTTStopCmd:
                do_stop(ctx);
                break;

            case MQTTDisconnectCmd:
                ret = do_disconnect(ctx);
                break;

            case MQTTReconnectCmd:
                ret = do_reconnect(ctx);
                break;

            default:
                ESP_LOGE(TAG, "Unknown command");
                break;
        }

    } else if (term_is_tuple(req) && term_get_tuple_arity(req) > 0) {

        int cmd = interop_atom_term_select_int(cmd_table, term_get_tuple_element(req, 0), ctx->global);
        switch (cmd) {
            case MQTTPublishCmd: {
                    term topic = term_get_tuple_element(req, 1);
                    term data = term_get_tuple_element(req, 2);
                    term qos = term_get_tuple_element(req, 3);
                    term retain = term_get_tuple_element(req, 4);
                    ret = do_publish(ctx, topic, data, qos, retain);
                }
                break;

            case MQTTSubscribeCmd: {
                    term topic = term_get_tuple_element(req, 1);
                    term qos = term_get_tuple_element(req, 2);
                    ret = do_subscribe(ctx, topic, qos);
                }
                break;

            case MQTTUnSubscribeCmd: {
                    term topic = term_get_tuple_element(req, 1);
                    ret = do_unsubscribe(ctx, topic);
                }
                break;

            default:
                ESP_LOGE(TAG, "Unknown command");
                break;
        }
    } else {
        ESP_LOGE(TAG, "Invalid command");
    }

    // {Ref, ok | error}
    size_t heap_size = TUPLE_SIZE(2) + REF_SIZE;
    if (UNLIKELY(memory_ensure_free(ctx, heap_size) != MEMORY_GC_OK)) {
        mailbox_send(target, MEMORY_ATOM);
    } else {
        term ret_msg = port_create_tuple2(ctx, ref, ret);
        mailbox_send(target, ret_msg);
    }

    mailbox_destroy_message(message);
}

//
// entrypoints
//

void atomvm_mqtt_client_init(GlobalContext *global)
{
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
}

// NB. Caller assumes ownership of returned string
static char *maybe_get_string(term kv, AtomString key, GlobalContext *global)
{
    term value_term = interop_kv_get_value(kv, key, global);
    if (!term_is_string(value_term) && !term_is_binary(value_term)) {
        return NULL;
    }

    int ok;
    char *value_str = interop_term_to_string(value_term, &ok);
    if (UNLIKELY(!ok)) {
        ESP_LOGE(TAG, "Error: value is not a proper string or binary.");
        return NULL;
    }
    return value_str;
}

// NB. Caller assumes ownership of returned string
static char *get_string_default(term kv, AtomString key, AtomString default_value, GlobalContext *global)
{
    term value_term = interop_kv_get_value(kv, key, global);
    if (!term_is_string(value_term) && !term_is_binary(value_term)) {
        int len = atom_string_len(default_value);
        char *buf = malloc(len + 1);
        if (IS_NULL_PTR(buf)) {
            ESP_LOGW(TAG, "Unable to allocate memory for default value");
            return NULL;
        }
        atom_string_to_c(default_value, buf, len);
        return buf;
    }

    int ok;
    char *value_str = interop_term_to_string(value_term, &ok);
    if (UNLIKELY(!ok)) {
        ESP_LOGE(TAG, "Error: value is not a proper string or binary.");
        return NULL;
    }
    return value_str;
}

// static esp_mqtt_transport_t get_transport(term kv, GlobalContext *global)
// {
//     int transport = interop_atom_term_select_int(transport_table, transport_atom, global);
//     switch (transport) {
//         case MQTTMQTTTransport:
//             return MQTT_TRANSPORT_OVER_TCP;
//         case MQTTMQTTSTransport:
//             return MQTT_TRANSPORT_OVER_SSL;
//         case MQTTWSTransport:
//             return MQTT_TRANSPORT_OVER_WS;
//         case MQTTWSSTransport:
//             return MQTT_TRANSPORT_OVER_WSS;
//         default:
//             ESP_LOGW(TAG, "Unknown transport");
//             return MQTT_TRANSPORT_UNKNOWN;
//     }
// }

Context *atomvm_mqtt_client_create_port(GlobalContext *global, term opts)
{
    term receiver_term = interop_kv_get_value(opts, receiver_atom, global);
    if (UNLIKELY(!term_is_pid(receiver_term))) {
        ESP_LOGE(TAG, "Missing receiver pid during port creation");
        return NULL;
    }

    Context *ctx = context_new(global);
    ctx->native_handler = consume_mailbox;

    struct platform_data *plfdat = malloc(sizeof(struct platform_data));
    plfdat->receiver = receiver_term;
    ctx->platform_data = plfdat;

    char *url_str = maybe_get_string(opts, url_atom, global);
    // esp_mqtt_transport_t transport = get_transport(opts, global);
    char *host_str = maybe_get_string(opts, host_atom, global);
    term port_term = interop_kv_get_value(opts, port_atom, global);
    int port = 0;
    if (term_is_integer(port_term)) {
        port = term_from_int(port_term);
    }
    char *username_str = maybe_get_string(opts, username_atom, global);
    char *password_str = maybe_get_string(opts, password_atom, global);
    // char *cert_str = maybe_get_string(opts, cert_atom, global);

    // Note that char * values passed into this struct are copied into the MQTT state
    const char *client_id = get_default_client_id();
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = url_str,
        .client_id = client_id,
        .user_context = (void *) ctx
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);

    free(url_str);
    free(host_str);
    free(username_str);
    free(password_str);

    if (UNLIKELY(IS_NULL_PTR(client))) {
        ESP_LOGE(TAG, "Error: Unable to initialize MQTT client.\n");
        return NULL;
    }

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, ctx);

    esp_err_t err = esp_mqtt_client_start(client);
    if (err != ESP_OK) {
        context_destroy(ctx);
        ESP_LOGE(TAG, "Error: Unable to start MQTT client.  Error: %i.\n", err);
        return NULL;
    }
    plfdat->client = client;

    TRACE(TAG ": MQTT started.\n");
    return ctx;
}

#include <sdkconfig.h>

#ifdef CONFIG_AVM_MQTT_CLIENT_ENABLE

REGISTER_PORT_DRIVER(atomvm_mqtt_client, atomvm_mqtt_client_init, atomvm_mqtt_client_create_port)

#endif
