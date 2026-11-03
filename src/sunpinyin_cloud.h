// SPDX-License-Identifier: LGPL-2.1-only OR CDDL-1.0
// Copyright (c) 2026 Hillwood Yang
#ifndef SUNPINYIN_CLOUD_H
#define SUNPINYIN_CLOUD_H
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// All callbacks run on the GLib main context; cancelling invalidates old replies.
class SunPinyinCloud {
public:
    using Results = std::vector<std::string>;
    using Callback = std::function<void(const Results&)>;
    SunPinyinCloud();
    ~SunPinyinCloud();
    SunPinyinCloud(const SunPinyinCloud&) = delete;
    SunPinyinCloud& operator=(const SunPinyinCloud&) = delete;
    void query(const std::string& pinyin, int source, int count, int delay_ms, Callback callback);
    void cancel();
    static Results parse_response(const char* data, size_t length,
                                  const std::string& pinyin);
private:
    struct State;
    std::shared_ptr<State> m_state;
};
#endif
