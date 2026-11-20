/**
 * @file test_e32p_ota.c
 * @brief Unity tests for e32p_ota. The offline query layer (running/next slot,
 *        app info, image size) is exercised against the runner's own OTA image;
 *        the streaming write and URL pull are destructive / need a server, so
 *        only their argument validation is checked (see the 25_ota demo).
 *        One case does write flash: it stages a copy of the running image and
 *        checks that a new begin() cancels it (see the boot-slot case below).
 */
#include "unity.h"
#include <string.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "e32p_ota.h"

#define OTA_SLOT_SIZE  0x180000   /* app partition size in the platform table */

TEST_CASE("e32p_ota: running and next slots are distinct OTA partitions", "[e32p_ota]")
{
    const char *running = e32p_ota_running_label();
    const char *next = e32p_ota_next_label();
    TEST_ASSERT_NOT_NULL(running);
    TEST_ASSERT_NOT_NULL(next);
    TEST_ASSERT_TRUE(strlen(running) > 0);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(running, next));   /* update targets the other slot */
}

TEST_CASE("e32p_ota: running app description is populated", "[e32p_ota]")
{
    e32p_ota_app_info_t info;
    TEST_ESP_OK(e32p_ota_get_app_info(&info));
    TEST_ASSERT_TRUE(strlen(info.project) > 0);
    TEST_ASSERT_TRUE(strlen(info.version) > 0);
    TEST_ASSERT_TRUE(strlen(info.idf_version) > 0);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ota_get_app_info(NULL));
}

TEST_CASE("e32p_ota: running image size is plausible", "[e32p_ota]")
{
    size_t size = 0;
    TEST_ESP_OK(e32p_ota_running_image_size(&size));
    TEST_ASSERT_GREATER_THAN(0, size);
    TEST_ASSERT_LESS_THAN(OTA_SLOT_SIZE, size);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ota_running_image_size(NULL));
}

/* Copy the running image into the next slot with begin / write / end. After a
 * successful end() the boot partition is the next slot (staged, not booted yet). */
static esp_err_t stage_running_image(void)
{
    static uint8_t buf[1024];
    size_t size = 0;
    esp_err_t err = e32p_ota_running_image_size(&size);
    if (err != ESP_OK) {
        return err;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    e32p_ota_handle_t h = NULL;
    err = e32p_ota_begin(&h);
    if (err != ESP_OK) {
        return err;
    }
    for (size_t off = 0; off < size && err == ESP_OK; off += sizeof(buf)) {
        size_t chunk = (size - off > sizeof(buf)) ? sizeof(buf) : (size - off);
        err = esp_partition_read(running, off, buf, chunk);
        if (err == ESP_OK) {
            err = e32p_ota_write(h, buf, chunk);
        }
    }
    if (err != ESP_OK) {
        e32p_ota_abort(h);
        return err;
    }
    return e32p_ota_end(h);
}

/* Starting a new update must cancel an image that is staged but has not booted yet.
 * The case moves the boot partition, so it puts it back to the running partition
 * BEFORE any assert can return; the runner keeps booting from its own slot even when
 * this case fails. Needs the dual-OTA table and an unsigned runner (the copy is of
 * the running image, so it verifies). */
TEST_CASE("e32p_ota: begin cancels a staged image that has not booted yet", "[e32p_ota]")
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);

    esp_err_t stage_err = stage_running_image();
    const esp_partition_t *boot_staged = esp_ota_get_boot_partition();

    e32p_ota_handle_t h = NULL;
    esp_err_t begin_err = ESP_FAIL;
    if (stage_err == ESP_OK && boot_staged == next) {
        begin_err = e32p_ota_begin(&h);
    }
    const esp_partition_t *boot_after = esp_ota_get_boot_partition();
    e32p_ota_abort(h);

    /* Teardown: always leave the boot partition on the running partition. */
    esp_err_t restore_err = ESP_OK;
    if (esp_ota_get_boot_partition() != running) {
        restore_err = esp_ota_set_boot_partition(running);
    }
    const esp_partition_t *boot_final = esp_ota_get_boot_partition();

    TEST_ESP_OK(restore_err);
    TEST_ASSERT_TRUE(boot_final == running);
    TEST_ESP_OK(stage_err);
    TEST_ASSERT_NOT_NULL(next);
    TEST_ASSERT_TRUE(boot_staged == next);            /* the copy is staged */
    TEST_ESP_OK(begin_err);
    TEST_ASSERT_TRUE(boot_after == running);          /* begin() cancelled the staged image */
}

TEST_CASE("e32p_ota: rollback queries are callable", "[e32p_ota]")
{
    /* Values depend on how the runner was flashed; just confirm they don't crash
       and return well-formed booleans. */
    volatile bool pending = e32p_ota_is_pending_verify();
    volatile bool possible = e32p_ota_rollback_possible();
    TEST_ASSERT_TRUE(pending == true || pending == false);
    TEST_ASSERT_TRUE(possible == true || possible == false);
}

TEST_CASE("e32p_ota: streaming and URL entry points validate args", "[e32p_ota]")
{
    uint8_t byte = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ota_begin(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ota_write(NULL, &byte, 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ota_end(NULL));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_ota_abort(NULL));                 /* NULL is a no-op */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ota_update_from_url(NULL, 0));
}

#if !CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP
/* These return before any network call, so no netif is needed. */
TEST_CASE("e32p_ota: plain http URL is refused", "[e32p_ota]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ota_update_from_url("http://192.0.2.1/x.bin", 0));
}

TEST_CASE("e32p_ota: upper-case HTTP scheme is refused", "[e32p_ota]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ota_update_from_url("HTTP://192.0.2.1/x.bin", 0));
}

TEST_CASE("e32p_ota: URL without a scheme is refused", "[e32p_ota]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ota_update_from_url("192.0.2.1/x.bin", 0));
}
#endif
