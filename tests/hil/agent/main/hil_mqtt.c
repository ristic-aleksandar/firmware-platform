/**
 * @file hil_mqtt.c
 * @brief MQTT HIL self-test. No network: confirms the topic-filter matching runs
 *        correctly on-target and that the esp-mqtt client links and allocates
 *        (init without start does not connect). A full broker round-trip is left
 *        to the 23_mqtt demo, which needs Wi-Fi credentials and a broker.
 */
#include <string.h>
#include <stdio.h>
#include "e32p_mqtt.h"
#include "hil_agent.h"
#include "hil_mqtt.h"

hil_verdict_t hil_mqtt_selftest(char *d, size_t n)
{
    static const struct { const char *filter; const char *topic; bool expect; } cases[] = {
        { "sport/#",  "sport/tennis/player1", true  },
        { "sport/#",  "sport",                true  },   /* # matches parent */
        { "sport/+",  "sport/tennis",         true  },
        { "sport/+",  "sport/tennis/player1", false },   /* + is one level */
        { "+/+",      "/finance",             true  },
        { "a/b/c",    "a/b/d",                false },
        { "#",        "$SYS/broker/uptime",   false },   /* wildcard vs $ topic */
        { "$SYS/#",   "$SYS/broker/uptime",   true  },
    };
    const int ncases = (int)(sizeof(cases) / sizeof(cases[0]));
    for (int i = 0; i < ncases; i++) {
        if (e32p_mqtt_topic_matches(cases[i].filter, cases[i].topic) != cases[i].expect) {
            snprintf(d, n, "match '%s' vs '%s' != %d", cases[i].filter, cases[i].topic, cases[i].expect);
            return HIL_FAIL;
        }
    }

    /* Client allocates on-target without connecting. */
    e32p_mqtt_config_t cfg = { .broker_uri = "mqtt://localhost:1883" };
    e32p_mqtt_handle_t h = NULL;
    if (e32p_mqtt_init(&cfg, &h) != ESP_OK) {
        snprintf(d, n, "client init failed");
        return HIL_FAIL;
    }
    bool connected = e32p_mqtt_is_connected(h);
    e32p_mqtt_deinit(h);
    if (connected) {
        snprintf(d, n, "client reported connected before start");
        return HIL_FAIL;
    }

    snprintf(d, n, "%d topic cases + client init/deinit ok", ncases);
    return HIL_PASS;
}
