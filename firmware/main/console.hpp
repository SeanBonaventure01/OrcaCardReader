#pragma once

#include <string>

#include "orca/api/api.hpp"

namespace orca::app {

void start_console(api::Api& api, const std::string& admin_pin);

}  // namespace orca::app
