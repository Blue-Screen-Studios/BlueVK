#pragma once
#include <format>
#include <string>

#include "bluevk/context.hpp"

namespace bvk::detail {

void log(LogLevel, std::string_view);

template <class... A>
void logf(LogLevel level, std::format_string<A...> fmt, A&&... args) {
  log(level, std::format(fmt, std::forward<A>(args)...));
}

}  // namespace bvk::detail
