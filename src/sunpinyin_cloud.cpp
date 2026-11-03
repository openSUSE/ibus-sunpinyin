// SPDX-License-Identifier: LGPL-2.1-only OR CDDL-1.0
// Copyright (c) 2026 Hillwood Yang
#include "sunpinyin_cloud.h"
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <algorithm>

struct SunPinyinCloud::State {
    SoupSession* session = soup_session_new_with_options("timeout", 5, nullptr);
    GCancellable* cancellable = nullptr;
    guint timer = 0;
    guint64 generation = 0;
    ~State() {
        if (cancellable) g_object_unref(cancellable);
        g_object_unref(session);
    }
};
SunPinyinCloud::SunPinyinCloud() : m_state(std::make_shared<State>()) {}
SunPinyinCloud::~SunPinyinCloud() { cancel(); }
void SunPinyinCloud::cancel() {
    ++m_state->generation;
    if (m_state->timer) {
        g_source_remove(m_state->timer);
        m_state->timer = 0;
    }
    if (m_state->cancellable) {
        g_cancellable_cancel(m_state->cancellable);
        g_clear_object(&m_state->cancellable);
    }
}

SunPinyinCloud::Results
SunPinyinCloud::parse_response(const char* data, size_t length, const std::string& pinyin) {
    Results result;
    if (!data || length > 65536) return result;
    JsonParser* parser = json_parser_new();
    if (!json_parser_load_from_data(parser, data, length, nullptr)) {
        g_object_unref(parser);
        return result;
    }
    auto array = [](JsonNode* n) -> JsonArray* {
        return n && JSON_NODE_HOLDS_ARRAY(n) ? json_node_get_array(n) : nullptr;
    };
    auto element = [](JsonArray* a, guint i) -> JsonNode* {
        return a && i < json_array_get_length(a) ? json_array_get_element(a, i) : nullptr;
    };
    auto string = [](JsonNode* n) -> const char* {
        return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_STRING
            ? json_node_get_string(n) : nullptr;
    };
    JsonArray* root = array(json_parser_get_root(parser));
    const char* status = string(element(root, 0));
    JsonArray* entry = array(element(array(element(root, 1)), 0));
    const char* echoed = string(element(entry, 0));
    if (status && g_str_equal(status, "SUCCESS") && echoed && pinyin == echoed) {
        JsonArray* words = array(element(entry, 1));
        for (guint i = 0; words && i < json_array_get_length(words) && result.size() < 9; ++i) {
            const char* word = string(element(words, i));
            if (!word || !*word || !g_utf8_validate(word, -1, nullptr) ||
                g_utf8_strlen(word, -1) > 128) continue;
            bool chinese = true;
            for (const char* p = word; *p; p = g_utf8_next_char(p)) {
                if (!g_unichar_iswide(g_utf8_get_char(p)) || g_unichar_iscntrl(g_utf8_get_char(p))) {
                    chinese = false; break;
                }
            }
            if (chinese && std::find(result.begin(), result.end(), word) == result.end())
                result.emplace_back(word);
        }
    }
    g_object_unref(parser);
    return result;
}

void SunPinyinCloud::query(const std::string& pinyin, int source, int count, int delay_ms, Callback callback) {
    cancel();
    // No raw double-pinyin, selected Chinese text, or arbitrarily long input.
    if (pinyin.size() < 2 || pinyin.size() > 128 ||
        pinyin.find_first_not_of("abcdefghijklmnopqrstuvwxyz'") != std::string::npos)
        return;
    count = std::clamp(count, 1, 9);
    delay_ms = std::clamp(delay_ms, 0, 3000);
    struct Request {
        std::shared_ptr<State> state;
        guint64 generation;
        std::string pinyin;
        int source;
        int count;
        Callback callback;
        SoupMessage* message = nullptr;
        GInputStream* stream = nullptr;
        std::string body;
        Request(std::shared_ptr<State> state_, guint64 generation_, std::string pinyin_,
                int source_, int count_, Callback callback_)
            : state(std::move(state_)), generation(generation_), pinyin(std::move(pinyin_)),
              source(source_), count(count_), callback(std::move(callback_)) {}
        ~Request() {
            if (stream) {
                g_input_stream_close(stream, nullptr, nullptr);
                g_object_unref(stream);
            }
            if (message) g_object_unref(message);
        }
        bool current() const { return state->generation == generation; }
        static void read(Request* r) {
            g_input_stream_read_bytes_async(r->stream, 4096, G_PRIORITY_DEFAULT,
                r->state->cancellable, [](GObject* object, GAsyncResult* res, gpointer data) {
                    std::unique_ptr<Request> r(static_cast<Request*>(data));
                    GError* error = nullptr;
                    GBytes* bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(object), res, &error);
                    if (error) g_error_free(error);
                    if (!bytes) return;
                    gsize length = 0;
                    const char* chunk = static_cast<const char*>(g_bytes_get_data(bytes, &length));
                    if (length) r->body.append(chunk, length);
                    g_bytes_unref(bytes);
                    if (!r->current() || r->body.size() > 65536) return;
                    if (length) { read(r.release()); return; }
                    Results result = parse_response(r->body.data(), r->body.size(), r->pinyin);
                    r->callback(result);
                }, r);
        }
    };
    auto* request = new Request{m_state, m_state->generation, pinyin, source, count, std::move(callback)};
    m_state->timer = g_timeout_add_full(G_PRIORITY_DEFAULT, delay_ms, [](gpointer data) -> gboolean {
        auto* pending = static_cast<Request*>(data);
        // The timeout owns pending; the async operation owns a separate request.
        auto* r = new Request{pending->state, pending->generation, pending->pinyin,
                              pending->source, pending->count, std::move(pending->callback)};
        r->state->timer = 0;
        r->state->cancellable = g_cancellable_new();
        gchar* escaped = g_uri_escape_string(r->pinyin.c_str(), nullptr, FALSE);
        std::string uri = std::string(r->source == 1 ? "https://www.google.cn" : "https://www.google.com") +
            "/inputtools/request?ime=pinyin&text=" + escaped + "&num=" + std::to_string(r->count);
        g_free(escaped);
        r->message = soup_message_new("GET", uri.c_str());
        if (!r->message) { delete r; return G_SOURCE_REMOVE; }
        soup_session_send_async(r->state->session, r->message, G_PRIORITY_DEFAULT,
            r->state->cancellable, [](GObject* object, GAsyncResult* res, gpointer data) {
                std::unique_ptr<Request> r(static_cast<Request*>(data));
                GError* error = nullptr;
                r->stream = soup_session_send_finish(SOUP_SESSION(object), res, &error);
                if (error) g_error_free(error);
                if (!r->current() || !r->stream ||
                    soup_message_get_status(r->message) != SOUP_STATUS_OK) return;
                Request::read(r.release());
            }, r);
        return G_SOURCE_REMOVE;
    }, request, [](gpointer data) { delete static_cast<Request*>(data); });
}
