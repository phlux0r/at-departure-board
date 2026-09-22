#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "config_schema.h"

void setUp() {}
void tearDown() {}

namespace {
// The shape an older build stored. cfg_parse still has to accept it, or a
// firmware update throws away the stops someone already configured.
const char* V1 =
    "{\"v\":1,\"location\":\"Kingsland\",\"theme\":1,\"watches\":["
    "{\"label\":\"to Wynyard Quarter\",\"stop_code\":\"8213\","
    "\"route_short_name\":\"20\",\"toward_stop_code\":\"1060\",\"enabled\":true},"
    "{\"label\":\"to Waitemata\",\"stop_code\":\"122\","
    "\"route_short_name\":\"\",\"toward_stop_code\":\"133\",\"enabled\":false}]}";

const char* GOOD =
    "{\"v\":2,\"location\":\"Kingsland\",\"theme\":1,\"active_group\":1,\"groups\":["
    "{\"name\":\"Weekday\",\"watches\":["
    "{\"label\":\"to Wynyard Quarter\",\"stop_code\":\"8213\","
    "\"route_short_name\":\"20\",\"toward_stop_code\":\"1060\",\"enabled\":true},"
    "{\"label\":\"to Waitemata\",\"stop_code\":\"122\","
    "\"route_short_name\":\"\",\"toward_stop_code\":\"133\",\"enabled\":false}]},"
    "{\"name\":\"Weekend\",\"watches\":["
    "{\"label\":\"to the beach\",\"stop_code\":\"7042\","
    "\"route_short_name\":\"\",\"toward_stop_code\":\"7100\",\"enabled\":true}]}]}";

// A one-group document, filled in by the caller.
int one_group(char* out, size_t cap, const char* watches) {
  return snprintf(out, cap,
                  "{\"v\":2,\"location\":\"X\",\"theme\":0,\"active_group\":0,"
                  "\"groups\":[{\"name\":\"g\",\"watches\":[%s]}]}",
                  watches);
}

const char* ONE_WATCH =
    "{\"label\":\"a\",\"stop_code\":\"1\",\"route_short_name\":\"\","
    "\"toward_stop_code\":\"\",\"enabled\":true}";
}  // namespace

void test_parses_a_good_document() {
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_STRING("Kingsland", c.location);
  TEST_ASSERT_EQUAL_UINT8(1, c.theme);
  TEST_ASSERT_EQUAL_UINT8(2, c.n_groups);
  TEST_ASSERT_EQUAL_UINT8(1, c.active_group);
  TEST_ASSERT_EQUAL_STRING("Weekday", c.groups[0].name);
  TEST_ASSERT_EQUAL_STRING("Weekend", c.groups[1].name);
  TEST_ASSERT_EQUAL_UINT8(2, c.groups[0].n_watches);
  TEST_ASSERT_EQUAL_UINT8(1, c.groups[1].n_watches);
  TEST_ASSERT_EQUAL_STRING("8213", c.groups[0].watches[0].stop_code);
  TEST_ASSERT_EQUAL_STRING("20", c.groups[0].watches[0].route_short_name);
  TEST_ASSERT_TRUE(c.groups[0].watches[0].enabled);
  TEST_ASSERT_FALSE(c.groups[0].watches[1].enabled);
}

void test_v1_is_lifted_into_a_single_group() {
  // The migration that keeps an existing board's stops across the update.
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(V1, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_STRING("Kingsland", c.location);
  TEST_ASSERT_EQUAL_UINT8(1, c.theme);
  TEST_ASSERT_EQUAL_UINT8(1, c.n_groups);
  TEST_ASSERT_EQUAL_UINT8(0, c.active_group);
  TEST_ASSERT_TRUE(strlen(c.groups[0].name) > 0);  // named, whatever we call it
  TEST_ASSERT_EQUAL_UINT8(2, c.groups[0].n_watches);
  TEST_ASSERT_EQUAL_STRING("8213", c.groups[0].watches[0].stop_code);
  TEST_ASSERT_EQUAL_STRING("122", c.groups[0].watches[1].stop_code);
  TEST_ASSERT_FALSE(c.groups[0].watches[1].enabled);
}

void test_v1_is_re_serialised_as_v2() {
  // What config_begin()'s migration relies on: read v1, write v2.
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(V1, &c, 2) == CfgError::Ok);
  char buf[CFG_JSON_CAP];
  TEST_ASSERT_TRUE(cfg_serialize(c, buf, sizeof buf) > 0);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"v\":2"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"groups\""));

  Config again{};
  TEST_ASSERT_TRUE(cfg_parse(buf, &again, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_STRING("8213", again.groups[0].watches[0].stop_code);
}

void test_empty_route_short_name_is_allowed() {
  // "" means any route, which is what carries a rail watch through the CRL
  // rename. Rejecting it would break the shipped default config.
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_STRING("", c.groups[0].watches[1].route_short_name);
}

void test_theme_is_clamped_not_rejected() {
  Config c{};
  char j[CFG_JSON_CAP];
  snprintf(j, sizeof j,
           "{\"v\":2,\"location\":\"X\",\"theme\":99,\"active_group\":0,"
           "\"groups\":[{\"name\":\"g\",\"watches\":[%s]}]}",
           ONE_WATCH);
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_UINT8(1, c.theme);  // clamped to theme_max - 1
}

void test_active_group_is_clamped_not_rejected() {
  // A group can be deleted from under a stored index; that must not brick
  // the config, for the same reason a vanished theme does not.
  Config c{};
  char j[CFG_JSON_CAP];
  snprintf(j, sizeof j,
           "{\"v\":2,\"location\":\"X\",\"theme\":0,\"active_group\":3,"
           "\"groups\":[{\"name\":\"g\",\"watches\":[%s]}]}",
           ONE_WATCH);
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_UINT8(0, c.active_group);
}

void test_rejects_garbage_and_wrong_version() {
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse("not json at all", &c, 2) == CfgError::BadJson);
  TEST_ASSERT_TRUE(cfg_parse("", &c, 2) == CfgError::BadJson);
  TEST_ASSERT_TRUE(cfg_parse("{\"v\":2,\"location\":\"X\"", &c, 2) == CfgError::BadJson);
  TEST_ASSERT_TRUE(
      cfg_parse("{\"v\":3,\"location\":\"X\",\"groups\":[]}", &c, 2) == CfgError::BadVersion);
  TEST_ASSERT_TRUE(
      cfg_parse("{\"location\":\"X\",\"groups\":[]}", &c, 2) == CfgError::BadVersion);
}

void test_rejects_too_many_watches_in_a_group() {
  Config c{};
  char ws[CFG_JSON_CAP];
  int p = 0;
  for (int i = 0; i < MAX_WATCHES + 1; i++) {
    p += snprintf(ws + p, sizeof(ws) - p, "%s%s", i ? "," : "", ONE_WATCH);
  }
  char j[CFG_JSON_CAP];
  one_group(j, sizeof j, ws);
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::TooManyWatches);
}

void test_rejects_too_many_groups() {
  Config c{};
  char j[CFG_JSON_CAP];
  int p = snprintf(j, sizeof j,
                   "{\"v\":2,\"location\":\"X\",\"theme\":0,\"active_group\":0,\"groups\":[");
  for (int i = 0; i < MAX_GROUPS + 1; i++) {
    p += snprintf(j + p, sizeof(j) - p, "%s{\"name\":\"g\",\"watches\":[%s]}", i ? "," : "",
                  ONE_WATCH);
  }
  snprintf(j + p, sizeof(j) - p, "]}");
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::TooManyGroups);
}

void test_rejects_no_groups() {
  Config c{};
  TEST_ASSERT_TRUE(
      cfg_parse("{\"v\":2,\"location\":\"X\",\"theme\":0,\"groups\":[]}", &c, 2) ==
      CfgError::NoGroups);
}

void test_every_group_needs_an_enabled_watch() {
  // Not just the active one: a group you can switch to and get a blank board
  // is not worth being able to switch to.
  Config c{};
  char j[CFG_JSON_CAP];
  one_group(j, sizeof j, "");
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::NoWatches);

  const char* off =
      "{\"label\":\"a\",\"stop_code\":\"1\",\"route_short_name\":\"\","
      "\"toward_stop_code\":\"\",\"enabled\":false}";
  one_group(j, sizeof j, off);
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::NoWatches);

  // A second group with everything off is rejected even though the first is fine.
  snprintf(j, sizeof j,
           "{\"v\":2,\"location\":\"X\",\"theme\":0,\"active_group\":0,\"groups\":["
           "{\"name\":\"ok\",\"watches\":[%s]},{\"name\":\"bad\",\"watches\":[%s]}]}",
           ONE_WATCH, off);
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::NoWatches);
}

void test_rejects_missing_stop_code() {
  Config c{};
  char j[CFG_JSON_CAP];
  one_group(j, sizeof j,
            "{\"label\":\"a\",\"stop_code\":\"\",\"route_short_name\":\"\","
            "\"toward_stop_code\":\"\",\"enabled\":true}");
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::MissingStopCode);
}

void test_rejects_overlong_fields() {
  Config c{};
  char big[CFG_FIELD_CAP + 8];
  memset(big, 'x', sizeof big);
  big[sizeof(big) - 1] = '\0';
  char w[CFG_JSON_CAP];
  snprintf(w, sizeof w,
           "{\"label\":\"%s\",\"stop_code\":\"1\",\"route_short_name\":\"\","
           "\"toward_stop_code\":\"\",\"enabled\":true}",
           big);
  char j[CFG_JSON_CAP];
  one_group(j, sizeof j, w);
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::FieldTooLong);

  char loc[CFG_LOCATION_CAP + 8];
  memset(loc, 'y', sizeof loc);
  loc[sizeof(loc) - 1] = '\0';
  snprintf(j, sizeof j,
           "{\"v\":2,\"location\":\"%s\",\"theme\":0,\"active_group\":0,"
           "\"groups\":[{\"name\":\"g\",\"watches\":[%s]}]}",
           loc, ONE_WATCH);
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::LocationTooLong);
}

void test_rejects_an_overlong_group_name() {
  Config c{};
  char name[CFG_GROUP_NAME_CAP + 8];
  memset(name, 'z', sizeof name);
  name[sizeof(name) - 1] = '\0';
  char j[CFG_JSON_CAP];
  snprintf(j, sizeof j,
           "{\"v\":2,\"location\":\"X\",\"theme\":0,\"active_group\":0,"
           "\"groups\":[{\"name\":\"%s\",\"watches\":[%s]}]}",
           name, ONE_WATCH);
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::GroupNameTooLong);
}

void test_failed_parse_leaves_the_target_alone() {
  // config_save_json must be able to reject without destroying live config.
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &c, 2) == CfgError::Ok);
  TEST_ASSERT_TRUE(cfg_parse("rubbish", &c, 2) == CfgError::BadJson);
  TEST_ASSERT_EQUAL_STRING("Kingsland", c.location);
  TEST_ASSERT_EQUAL_UINT8(2, c.n_groups);
}

void test_every_error_has_text() {
  const CfgError all[] = {CfgError::Ok,
                          CfgError::BadJson,
                          CfgError::BadVersion,
                          CfgError::TooManyWatches,
                          CfgError::NoWatches,
                          CfgError::MissingStopCode,
                          CfgError::FieldTooLong,
                          CfgError::LocationTooLong,
                          CfgError::TooManyGroups,
                          CfgError::NoGroups,
                          CfgError::GroupNameTooLong};
  for (CfgError e : all) {
    const char* t = cfg_error_text(e);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_TRUE(strlen(t) > 0);
  }
}

void test_round_trips_through_serialise() {
  Config a{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &a, 2) == CfgError::Ok);
  char buf[CFG_JSON_CAP];
  const size_t n = cfg_serialize(a, buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_size_t(n, strlen(buf));

  Config b{};
  TEST_ASSERT_TRUE(cfg_parse(buf, &b, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_STRING(a.location, b.location);
  TEST_ASSERT_EQUAL_UINT8(a.theme, b.theme);
  TEST_ASSERT_EQUAL_UINT8(a.n_groups, b.n_groups);
  TEST_ASSERT_EQUAL_UINT8(a.active_group, b.active_group);
  for (int g = 0; g < a.n_groups; g++) {
    TEST_ASSERT_EQUAL_STRING(a.groups[g].name, b.groups[g].name);
    TEST_ASSERT_EQUAL_UINT8(a.groups[g].n_watches, b.groups[g].n_watches);
    for (int i = 0; i < a.groups[g].n_watches; i++) {
      const CfgWatch& x = a.groups[g].watches[i];
      const CfgWatch& y = b.groups[g].watches[i];
      TEST_ASSERT_EQUAL_STRING(x.label, y.label);
      TEST_ASSERT_EQUAL_STRING(x.stop_code, y.stop_code);
      TEST_ASSERT_EQUAL_STRING(x.route_short_name, y.route_short_name);
      TEST_ASSERT_EQUAL_STRING(x.toward_stop_code, y.toward_stop_code);
      // The disabled watch must survive the round trip as disabled.
      TEST_ASSERT_EQUAL_INT(x.enabled, y.enabled);
    }
  }
}

void test_a_full_config_fits_the_buffer_and_nvs() {
  // The worst case the schema allows: every group, every watch, every field
  // at its cap. CFG_JSON_CAP has to hold it, and it also has to stay clear of
  // the 4000-byte ceiling nvs_set_str would impose - which is the whole
  // reason config.cpp stores this as a blob instead.
  Config c{};
  memset(c.location, 'L', sizeof c.location - 1);
  c.theme = 255;
  c.n_groups = MAX_GROUPS;
  c.active_group = MAX_GROUPS - 1;
  for (int g = 0; g < MAX_GROUPS; g++) {
    memset(c.groups[g].name, 'N', sizeof c.groups[g].name - 1);
    c.groups[g].n_watches = MAX_WATCHES;
    for (int i = 0; i < MAX_WATCHES; i++) {
      CfgWatch& w = c.groups[g].watches[i];
      memset(w.label, 'a', sizeof w.label - 1);
      memset(w.stop_code, 'b', sizeof w.stop_code - 1);
      memset(w.route_short_name, 'c', sizeof w.route_short_name - 1);
      memset(w.toward_stop_code, 'd', sizeof w.toward_stop_code - 1);
      w.enabled = true;
    }
  }

  char buf[CFG_JSON_CAP];
  const size_t n = cfg_serialize(c, buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(n < 4000);

  Config back{};
  TEST_ASSERT_TRUE(cfg_parse(buf, &back, 0) == CfgError::Ok);
  TEST_ASSERT_EQUAL_UINT8(MAX_GROUPS, back.n_groups);
}

void test_serialise_refuses_a_small_buffer() {
  Config a{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &a, 2) == CfgError::Ok);
  char small[16];
  TEST_ASSERT_EQUAL_size_t(0, cfg_serialize(a, small, sizeof small));
  TEST_ASSERT_EQUAL_STRING("", small);
}

void test_publish_uses_the_active_group_only() {
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_UINT8(1, c.active_group);

  WatchConfig pub[MAX_WATCHES];
  TEST_ASSERT_EQUAL_UINT8(1, cfg_publish(c, pub));  // the Weekend group's one watch
  TEST_ASSERT_EQUAL_STRING("to the beach", pub[0].label);
  TEST_ASSERT_EQUAL_STRING("7042", pub[0].stop_code);

  c.active_group = 0;  // Weekday: two watches, the second disabled
  TEST_ASSERT_EQUAL_UINT8(1, cfg_publish(c, pub));
  TEST_ASSERT_EQUAL_STRING("8213", pub[0].stop_code);
}

void test_publish_compacts_around_a_disabled_watch() {
  char j[CFG_JSON_CAP];
  one_group(j, sizeof j,
            "{\"label\":\"first\",\"stop_code\":\"1\",\"route_short_name\":\"\","
            "\"toward_stop_code\":\"\",\"enabled\":true},"
            "{\"label\":\"middle\",\"stop_code\":\"2\",\"route_short_name\":\"\","
            "\"toward_stop_code\":\"\",\"enabled\":false},"
            "{\"label\":\"last\",\"stop_code\":\"3\",\"route_short_name\":\"\","
            "\"toward_stop_code\":\"\",\"enabled\":true}");
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_UINT8(3, c.groups[0].n_watches);

  WatchConfig pub[MAX_WATCHES];
  TEST_ASSERT_EQUAL_UINT8(2, cfg_publish(c, pub));
  TEST_ASSERT_EQUAL_STRING("first", pub[0].label);
  TEST_ASSERT_EQUAL_STRING("1", pub[0].stop_code);
  TEST_ASSERT_EQUAL_STRING("last", pub[1].label);
  TEST_ASSERT_EQUAL_STRING("3", pub[1].stop_code);
}

void test_published_pointers_reach_into_the_config() {
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &c, 2) == CfgError::Ok);
  c.active_group = 0;
  WatchConfig pub[MAX_WATCHES];
  TEST_ASSERT_EQUAL_UINT8(1, cfg_publish(c, pub));  // second watch is disabled
  TEST_ASSERT_EQUAL_PTR(c.groups[0].watches[0].stop_code, pub[0].stop_code);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_a_good_document);
  RUN_TEST(test_v1_is_lifted_into_a_single_group);
  RUN_TEST(test_v1_is_re_serialised_as_v2);
  RUN_TEST(test_empty_route_short_name_is_allowed);
  RUN_TEST(test_theme_is_clamped_not_rejected);
  RUN_TEST(test_active_group_is_clamped_not_rejected);
  RUN_TEST(test_rejects_garbage_and_wrong_version);
  RUN_TEST(test_rejects_too_many_watches_in_a_group);
  RUN_TEST(test_rejects_too_many_groups);
  RUN_TEST(test_rejects_no_groups);
  RUN_TEST(test_every_group_needs_an_enabled_watch);
  RUN_TEST(test_rejects_missing_stop_code);
  RUN_TEST(test_rejects_overlong_fields);
  RUN_TEST(test_rejects_an_overlong_group_name);
  RUN_TEST(test_failed_parse_leaves_the_target_alone);
  RUN_TEST(test_every_error_has_text);
  RUN_TEST(test_round_trips_through_serialise);
  RUN_TEST(test_a_full_config_fits_the_buffer_and_nvs);
  RUN_TEST(test_serialise_refuses_a_small_buffer);
  RUN_TEST(test_publish_uses_the_active_group_only);
  RUN_TEST(test_publish_compacts_around_a_disabled_watch);
  RUN_TEST(test_published_pointers_reach_into_the_config);
  return UNITY_END();
}
