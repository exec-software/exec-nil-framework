/* Synthetic ABI peer, never a browser provider. SPDX-License-Identifier: MIT */
#define NILWEBVIEW_PROVIDER_BUILD
#include "../common/webview_provider_v1.h"
#include <atomic>
#include <cstdint>
#include <string>

struct nilwv_context_v1 {
  bool open = false, event = false;
  std::int64_t kind = NILWV_EVENT_READY, operation = 0;
  std::string payload, detail, control;
};
namespace {
std::atomic<std::int64_t> contexts{0}, releases{0};
void ok(nilwv_result_v1 *r, std::int64_t value = 1) { *r = {sizeof(*r), 0, value, {nullptr, 0}}; }
void bad(nilwv_result_v1 *r) { static const char text[] = "synthetic invalid handle"; *r = {sizeof(*r), NILWV_E_HANDLE, 0, {text, sizeof(text)-1}}; }
void probe(nilwv_result_v1 *r) { ok(r); }
void create(nilwv_context_v1 **c, nilwv_result_v1 *r) { *c = new nilwv_context_v1; ++contexts; ok(r); }
void destroy(nilwv_context_v1 *c) { delete c; --contexts; }
void window(nilwv_context_v1 *c, nilwv_text_v1, std::int64_t, std::int64_t, nilwv_result_v1 *r) {
  if (c->open) { bad(r); return; }
  c->open = c->event = true; c->kind = NILWV_EVENT_READY; ok(r, 17);
}
void show(nilwv_context_v1 *c, std::int64_t w, nilwv_result_v1 *r) { if (c->open && w == 17) ok(r); else bad(r); }
void navigate(nilwv_context_v1 *c, std::int64_t w, nilwv_text_v1, nilwv_result_v1 *r) {
  if (!c->open || w != 17) { bad(r); return; }
  c->event = true; c->kind = NILWV_EVENT_NAVIGATION_COMPLETED; c->operation = 31; c->payload = "\"about:blank\""; ok(r, 31);
}
void script(nilwv_context_v1 *c, std::int64_t w, nilwv_text_v1 body, nilwv_result_v1 *r) {
  if (!c->open || w != 17) { bad(r); return; }
  c->event = true; c->kind = NILWV_EVENT_SCRIPT_COMPLETED; c->operation = 32;
  const std::string value(body.data, body.size);
  c->control = value;
  c->payload = value == "bad-json" ? "{invalid" : "{\"answer\":42}";
  if (value == "surrogate") c->payload = "\"\\ud800\"";
  if (value == "deep33" || value == "deep64" || value == "deep65") {
    const auto depth = value == "deep33" ? 33 : value == "deep64" ? 64 : 65;
    c->payload = std::string(depth, '[') + "0" + std::string(depth, ']');
  }
  if (value == "bad-message") { c->kind = NILWV_EVENT_PAGE_MESSAGE; c->operation = 0; c->payload.clear(); }
  c->detail = value == "bad-text" ? std::string("\xc0\xaf", 2) : "";
  ok(r, 32);
}
void poll(nilwv_context_v1 *c, std::int64_t w, nilwv_result_v1 *r) {
  if (!c->open || w != 17) { bad(r); return; } ok(r, c->event ? 19 : 0);
}
void number(nilwv_context_v1 *c, std::int64_t e, nilwv_text_v1 property, nilwv_result_v1 *r) {
  if (!c->event || e != 19) { bad(r); return; }
  const std::string field(property.data, property.size);
  if (field == "window") ok(r,17);
  else if (field == "kind") ok(r,c->kind);
  else if (field == "sequence") ok(r,1);
  else if (field == "operation") ok(r,c->operation);
  else if (field == "value_kind") ok(r,c->payload.empty() ? NILWV_VALUE_NONE : NILWV_VALUE_JSON);
  else if (field == "source_kind") ok(r,c->control == "bad-origin" ? NILWV_SOURCE_NATIVE_ORIGIN : c->control == "bad-url" ? NILWV_SOURCE_NATIVE_URL : 0);
  else if (field == "frame_kind") ok(r,c->control == "bad-frame" ? NILWV_FRAME_MAIN : 0);
  else if (field == "failure_kind") ok(r,0);
  else bad(r);
}
void text(nilwv_context_v1 *c, std::int64_t e, nilwv_text_v1 property, nilwv_result_v1 *r) {
  if (!c->event || e != 19) { bad(r); return; }
  const std::string field(property.data, property.size);
  ok(r,0);
  if (field == "payload_json") r->text = {c->payload.data(), c->payload.size()};
  else if (field == "detail") r->text = {c->detail.data(), c->detail.size()};
  else if (field != "source_url" && field != "source_origin") bad(r);
}
void release(nilwv_context_v1 *c, std::int64_t e, nilwv_result_v1 *r) {
  if (!c->event || e != 19) { bad(r); return; }
  c->event = false; c->payload.assign("released storage"); c->detail.assign("released storage"); ++releases; ok(r);
}
void close(nilwv_context_v1 *c, std::int64_t w, nilwv_result_v1 *r) {
  if (!c->open || w != 17) { bad(r); return; } c->open = c->event = false; ok(r);
}
const nilwv_api_v1 api{
#ifdef NILWV_FIXTURE_BAD_ABI
  2,
#else
  NILWV_ABI_V1,
#endif
  sizeof(nilwv_api_v1), 0, "synthetic", "test-only", probe, create, destroy,
  window, show, navigate, navigate, script, poll, number, text, release, close};
}
extern "C" NILWV_EXPORT const nilwv_api_v1 *NILWV_CALL
nilwebview_provider_v1_get_api(std::uint32_t version, std::uint32_t size) {
  return version == NILWV_ABI_V1 && size <= sizeof(api) ? &api : nullptr;
}
extern "C" NILWV_EXPORT std::int64_t nilwebview_fixture_live_contexts() { return contexts; }
extern "C" NILWV_EXPORT std::int64_t nilwebview_fixture_releases() { return releases; }
