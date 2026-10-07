#ifndef CHIMERA_CORE_LOG_HPP
#define CHIMERA_CORE_LOG_HPP

#include <string>

namespace chimera {

/* Whether the core log is on (ce_core_log): a session opened now mounts the
 * "corelog" request and notes itself in the log. */
bool coreLogOn();

/* One line into the core log, prefixed with the UTC time; nothing while it
 * is off. */
void coreLogNote(const std::string &line);

} // namespace chimera

#endif
