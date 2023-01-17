#ifndef __EUC_JP_H__
#define __EUC_JP_H__

#include <string>
#include <string_view>

namespace Encoding {
    std::string decode_eucjp(std::string_view src);
}

#endif
