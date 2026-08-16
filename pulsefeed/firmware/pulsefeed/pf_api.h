// =====================================================================
//  pf_api.h -- HTTP server and REST API v1
//
//  v43 exposed twenty endpoints with no authentication whatsoever, all
//  of them GET, including /api/run?state=1 and /api/estop. On a LAN
//  that means any device can start the machine; behind the internet
//  proxy the owner asked for, it means anyone can. It also means any
//  web page in any tab could start the machine with a single <img> tag,
//  because state-changing GETs are forgeable cross-site.
//
//  This layer:
//    * authenticates with a per-device PIN and bearer tokens
//    * makes every mutation a POST carrying the token in a header,
//      which a cross-site form cannot forge
//    * rate limits the PIN endpoint
//    * never reconfigures the network from inside a request
// =====================================================================
#pragma once

#include "pf_app.h"

namespace pf {
namespace api {

void begin(uint16_t port);
void stop();
void service();
bool running();

// Issued on first boot if none is stored.
void ensurePin();

uint32_t requestCount();
uint32_t rejectCount();

}  // namespace api
}  // namespace pf
