/* test_plain_numbers.cpp - numbers as text do not follow the system's locale
 * (issue 244).
 *
 * On Linux the frontend's runtime puts the process in the user's locale. With
 * a decimal comma, strtod stops at the dot of "0.5" and printf writes "0,5":
 * a core's declarations did not parse, and a project would have been written
 * with numbers nothing reads back.
 *
 * The first half runs anywhere. The second half needs a locale with a decimal
 * comma, which a test machine may not have: it takes the environment's
 * locale, and when that has no comma it SAYS it was skipped. Set
 * CHIMERA_TEST_NEEDS_COMMA_LOCALE=1 to make that a failure - the end-to-end
 * script does, after building such a locale itself.
 */

#include "../source/plain_numbers.hpp"
#include "../../../extern/cjson/cJSON.h"

#include <cassert>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
void plainNumbers()
{
	double v = 0;
	assert(chimera::parsePlainDouble("1.5", v) && v == 1.5);
	assert(chimera::parsePlainDouble("+2", v) && v == 2);
	assert(chimera::parsePlainDouble("-0.25", v) && v == -0.25);
	assert(chimera::parsePlainDouble("1e3", v) && v == 1000);
	assert(chimera::parsePlainDouble("0x10", v) && v == 16);
	assert(chimera::parsePlainDouble("inf", v) && std::isinf(v) && v > 0);
	assert(chimera::parsePlainDouble("-inf", v) && std::isinf(v) && v < 0);
	assert(chimera::parsePlainDouble("nan", v) && std::isnan(v));
	// one number, the whole text, and a dot for the fraction
	assert(!chimera::parsePlainDouble("", v));
	assert(!chimera::parsePlainDouble("1,5", v));
	assert(!chimera::parsePlainDouble("1.5 ", v));
	assert(!chimera::parsePlainDouble("1.5x", v));
	assert(!chimera::parsePlainDouble("+-1", v));
	assert(!chimera::parsePlainDouble("abc", v));

	assert(chimera::formatPlainDouble(1.5, 6) == "1.5");
	assert(chimera::formatPlainDouble(0.1, 1) == "0.1");
	assert(chimera::formatPlainDouble(-2.25, 17) == "-2.25");
	assert(chimera::formatPlainDouble(1e21, 6) == "1e+21");
	assert(chimera::formatPlainDouble(100000, 6) == "100000");
}

void json()
{
	// the shape of a core's declarations: one fraction is enough to matter
	const char *text = "{\"whole\":3,\"half\":0.5,\"list\":[1.25e2,-0.75],\"zero\":0.0}";
	cJSON *root = cJSON_Parse(text);
	assert(root != nullptr && cJSON_IsObject(root));
	assert(cJSON_GetObjectItemCaseSensitive(root, "whole")->valuedouble == 3);
	assert(cJSON_GetObjectItemCaseSensitive(root, "half")->valuedouble == 0.5);
	const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "list");
	assert(cJSON_GetArrayItem(list, 0)->valuedouble == 125);
	assert(cJSON_GetArrayItem(list, 1)->valuedouble == -0.75);
	// and it is written back with dots
	char *printed = cJSON_PrintUnformatted(root);
	assert(printed != nullptr);
	assert(std::strstr(printed, "\"half\":0.5") != nullptr);
	assert(std::strstr(printed, "-0.75") != nullptr);
	assert(std::strchr(printed, ',') != nullptr);                 // separators, yes
	assert(std::strstr(printed, "0,5") == nullptr && std::strstr(printed, "0,75") == nullptr);
	cJSON *again = cJSON_Parse(printed);
	assert(again != nullptr && cJSON_GetObjectItemCaseSensitive(again, "half")->valuedouble == 0.5);
	cJSON_Delete(again);
	cJSON_free(printed);
	cJSON_Delete(root);
}
} // namespace

int main()
{
	// in the C locale, where every program starts
	plainNumbers();
	json();

	// and in the user's, as under a runtime that adopts it
	std::setlocale(LC_ALL, "");
	const std::lconv *lc = std::localeconv();
	const bool comma = lc != nullptr && lc->decimal_point != nullptr && std::strcmp(lc->decimal_point, ",") == 0;
	const char *needs = std::getenv("CHIMERA_TEST_NEEDS_COMMA_LOCALE");
	if (!comma)
	{
		if (needs != nullptr && needs[0] == '1')
		{
			std::fprintf(stderr, "test_plain_numbers: a locale with a decimal comma was asked for and the environment's has '%s'\n",
				lc != nullptr && lc->decimal_point != nullptr ? lc->decimal_point : "?");
			return 1;
		}
		std::printf("test_plain_numbers: C locale ok; the decimal-comma half was SKIPPED (the environment's locale has none)\n");
		return 0;
	}
	// the instrument first: the C library really does use a comma here
	char probe[32];
	std::snprintf(probe, sizeof probe, "%.1f", 0.5);
	assert(std::strcmp(probe, "0,5") == 0);
	plainNumbers();
	json();
	std::printf("test_plain_numbers: ok, in the C locale and with a decimal comma\n");
	return 0;
}
