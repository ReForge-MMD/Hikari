#pragma once

#include <string>

namespace hikari::core {

void log_msg(const std::string& message);
std::string get_recent_logs();
void clear_logs();
void install_av_tracer();

} // namespace hikari::core
