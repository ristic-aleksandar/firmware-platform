/**
 * @file test_e32p_mqtt.c
 * @brief Unity tests for e32p_mqtt. Topic-filter matching (the spec logic) is
 *        pure and fully covered; the client is only exercised for argument
 *        validation (a broker/network is needed for the transport paths, covered
 *        by the 23_mqtt demo). esp_mqtt_client_init() allocates without
 *        connecting, so a handle can be created headless for the arg checks.
 */
#include "unity.h"
#include "e32p_mqtt.h"

TEST_CASE("e32p_mqtt: topic matching — # multi-level wildcard", "[e32p_mqtt]")
{
    TEST_ASSERT_TRUE(e32p_mqtt_topic_matches("#", "a/b/c"));
    TEST_ASSERT_TRUE(e32p_mqtt_topic_matches("sport/#", "sport/tennis/player1"));
    TEST_ASSERT_TRUE(e32p_mqtt_topic_matches("sport/#", "sport"));            /* matches parent */
    TEST_ASSERT_TRUE(e32p_mqtt_topic_matches("sport/tennis/#", "sport/tennis"));
    TEST_ASSERT_FALSE(e32p_mqtt_topic_matches("sport/#", "other/tennis"));
}

TEST_CASE("e32p_mqtt: topic matching — + single-level wildcard", "[e32p_mqtt]")
{
    TEST_ASSERT_TRUE(e32p_mqtt_topic_matches("sport/+", "sport/tennis"));
    TEST_ASSERT_TRUE(e32p_mqtt_topic_matches("sport/+/player1", "sport/tennis/player1"));
    TEST_ASSERT_TRUE(e32p_mqtt_topic_matches("+/+", "/finance"));             /* empty levels count */
    TEST_ASSERT_TRUE(e32p_mqtt_topic_matches("sport/+", "sport/"));           /* empty trailing level */
    TEST_ASSERT_TRUE(e32p_mqtt_topic_matches("+", "finance"));
    TEST_ASSERT_FALSE(e32p_mqtt_topic_matches("sport/+", "sport/tennis/player1")); /* + is one level */
    TEST_ASSERT_FALSE(e32p_mqtt_topic_matches("sport/+", "sport"));           /* + needs a level */
    TEST_ASSERT_FALSE(e32p_mqtt_topic_matches("+", "finance/x"));
}

TEST_CASE("e32p_mqtt: topic matching — exact and negatives", "[e32p_mqtt]")
{
    TEST_ASSERT_TRUE(e32p_mqtt_topic_matches("a/b/c", "a/b/c"));
    TEST_ASSERT_FALSE(e32p_mqtt_topic_matches("a/b/c", "a/b/d"));
    TEST_ASSERT_FALSE(e32p_mqtt_topic_matches("a/b", "a/b/c"));   /* filter shorter, no wildcard */
    TEST_ASSERT_FALSE(e32p_mqtt_topic_matches("a/b/c", "a/b"));   /* topic shorter */
    TEST_ASSERT_FALSE(e32p_mqtt_topic_matches(NULL, "a"));
    TEST_ASSERT_FALSE(e32p_mqtt_topic_matches("a", NULL));
}

TEST_CASE("e32p_mqtt: topic matching — $ topics resist leading wildcards", "[e32p_mqtt]")
{
    TEST_ASSERT_FALSE(e32p_mqtt_topic_matches("#", "$SYS/broker/uptime"));
    TEST_ASSERT_FALSE(e32p_mqtt_topic_matches("+/monitor/Clients", "$SYS/monitor/Clients"));
    TEST_ASSERT_TRUE(e32p_mqtt_topic_matches("$SYS/#", "$SYS/broker/uptime"));  /* explicit $ ok */
}

TEST_CASE("e32p_mqtt: event-type strings are non-empty", "[e32p_mqtt]")
{
    TEST_ASSERT_EQUAL_STRING("connected",    e32p_mqtt_event_type_str(E32P_MQTT_EVENT_CONNECTED));
    TEST_ASSERT_EQUAL_STRING("data",         e32p_mqtt_event_type_str(E32P_MQTT_EVENT_DATA));
    TEST_ASSERT_EQUAL_STRING("disconnected", e32p_mqtt_event_type_str(E32P_MQTT_EVENT_DISCONNECTED));
    TEST_ASSERT_EQUAL_STRING("other",        e32p_mqtt_event_type_str((e32p_mqtt_event_type_t)999));
}

TEST_CASE("e32p_mqtt: init/deinit and argument validation", "[e32p_mqtt]")
{
    e32p_mqtt_handle_t h = NULL;
    /* Config validation (no client created, no network). */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_mqtt_init(NULL, &h));
    e32p_mqtt_config_t bad = { .broker_uri = NULL };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_mqtt_init(&bad, &h));

    /* A client can be created without connecting (esp_mqtt_client_init only
       allocates); use it to check publish/subscribe argument guards. */
    e32p_mqtt_config_t cfg = { .broker_uri = "mqtt://localhost:1883" };
    TEST_ESP_OK(e32p_mqtt_init(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_FALSE(e32p_mqtt_is_connected(h));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_mqtt_publish(h, NULL, "x", 1, 0, false, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_mqtt_publish(h, "t", "x", 1, 3, false, NULL)); /* qos>2 */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_mqtt_subscribe(h, NULL, 1, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_mqtt_subscribe(h, "t", -1, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_mqtt_publish(NULL, "t", "x", 1, 0, false, NULL));

    TEST_ESP_OK(e32p_mqtt_deinit(h));
}
