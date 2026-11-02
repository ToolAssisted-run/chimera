/* project_check.hpp - internal: ce_project_validate with the settings the
 * machine will run with. Not part of the ABI. */

#pragma once

#include "chimera/engine.h"
#include "../../extern/cjson/cJSON.h"

#include <string>
#include <vector>

/* ce_project_validate, with the slots' exposedWhen conditions read against
 * these settings (a JSON object) rather than the project's own; NULL reads the
 * project's own. ce_session_open_project passes the package's defaults with
 * the project's settings on top, so a project that leaves a setting out is
 * judged by the value the machine will boot with. */
extern "C" int32_t ceProjectValidate(const ce_project *p, const char *slots_json, uint64_t slots_len,
	const cJSON *settings, const char **error_out);

/* The firmware a machine is given, against the pins the project records
 * ([{"id", "sha1"}]): one plain sentence for each pin whose id is not given
 * or is given with other bytes. Empty when every pin is met. A warning only
 * (user-decided, 2026-10-18): another file is another machine, and a movie
 * may desync on it, but the window is where a person is asked. */
std::vector<std::string> ceFirmwarePinMismatches(const ce_project *p,
	const char *const *ids, const uint8_t *const *data, const uint64_t *lens, int32_t count);

/* A project made from its two parts, as a core's tool writes them for an
 * output declared "project" (docs/project.md): project.json's text and the
 * lines of input.input. It is read as an opened project is - a part that
 * would be refused there is refused here, with why - given an id of its own,
 * and saved to path. 0 when it was. */
int32_t ceProjectPack(const std::string &json, const std::string &input, const char *path, std::string &why);
