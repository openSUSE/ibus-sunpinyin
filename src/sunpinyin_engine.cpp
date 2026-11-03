/*
 * Copyright (c) 2009 Kov Chai <tchaikov@gmail.com>
 * Copyright (c) 2026 Hillwood Yang <hillwood@opensuse.org>
 *
 * The contents of this file are subject to the terms of either the GNU Lesser
 * General Public License Version 2.1 only ("LGPL") or the Common Development and
 * Distribution License ("CDDL")(collectively, the "License"). You may not use this
 * file except in compliance with the License. You can obtain a copy of the CDDL at
 * http://www.opensource.org/licenses/cddl1.php and a copy of the LGPLv2.1 at
 * http://www.opensource.org/licenses/lgpl-license.php. See the License for the
 * specific language governing permissions and limitations under the License. When
 * distributing the software, include this License Header Notice in each file and
 * include the full text of the License in the License file as well as the
 * following notice:
 *
 * NOTICE PURSUANT TO SECTION 9 OF THE COMMON DEVELOPMENT AND DISTRIBUTION LICENSE
 * (CDDL)
 * For Covered Software in this distribution, this License shall be governed by the
 * laws of the State of California (excluding conflict-of-law provisions).
 * Any litigation relating to this License shall be subject to the jurisdiction of
 * the Federal Courts of the Northern District of California and the state courts
 * of the State of California, with venue lying in Santa Clara County, California.
 *
 * Contributor(s):
 *
 * If you wish your version of this file to be governed by only the CDDL or only
 * the LGPL Version 2.1, indicate your decision by adding "[Contributor]" elects to
 * include this software in this distribution under the [CDDL or LGPL Version 2.1]
 * license." If you don't indicate a single choice of license, a recipient has the
 * option to distribute your version of this file under either the CDDL or the LGPL
 * Version 2.1, or to extend the choice of license to its licensees as provided
 * above. However, if you add LGPL Version 2.1 code and therefore, elected the LGPL
 * Version 2 license, then the option applies only if the new code is made subject
 * to such option by the copyright holder.
 */

#include <cassert>
#include <algorithm>
#include <sstream>

#include <sunpinyin.h>
#include <pinyin/pinyin_data.h>

#include "sunpinyin_property.h"
#include "sunpinyin_lookup_table.h"
#include "sunpinyin_config.h"
#include "sunpinyin_config_keys.h"
#include "imi_ibus_win.h"
#include "ibus_portable.h"
#include "sunpinyin_engine.h"

extern ibus::Config config;

SunPinyinEngine::SunPinyinEngine(IBusEngine *engine)
    : m_engine(engine),
      m_status_prop(SunPinyinProperty::create_status_prop(engine)),
      m_letter_prop(SunPinyinProperty::create_letter_prop(engine)),
      m_punct_prop(SunPinyinProperty::create_punct_prop(engine)),
      m_script_prop(SunPinyinProperty::create_script_prop(engine)),
      m_wh(NULL),
      m_pv(NULL),
      m_hotkey_profile(NULL),
      m_hard_forward(false)
{
    CSunpinyinSessionFactory& factory = CSunpinyinSessionFactory::getFactory();

    CSunpinyinSessionFactory::EPyScheme pinyin_scheme =
        m_config.get_py_scheme(CSunpinyinSessionFactory::QUANPIN);
    m_full_pinyin = pinyin_scheme == CSunpinyinSessionFactory::QUANPIN;
    factory.setPinyinScheme(pinyin_scheme);
    if (pinyin_scheme == CSunpinyinSessionFactory::QUANPIN) {
        update_fuzzy_pinyins();
        update_correction_pinyins();
        update_fuzzy_segs();
    } else {
        update_shuangpin_type();
    }
    update_user_data_dir();
    update_punct_mappings();

    factory.setCandiWindowSize(m_config.get(CONFIG_GENERAL_PAGE_SIZE, 10));

    m_pv = factory.createSession();
    if (!m_pv)
        return;

    m_hotkey_profile = new CHotkeyProfile();
    m_pv->setHotkeyProfile(m_hotkey_profile);

    m_wh = new CIBusWinHandler(this);
    m_pv->attachWinHandler(m_wh);

    m_prop_list = ibus_prop_list_new();

    ibus_prop_list_append(m_prop_list, m_status_prop);
    ibus_prop_list_append(m_prop_list, m_letter_prop);
    ibus_prop_list_append(m_prop_list, m_punct_prop);
    ibus_prop_list_append(m_prop_list, m_script_prop);
    ibus_prop_list_append(m_prop_list, m_setup_prop);

    g_signal_connect(m_engine, "set-content-type", G_CALLBACK(+[](
        IBusEngine*, guint, guint, gpointer data) {
            auto* self = static_cast<SunPinyinEngine*>(data);
            if (self->m_cloud_enabled) self->reset();
        }), this);
    update_config();
}

SunPinyinEngine::~SunPinyinEngine()
{
    m_cloud.cancel();
    g_signal_handlers_disconnect_by_data(m_engine, this);
    if (m_pv) {
        CSunpinyinSessionFactory& factory =
            CSunpinyinSessionFactory::getFactory();
        factory.destroySession(m_pv);
    }

    delete m_wh;
    delete m_hotkey_profile;
}

static CKeyEvent
translate_key(guint key_val, guint /*key_code*/, guint modifiers)
{
    // XXX: may need to move this logic into CKeyEvent
    if (key_val > 0x20 && key_val < 0x7f // isprint(key_val) && !isspace(key_val)
    && !(modifiers & IM_CTRL_MASK)) {
        // we only care about key_val here
        return CKeyEvent(key_val, key_val, modifiers);
    } else {
        // what matters is key_code, but ibus sents me key_code as key_val
        return CKeyEvent(key_val, 0, modifiers);
    }
}

gboolean
SunPinyinEngine::process_key_event (guint key_val,
                                    guint key_code,
                                    guint modifiers)
{
    CKeyEvent key = translate_key(key_val, key_code, modifiers);

    if (getenv("DISABLE_HARD_FORWARD") == NULL) {
        // Ctrl+<space> is pressed. let's just hard code these.
        // it looks ridiculous, but on what else do you need to do this hack?
        if (key.code == 0x20 && key.modifiers == IM_CTRL_MASK) {
            if (m_cloud_enabled) reset();
            m_hard_forward = !m_hard_forward;
            return TRUE;
        } else if (m_hard_forward) {
            return FALSE;
        }
    }

    if (!m_pv->getStatusAttrValue(CIBusWinHandler::STATUS_ID_CN)) {
        // we are in English input mode
        if (!m_hotkey_profile->isModeSwitchKey(key)) {
            m_hotkey_profile->rememberLastKey(key);
            return FALSE;
        }
    } else if (m_hotkey_profile->isModeSwitchKey(key)) {
        m_pv->onKeyEvent(CKeyEvent(IM_VK_ENTER, 0, 0));
        m_pv->setStatusAttrValue(CIMIWinHandler::STATUS_ID_CN, false);
        return TRUE;
    }

    if (m_cloud_enabled && !(modifiers & (IBUS_RELEASE_MASK | IBUS_CONTROL_MASK |
                                         IBUS_MOD1_MASK | IBUS_SUPER_MASK)) &&
        !m_local_candidates.empty()) {
        if (key_val == IBUS_KEY_Down) { cursor_down(); return TRUE; }
        if (key_val == IBUS_KEY_Up) { cursor_up(); return TRUE; }
    }
    if (m_cloud_enabled && (!m_local_candidates.empty() || m_cloud_visible) &&
        !(modifiers & IBUS_RELEASE_MASK) &&
        !(modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SUPER_MASK))) {
        if (key_val >= IBUS_KEY_1 && key_val <= IBUS_KEY_9) {
            guint index = key_val - IBUS_KEY_1;
            if (index < m_local_candidates.size() + m_visible_cloud.size())
                candidate_clicked(index);
            return TRUE;
        }
        if (key_val == IBUS_KEY_0) {
            if (m_local_candidates.size() + m_visible_cloud.size() >= 10)
                candidate_clicked(9);
            return TRUE;
        }
        if (key_val == IBUS_KEY_space) {
            candidate_clicked(ibus_lookup_table_get_cursor_in_page(m_lookup_table));
            return TRUE;
        }
    }
    // Map Alt+number deletion back to the underlying local candidate indices.
    if (m_cloud_visible && (modifiers & IBUS_MOD1_MASK) &&
        !(modifiers & IBUS_RELEASE_MASK) && key_val >= IBUS_KEY_0 && key_val <= IBUS_KEY_9) {
        guint index = key_val == IBUS_KEY_0 ? 9 : key_val - IBUS_KEY_1;
        if (index >= 1 && index <= m_visible_cloud.size()) return TRUE;
        if (index > m_visible_cloud.size()) index -= m_visible_cloud.size();
        key = translate_key(IBUS_KEY_1 + index, key_code, modifiers);
    }
    return m_pv->onKeyEvent(key);
}

void
SunPinyinEngine::focus_in ()
{
    update_candidate_orientation();
    update_output_script();
    ibus_engine_register_properties(m_engine, m_prop_list);
    m_pv->updateWindows(CIMIView::PREEDIT_MASK | CIMIView::CANDIDATE_MASK);
}

void
SunPinyinEngine::focus_out ()
{
    reset();
}

void
SunPinyinEngine::reset ()
{
    clear_cloud();
    m_pv->updateWindows(m_pv->clearIC());
}

void
SunPinyinEngine::enable ()
{
    bool is_cn = m_config.is_initial_mode_cn();
    m_status_prop.update(is_cn);
    m_pv->setStatusAttrValue(CIMIWinHandler::STATUS_ID_CN, is_cn);

    bool is_letter_full = m_config.is_initial_letter_full();
    m_letter_prop.update(is_letter_full);
    m_pv->setStatusAttrValue(CIMIWinHandler::STATUS_ID_FULLSYMBOL, is_letter_full);

    bool is_punct_full = m_config.is_initial_punct_full();
    m_punct_prop.update(is_punct_full);
    m_pv->setStatusAttrValue(CIMIWinHandler::STATUS_ID_FULLPUNC, is_punct_full);
}

void
SunPinyinEngine::disable ()
{
    if (m_cloud_enabled) reset();
}

void
SunPinyinEngine::page_up ()
{
    m_pv->onCandidatePageRequest(-1, true /* relative */);
}

void
SunPinyinEngine::page_down ()
{
    m_pv->onCandidatePageRequest(1, true /* relative */);
}

void
SunPinyinEngine::property_activate (const std::string& property, unsigned /*state*/)
{
    if (m_status_prop.toggle(property)) {
        m_pv->setStatusAttrValue(CIMIWinHandler::STATUS_ID_CN,
                                 m_status_prop.state());
    } else if (m_letter_prop.toggle(property)) {
        m_pv->setStatusAttrValue(CIMIWinHandler::STATUS_ID_FULLSYMBOL,
                                 m_letter_prop.state());
    } else if (m_punct_prop.toggle(property)) {
        m_pv->setStatusAttrValue(CIMIWinHandler::STATUS_ID_FULLPUNC,
                                 m_punct_prop.state());
    } else if (m_script_prop.toggle(property)) {
        const bool traditional = m_script_prop.state();
        m_config.set(CONFIG_GENERAL_TRADITIONAL, traditional);
        // Apply locally as well: config notifications may arrive later.
        m_conversion.set_enabled(traditional);
        m_pv->updateWindows(CIMIView::PREEDIT_MASK | CIMIView::CANDIDATE_MASK);
    } else {
        // try to launch the setup UI
        m_setup_prop.launch(property);
    }
}

void
SunPinyinEngine::candidate_clicked (guint index)
{
    if (m_cloud_enabled) {
        if (select_cloud_candidate(index)) return;
        if (m_cloud_visible && index > m_visible_cloud.size()) index -= m_visible_cloud.size();
        if (index >= m_local_candidates.size()) return;
    }
    m_pv->onCandidateSelectRequest(index);
}

void
SunPinyinEngine::cursor_up ()
{
    if (m_cloud_enabled && ibus_lookup_table_get_cursor_in_page(m_lookup_table) == 0) {
        page_up();
        return;
    }
    if (m_lookup_table.cursor_up()) {
        update_lookup_table();
    }
}

void
SunPinyinEngine::cursor_down ()
{
    if (m_cloud_enabled && ibus_lookup_table_get_cursor_in_page(m_lookup_table) + 1 >=
            m_local_candidates.size() + m_visible_cloud.size()) {
        page_down();
        return;
    }
    if (m_lookup_table.cursor_down()) {
        update_lookup_table();
    }
}

bool
SunPinyinEngine::onConfigChanged(const COptionEvent& event)
{
    if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_CLOUD_ENABLED) == 0 ||
        g_ascii_strcasecmp(event.name.c_str(), CONFIG_CLOUD_SOURCE) == 0 ||
        g_ascii_strcasecmp(event.name.c_str(), CONFIG_CLOUD_CANDIDATES) == 0 ||
        g_ascii_strcasecmp(event.name.c_str(), CONFIG_CLOUD_DELAY) == 0) {
        update_cloud_config();
        m_pv->updateWindows(CIMIView::PREEDIT_MASK | CIMIView::CANDIDATE_MASK);
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_GENERAL_MEMORY_POWER) == 0) {
        update_history_power();
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_GENERAL_PAGE_SIZE) == 0) {
        update_cand_window_size();
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_GENERAL_TRADITIONAL) == 0) {
        update_output_script();
        m_pv->updateWindows(CIMIView::PREEDIT_MASK | CIMIView::CANDIDATE_MASK);
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_GENERAL_ORIENTATION) == 0) {
        update_candidate_orientation();
        m_pv->updateWindows(CIMIView::CANDIDATE_MASK);
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_GENERAL_CHARSET_LEVEL) == 0) {
        update_charset_level();
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_GENERAL_MAX_BEST) == 0) {
        update_max_best();
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_GENERAL_MAX_TAIL_CANDIDATE) == 0) {
        update_max_tail_candidate();
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_KEYBOARD_MODE_SWITCH) == 0) {
        update_mode_key();
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_KEYBOARD_PUNCT_SWITCH) == 0) {
        update_punct_key();
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_KEYBOARD_PAGE_COMMA) == 0) {
        update_page_key_comma();
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_KEYBOARD_PAGE_MINUS) == 0) {
        update_page_key_minus();
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_KEYBOARD_PAGE_BRACKET) == 0) {
        update_page_key_bracket();
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_QUANPIN_FUZZYSEGS_ENABLED) == 0) {
        update_fuzzy_segs();
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_KEYBOARD_CANCEL_BACKSPACE) == 0) {
        update_cancel_with_backspace();
    } else if (g_ascii_strcasecmp(event.name.c_str(), CONFIG_KEYBOARD_SMARK_PUNCT) == 0) {
        update_smart_punc();
    }

    return false;
}

void
SunPinyinEngine::update_config()
{
    update_cloud_config();
    update_history_power();
    update_cand_window_size();
    update_candidate_orientation();
    update_output_script();
    update_max_best();
    update_max_tail_candidate();
    update_charset_level();
    update_page_key_minus();
    update_page_key_comma();
    update_page_key_bracket();
    update_mode_key();
    update_punct_key();
    update_cancel_with_backspace();
    update_smart_punc();
    update_punct_mappings();
    update_candi_delete_key();
    // update_quanpin_config();
    // update_shuangpin_config();
}

void
SunPinyinEngine::commit_string (const std::wstring& str)
{
    clear_cloud();
    const std::string converted = m_conversion.convert(
        reinterpret_cast<const gunichar*>(str.c_str()));
    IBusText* text = ibus_text_new_from_string(converted.c_str());
    ibus_engine_commit_text(m_engine, text);
}

void
SunPinyinEngine::update_candidates(const ICandidateList& cl)
{
    if (m_cloud_enabled) {
        m_local_candidates.clear();
        m_local_types.clear();
        m_local_first = cl.first();
        m_local_total = cl.total();
        for (int i = 0; i < cl.size(); ++i) {
            const TWCHAR* text = cl.candiString(i);
            if (!text || !cl.candiSize(i)) break;
            m_local_candidates.push_back(m_conversion.convert(
                reinterpret_cast<const gunichar*>(text)));
            m_local_types.push_back(cl.candiType(i));
        }
        render_cloud_candidates();
        return;
    }
    if (m_lookup_table.update_candidates(cl, m_conversion) > 0)
        update_lookup_table();
    else
        ibus_engine_hide_lookup_table (m_engine);
}

void
SunPinyinEngine::update_output_script()
{
    const bool traditional = m_config.get(CONFIG_GENERAL_TRADITIONAL, false);
    m_conversion.set_enabled(traditional);
    m_script_prop.update(traditional);
}

void
SunPinyinEngine::update_candidate_orientation()
{
    int orientation = m_config.get(CONFIG_GENERAL_ORIENTATION,
                                   static_cast<int>(IBUS_ORIENTATION_SYSTEM));
    if (orientation < IBUS_ORIENTATION_HORIZONTAL ||
        orientation > IBUS_ORIENTATION_SYSTEM)
        orientation = IBUS_ORIENTATION_SYSTEM;
    ibus_lookup_table_set_orientation(m_lookup_table, orientation);
}

void
SunPinyinEngine::update_lookup_table()
{
    ibus_engine_update_lookup_table(m_engine, m_lookup_table, TRUE);
}

bool
SunPinyinEngine::is_valid() const
{
    return m_pv != NULL;
}

static int
find_embed_preedit_pos(const IPreeditString& preedit)
{
    int mask = IPreeditString::USER_CHOICE & IPreeditString::HANZI_CHAR;
    for (size_t i = 0; i < preedit.charTypeSize(); i++) {
        if ((preedit.charTypeAt(i) & mask) == 0) {
            return i;
        }
    }
    return preedit.charTypeSize();
}

enum {ORANGE = 0xE76F00, GRAY_BLUE = 0x35556B, WHITE = 0xFFFFFF,
      BLACK = 0x000000};

void
SunPinyinEngine::update_preedit_string(const IPreeditString& preedit)
{
    request_cloud(preedit);
    if (preedit.size() > 0) {
        wstring wstr(preedit.string());
        int embed_pos = find_embed_preedit_pos(preedit);
        wstring embed_wstr = wstr.substr(0, embed_pos);
        wstring preedit_wstr = wstr.substr(embed_pos);
        const std::string embed_text = m_conversion.convert(
            reinterpret_cast<const gunichar*>(embed_wstr.c_str()));
        const gunichar* preedit_cstr = (const gunichar*) preedit_wstr.c_str();
        IBusText* preedit_text = ibus_text_new_from_ucs4(preedit_cstr);
        int caret = preedit.caret() - embed_pos;

        if (preedit.caret() < preedit.size()) {
            ibus_text_append_attribute(preedit_text, IBUS_ATTR_TYPE_FOREGROUND,
                                       WHITE, caret, preedit_wstr.size());
            ibus_text_append_attribute(preedit_text, IBUS_ATTR_TYPE_BACKGROUND,
                                       GRAY_BLUE, caret, preedit_wstr.size());
        }
        ibus_engine_update_auxiliary_text(m_engine, preedit_text, TRUE);

        ibus_engine_update_preedit_text(m_engine,
                                        ibus_text_new_from_string(embed_text.c_str()),
                                        g_utf8_strlen(embed_text.c_str(), -1), TRUE);

    } else {
        ibus_engine_hide_auxiliary_text(m_engine);
        ibus_engine_hide_preedit_text(m_engine);
    }
}

void
SunPinyinEngine::update_status_property(bool cn)
{
    m_status_prop.update(cn);
}

void
SunPinyinEngine::update_punct_property(bool full)
{
    m_punct_prop.update(full);
}

void
SunPinyinEngine::update_letter_property(bool full)
{
    m_letter_prop.update(full);
}

void
SunPinyinEngine::update_history_power()
{
    unsigned power = m_config.get(CONFIG_GENERAL_MEMORY_POWER, 3);
    CIMIContext* ic = m_pv->getIC();
    assert(ic);
    ic->setHistoryPower(power);
}

void
SunPinyinEngine::update_charset_level()
{
    std::string charset("GBK");
    charset = m_config.get(CONFIG_GENERAL_CHARSET_LEVEL, charset);
    //printf("charset is %s.\n", charset.c_str());
    CIMIContext* ic = m_pv->getIC();
    assert(ic);
    if (charset == "GB2312") {
        ic->setCharsetLevel(0);
    } else if (charset == "GBK") {
        ic->setCharsetLevel(1);
    }   
    else {
        ic->setCharsetLevel(2);
    }
}

void
SunPinyinEngine::update_cand_window_size()
{
    int size = m_config.get(CONFIG_GENERAL_PAGE_SIZE, 10);
    m_cloud_page_size = std::clamp(size, m_cloud_count + 1, 10);
    if (m_cloud_enabled) size = m_cloud_page_size - m_cloud_count;
    m_pv->setCandiWindowSize(size);
    // A new cloud count changes the local page capacity; restart at a page boundary.
    if (m_cloud_enabled) m_pv->onCandidatePageRequest(0, false);
}

void
SunPinyinEngine::update_mode_key()
{
    std::string mode_switch("Shift");
    mode_switch = m_config.get(CONFIG_KEYBOARD_MODE_SWITCH, mode_switch);

    CKeyEvent shift_l  (IM_VK_SHIFT_L, 0, IM_SHIFT_MASK|IM_RELEASE_MASK);
    CKeyEvent shift_r  (IM_VK_SHIFT_R, 0, IM_SHIFT_MASK|IM_RELEASE_MASK);
    CKeyEvent control_l(IM_VK_CONTROL_L, 0, IM_CTRL_MASK|IM_RELEASE_MASK);
    CKeyEvent control_r(IM_VK_CONTROL_R, 0, IM_CTRL_MASK|IM_RELEASE_MASK);

    if (mode_switch == "Shift") {
        m_hotkey_profile->removeModeSwitchKey(control_l);
        m_hotkey_profile->removeModeSwitchKey(control_r);
        m_hotkey_profile->addModeSwitchKey(shift_l);
        m_hotkey_profile->addModeSwitchKey(shift_r);
    } else if (mode_switch == "Control") {
        m_hotkey_profile->removeModeSwitchKey(shift_l);
        m_hotkey_profile->removeModeSwitchKey(shift_r);
        m_hotkey_profile->addModeSwitchKey(control_l);
        m_hotkey_profile->addModeSwitchKey(control_r);
    }
}

void
SunPinyinEngine::update_punct_key()
{
    std::string punct_switch("ControlComma");
    punct_switch = m_config.get(CONFIG_KEYBOARD_PUNCT_SWITCH, punct_switch);
    if (punct_switch == "ControlComma") {
        m_hotkey_profile->setPunctSwitchKey(CKeyEvent(IM_VK_COMMA, 0, IM_CTRL_MASK));
    } else if (punct_switch == "ControlPeriod") {
        m_hotkey_profile->setPunctSwitchKey(CKeyEvent(IM_VK_PERIOD, 0, IM_CTRL_MASK));
    }
}

void
SunPinyinEngine::update_page_key_minus()
{
    update_page_key(CONFIG_KEYBOARD_PAGE_MINUS, false,
                    IM_VK_MINUS, IM_VK_EQUALS);
}

void
SunPinyinEngine::update_page_key_comma()
{
    update_page_key(CONFIG_KEYBOARD_PAGE_COMMA, false,
                    IM_VK_COMMA, IM_VK_PERIOD);
}

void
SunPinyinEngine::update_page_key_bracket()
{
    update_page_key(CONFIG_KEYBOARD_PAGE_BRACKET, false,
                    IM_VK_OPEN_BRACKET, IM_VK_CLOSE_BRACKET);
}

void
SunPinyinEngine::update_page_key(const char* conf_key, bool default_val,
                            unsigned page_up, unsigned page_down)
{
    bool enabled = m_config.get(conf_key, default_val);

    if (enabled) {
        m_hotkey_profile->addPageUpKey(CKeyEvent(page_up, 0));
        m_hotkey_profile->addPageDownKey(CKeyEvent(page_down, 0));
    } else {
        m_hotkey_profile->removePageUpKey(CKeyEvent(page_up, 0));
        m_hotkey_profile->removePageDownKey(CKeyEvent(page_down, 0));
    }
}

void
SunPinyinEngine::update_cancel_with_backspace()
{
    bool enabled = m_config.get(CONFIG_KEYBOARD_CANCEL_BACKSPACE, true);
    m_pv->setCancelOnBackspace(enabled);
}

void
SunPinyinEngine::update_smart_punc()
{
    m_pv->setSmartPunct(m_config.get(CONFIG_KEYBOARD_SMARK_PUNCT, true));
}

void
SunPinyinEngine::update_max_best()
{
    if (m_pv->getIC() == NULL) {
        return;
    }
    int oldval = (int) m_pv->getIC()->getMaxBest();
    m_pv->getIC()->setMaxBest(m_config.get(CONFIG_GENERAL_MAX_BEST, oldval));
}

void
SunPinyinEngine::update_max_tail_candidate()
{
    if (m_pv->getIC() == NULL) {
        return;
    }
    int oldval = (int) m_pv->getIC()->getMaxTailCandidateNum();
    m_pv->getIC()->setMaxTailCandidateNum(
        m_config.get(CONFIG_GENERAL_MAX_TAIL_CANDIDATE, oldval));
}

string_pairs parse_pairs(const std::vector<std::string>& strings)
{
    string_pairs pairs;
    for (std::vector<std::string>::const_iterator pair = strings.begin();
         pair != strings.end(); ++pair) {

        std::string::size_type found = pair->find(':');
        if (found == pair->npos || pair->length() < 3)
            continue;
        if (found == 0 && (*pair)[0] == ':')
            found = 1;

        pairs.push_back(make_pair(pair->substr(0, found),
                                  pair->substr(found+1)));
    }
    return pairs;
}

// the mappings in default_pairs will override the ones in user_pairs
string_pairs merge_pairs(const string_pairs& default_pairs,
                         const string_pairs& user_pairs)
{
    typedef std::map<std::string, int> Indexes;
    Indexes indexes;
    int index = 0;
    for (string_pairs::const_iterator it = default_pairs.begin();
         it != default_pairs.end(); ++it, ++index) {
        Indexes::iterator found = indexes.find(it->first);
        if (found == indexes.end()) {
            indexes[it->first] = index;
        } else {
            // it is a paired punct.
            indexes[it->first] = -found->second;
        }
    }
    string_pairs result(default_pairs);
    for (string_pairs::const_iterator it = user_pairs.begin();
         it != user_pairs.end(); ++it) {
        Indexes::iterator found = indexes.find(it->first);
        if (found == indexes.end()) {
            result.push_back(*it);
        } else if (found->second >= 0) {
            result[found->second] = *it;
        } else {
            // it is a paired punct,
            // but we don't support this kind of mapping yet,
            // so quietly ignore it.
        }
    }
    return result;
}

void
SunPinyinEngine::update_punct_mappings()
{
    CSimplifiedChinesePolicy& policy = ASimplifiedChinesePolicy::instance();
    if (m_config.get(PINYIN_PUNCTMAPPING_ENABLED, false)) {
        std::vector<std::string> mappings;
        mappings = m_config.get(PINYIN_PUNCTMAPPING_MAPPINGS, mappings);
        string_pairs pairs(merge_pairs(policy.getDefaultPunctMapping(),
                                       parse_pairs(mappings)));
        policy.setPunctMapping(pairs);
    }
}

void
SunPinyinEngine::update_user_data_dir()
{
    std::stringstream user_data_dir;
    user_data_dir << g_get_home_dir()
                  << G_DIR_SEPARATOR_S << ".sunpinyin";
    ASimplifiedChinesePolicy::instance().setUserDataDir(user_data_dir.str());
}

void
SunPinyinEngine::update_fuzzy_pinyins()
{
    bool enabled = m_config.get(QUANPIN_FUZZY_ENABLED, false);
    AQuanpinSchemePolicy::instance().setFuzzyForwarding(enabled);
    AShuangpinSchemePolicy::instance().setFuzzyForwarding(enabled);
    if (!enabled)
        return;
    std::vector<std::string> fuzzy_pinyins;
    fuzzy_pinyins = m_config.get(QUANPIN_FUZZY_PINYINS, fuzzy_pinyins);
    AQuanpinSchemePolicy::instance().setFuzzyPinyinPairs(parse_pairs(fuzzy_pinyins));
    AShuangpinSchemePolicy::instance().setFuzzyPinyinPairs(parse_pairs(fuzzy_pinyins));
}

void
SunPinyinEngine::update_correction_pinyins()
{
    bool enabled = m_config.get(QUANPIN_AUTOCORRECTION_ENABLED, false);
    AQuanpinSchemePolicy::instance().setAutoCorrecting(enabled);
    if (!enabled)
        return;
    std::vector<std::string> correction_pinyins;
    correction_pinyins = m_config.get(QUANPIN_AUTOCORRECTION_PINYINS, correction_pinyins);
    AQuanpinSchemePolicy::instance().setAutoCorrectionPairs(parse_pairs(correction_pinyins));
}

void
SunPinyinEngine::update_fuzzy_segs()
{
    bool enable_fuzzy_segs = m_config.get(CONFIG_QUANPIN_FUZZYSEGS_ENABLED, false);
    AQuanpinSchemePolicy::instance().setFuzzySegmentation(enable_fuzzy_segs);
    bool enable_inner_fuzzy = m_config.get(CONFIG_QUANPIN_INNERFUZZY_ENABLED, false);
    AQuanpinSchemePolicy::instance().setInnerFuzzySegmentation(CONFIG_QUANPIN_INNERFUZZY_ENABLED);
}

void
SunPinyinEngine::update_shuangpin_type()
{
    EShuangpinType shuangpin_type = MS2003;
    shuangpin_type = (EShuangpinType) m_config.get(SHUANGPIN_TYPE, (int) shuangpin_type);
    AShuangpinSchemePolicy::instance().setShuangpinType(shuangpin_type);
}

void
SunPinyinEngine::update_candi_delete_key()
{
    /* FIXME: need to get candi_delete_key from user's configuration */
    m_hotkey_profile->setCandiDeleteKey(CKeyEvent(0, 0, IM_ALT_MASK));
}

bool
SunPinyinEngine::cloud_allowed()
{
    guint purpose = 0, hints = 0;
    ibus_engine_get_content_type(m_engine, &purpose, &hints);
    guint private_hints = IBUS_INPUT_HINT_PRIVATE;
#if IBUS_CHECK_VERSION(1, 5, 34)
    private_hints |= IBUS_INPUT_HINT_HIDDEN_TEXT;
#endif
    return m_cloud_enabled && !m_hard_forward &&
        m_pv->getStatusAttrValue(CIBusWinHandler::STATUS_ID_CN) &&
        purpose != IBUS_INPUT_PURPOSE_PASSWORD && purpose != IBUS_INPUT_PURPOSE_PIN &&
        !(hints & private_hints);
}

void
SunPinyinEngine::clear_cloud()
{
    m_cloud.cancel();
    m_cloud_query.clear();
    m_cloud_results.clear();
    m_visible_cloud.clear();
    m_cloud_visible = false;
}

void
SunPinyinEngine::update_cloud_config()
{
    clear_cloud();
    m_cloud_enabled = m_config.get(CONFIG_CLOUD_ENABLED, false);
    m_cloud_source = m_config.get(CONFIG_CLOUD_SOURCE, 0) == 1 ? 1 : 0;
    m_cloud_count = std::clamp(m_config.get(CONFIG_CLOUD_CANDIDATES, 1), 1, 9);
    m_cloud_delay = std::clamp(m_config.get(CONFIG_CLOUD_DELAY, 600), 0, 3000);
    m_local_candidates.clear();
    m_local_types.clear();
    update_cand_window_size();
}

std::string
SunPinyinEngine::cloud_pinyin(const IPreeditString& preedit) const
{
    if (preedit.size() <= 0) return {};
    std::string query;
    // A selected Chinese prefix must not be lost when committing a cloud result.
    for (int i = 0; i < preedit.size(); ++i) {
        const auto c = preedit.string()[i];
        if (c >= 'a' && c <= 'z') {
            if (m_full_pinyin) query += static_cast<char>(c);
        } else if (c == '\'' || c == ' ') {
            if (m_full_pinyin) query += '\'';
        } else if (c != ';' || m_full_pinyin) {
            return {};
        }
    }
    if (m_full_pinyin) return query;

    IPySegmentor* segmentor = m_pv->getPySegmentor();
    if (!segmentor) return {};
    const auto& input = segmentor->getInputBuffer();
    unsigned covered = 0;
    // Decode the active segmentor's syllables, so every supported double-pinyin
    // layout (including semicolon keys and zero initials) uses its own mapping.
    for (const auto& segment : segmentor->getSegments(false)) {
        if (segment.m_start != covered || segment.m_len == 0 ||
            covered + segment.m_len > input.size()) return {};
        covered += segment.m_len;
        if (segment.m_type == IPySegmentor::SYLLABLE_SEP) continue;
        // Wait for the second key; do not send raw or incomplete double pinyin.
        if (segment.m_type != IPySegmentor::SYLLABLE || segment.m_len != 2 ||
            segment.m_syllables.empty()) return {};
        const TSyllable syllable(segment.m_syllables.front());
        if (!syllable.isFullSyllable()) return {};
        const char* spelling = CPinyinData::decodeSyllable(syllable);
        if (!spelling || !*spelling) return {};
        if (!query.empty()) query += '\'';
        query += spelling;
    }
    if (covered != input.size()) return {};
    return query;
}

void
SunPinyinEngine::request_cloud(const IPreeditString& preedit)
{
    const std::string query = cloud_allowed() ? cloud_pinyin(preedit) : std::string();
    if (query == m_cloud_query) return;
    clear_cloud();
    // Remove the stale cloud entry even for empty or too-short input.
    render_cloud_candidates();
    if (query.size() < 2 || query.size() > 128) return;
    m_cloud_query = query;
    m_cloud.query(query, m_cloud_source, m_cloud_count, m_cloud_delay, [this, query](const SunPinyinCloud::Results& results) {
        if (!cloud_allowed() || query != m_cloud_query || results.empty()) return;
        const guint cursor = ibus_lookup_table_get_cursor_in_page(m_lookup_table);
        m_cloud_results = results;
        render_cloud_candidates();
        if (m_cloud_visible && cursor > 0) {
            const guint start = ibus_lookup_table_get_cursor_pos(m_lookup_table);
            ibus_lookup_table_set_cursor_pos(m_lookup_table, start + cursor + m_visible_cloud.size());
            update_lookup_table();
        }
    });
}

void
SunPinyinEngine::render_cloud_candidates()
{
    if (!m_cloud_enabled) return;
    ibus_lookup_table_clear(m_lookup_table);
    m_visible_cloud.clear();
    std::vector<std::string> converted;
    if (cloud_allowed()) {
        for (const auto& result : m_cloud_results) {
            if (converted.size() >= static_cast<size_t>(m_cloud_count)) break;
            gunichar* ucs4 = g_utf8_to_ucs4(result.c_str(), -1, nullptr, nullptr, nullptr);
            if (!ucs4) continue;
            std::string text = m_conversion.convert(ucs4);
            g_free(ucs4);
            if (text.empty() ||
                std::find(m_local_candidates.begin(), m_local_candidates.end(), text) != m_local_candidates.end() ||
                std::find(converted.begin(), converted.end(), text) != converted.end()) continue;
            converted.push_back(text);
            m_visible_cloud.push_back(result);
        }
    }
    m_cloud_visible = !converted.empty();
    const int capacity = m_cloud_page_size - m_cloud_count;
    const int page_size = capacity + converted.size();
    const int page_start = (m_local_first / capacity) * page_size;
    const int pages = (m_local_total + capacity - 1) / capacity;
    const int total = m_local_total + std::max(1, pages) * converted.size();
    ibus_lookup_table_set_page_size(m_lookup_table, page_size);
    for (int i = 0; i < page_start; ++i)
        ibus_lookup_table_append_candidate(m_lookup_table, ibus_text_new_from_static_string(""));
    for (size_t i = 0; i <= m_local_candidates.size(); ++i) {
        if (m_cloud_visible && (i == 1 || (i == 0 && m_local_candidates.empty()))) {
            for (const auto& result : converted) {
                IBusText* text = ibus_text_new_from_string((result + " ☁").c_str());
                ibus_lookup_table_append_candidate(m_lookup_table, text);
            }
        }
        if (i < m_local_candidates.size()) {
            IBusText* text = ibus_text_new_from_string(m_local_candidates[i].c_str());
            if (m_local_types[i] == ICandidateList::BEST_WORD)
                ibus_text_append_attribute(text, IBUS_ATTR_TYPE_UNDERLINE,
                                          IBUS_ATTR_UNDERLINE_SINGLE, 0, -1);
            ibus_lookup_table_append_candidate(m_lookup_table, text);
        }
    }
    for (int i = ibus_lookup_table_get_number_of_candidates(m_lookup_table); i < total; ++i)
        ibus_lookup_table_append_candidate(m_lookup_table, ibus_text_new_from_static_string(""));
    if (m_local_candidates.empty() && !m_cloud_visible)
        ibus_engine_hide_lookup_table(m_engine);
    else {
        ibus_lookup_table_set_cursor_pos(m_lookup_table, page_start);
        update_lookup_table();
    }
}

bool
SunPinyinEngine::select_cloud_candidate(guint index)
{
    const guint cloud_index = m_local_candidates.empty() ? 0 : 1;
    if (!m_cloud_visible || index < cloud_index ||
        index >= cloud_index + m_visible_cloud.size()) return false;
    if (!cloud_allowed()) { reset(); return true; }
    glong length = 0;
    gunichar* ucs4 = g_utf8_to_ucs4(m_visible_cloud[index - cloud_index].c_str(), -1, nullptr, &length, nullptr);
    if (!ucs4) return true;
    std::wstring text;
    for (glong i = 0; i < length; ++i) text += static_cast<wchar_t>(ucs4[i]);
    g_free(ucs4);
    commit_string(text);
    reset();
    return true;
}

// -*- indent-tabs-mode: nil -*- vim:et:ts=4
