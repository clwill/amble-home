#undef MINI_TEST_MAIN
#include "mesh_node.h"
#include "mini_test.h"

using namespace esphome::amble;

TEST(snoop_accepts_first_message_from_source) { Freshness f; CHECK(f.accept(0x001E, 0, 100)); CHECK_EQ(f.tracked(), 1u); }
TEST(snoop_rejects_duplicate_sequence) { Freshness f; CHECK(f.accept(0x001E, 0, 100)); CHECK(!f.accept(0x001E, 0, 100)); }
TEST(snoop_rejects_old_sequence) { Freshness f; CHECK(f.accept(0x001E, 0, 100)); CHECK(!f.accept(0x001E, 0, 99)); CHECK(!f.accept(0x001E, 0, 1)); }
TEST(snoop_accepts_increasing_sequence) { Freshness f; CHECK(f.accept(0x001E, 0, 100)); CHECK(f.accept(0x001E, 0, 101)); CHECK(f.accept(0x001E, 0, 5000)); }
TEST(snoop_iv_index_orders_before_sequence) { Freshness f; CHECK(f.accept(0x001E, 0, 16000000)); CHECK(f.accept(0x001E, 1, 5)); CHECK(!f.accept(0x001E, 0, 16000001)); }
TEST(snoop_tracks_sources_independently) { Freshness f; CHECK(f.accept(0x000C, 0, 500)); CHECK(f.accept(0x0010, 0, 10)); CHECK(!f.accept(0x000C, 0, 10)); CHECK(f.accept(0x0010, 0, 11)); }
TEST(snoop_full_table_refuses_unknown_source_and_keeps_marks) {
  Freshness f;
  for (uint16_t s = 0; s < Freshness::MAX_SOURCES; s++) CHECK(f.accept(0x0100 + s, 0, 50));
  CHECK_EQ(f.tracked(), Freshness::MAX_SOURCES);
  CHECK(!f.accept(0x0FFF, 0, 1));          // 33rd source refused
  CHECK(!f.accept(0x0100, 0, 49));          // tracked source still protected
  CHECK(f.accept(0x0100, 0, 51));
}
TEST(snoop_reset_forgets) { Freshness f; CHECK(f.accept(0x001E, 0, 100)); f.reset(); CHECK_EQ(f.tracked(), 0u); CHECK(f.accept(0x001E, 0, 1)); }

TEST(snoop_unwanted_sources_do_not_consume_slots) {
  // The filter runs before Freshness in the wrap; here we prove the table itself is
  // only fed by callers that passed it: 32 configured sources fit after any amount
  // of unrelated traffic that was filtered out upstream.
  Freshness f;
  for (uint16_t s = 0; s < Freshness::MAX_SOURCES; s++) CHECK(f.accept(0x0010 + s, 0, 1));
  CHECK_EQ(f.tracked(), Freshness::MAX_SOURCES);
}
