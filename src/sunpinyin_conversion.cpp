/*
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

#include "sunpinyin_conversion.h"

SunPinyinConversion::SunPinyinConversion()
    : m_converter(reinterpret_cast<opencc_t>(-1)), m_enabled(false)
{}

SunPinyinConversion::~SunPinyinConversion()
{
    if (m_converter != reinterpret_cast<opencc_t>(-1))
        opencc_close(m_converter);
}

void
SunPinyinConversion::set_enabled(bool enabled)
{
    m_enabled = enabled;
    if (enabled && m_converter == reinterpret_cast<opencc_t>(-1)) {
        m_converter = opencc_open("s2t.json");
        if (m_converter == reinterpret_cast<opencc_t>(-1)) {
            g_warning("Cannot initialize OpenCC: %s", opencc_error());
            m_enabled = false;
        }
    }
}

std::string
SunPinyinConversion::convert(const gunichar* text) const
{
    gchar* utf8 = g_ucs4_to_utf8(text, -1, NULL, NULL, NULL);
    if (!utf8)
        return std::string();
    std::string result(utf8);
    g_free(utf8);
    if (!m_enabled || result.empty())
        return result;

    char* converted = opencc_convert_utf8(m_converter, result.c_str(), result.size());
    if (converted) {
        result = converted;
        opencc_convert_utf8_free(converted);
    } else {
        g_warning("Cannot convert text with OpenCC: %s", opencc_error());
    }
    return result;
}
