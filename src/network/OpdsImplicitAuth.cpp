#include "OpdsImplicitAuth.h"

#include <Logging.h>

#if defined(FREEINK_NET_WOLFSSL)

#include <Arduino.h>
#include <OpdsLoginForm.h>
#include <SecureHttpClient.h>

namespace {

// A login chain is short: implicit endpoint -> SSO login page -> credential
// POST -> SSO handoff -> member sign-in -> opds:// callback.
constexpr int MAX_STEPS = 10;
// Login pages are small (a few KB); the cap bounds heap during auth.
constexpr size_t MAX_PAGE_BYTES = 16 * 1024;
constexpr int AUTH_TIMEOUT_MS = 30000;

// Bare host of a URL (no scheme, no port) for cookie domain matching.
std::string hostOf(const std::string& url) {
  size_t start = url.find("://");
  start = start == std::string::npos ? 0 : start + 3;
  size_t end = url.find('/', start);
  if (end == std::string::npos) end = url.size();
  const size_t colon = url.find(':', start);
  if (colon != std::string::npos && colon < end) end = colon;
  return url.substr(start, end - start);
}

}  // namespace

bool opdsImplicitAuthenticate(const std::string& authenticateUrl, const std::string& username,
                              const std::string& password, std::string& outToken) {
  OpdsCookieJar jar;
  std::string url = authenticateUrl;
  std::string postBody;
  bool isPost = false;
  bool formSubmitted = false;

  for (int step = 0; step < MAX_STEPS; ++step) {
    freeink::SecureHttpClient http;
    http.setTimeout(AUTH_TIMEOUT_MS);
    http.setInsecure();
    if (!http.begin(url)) {
      LOG_ERR("OPDS", "Implicit auth: bad URL");
      return false;
    }
    http.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
    const std::string host = hostOf(url);
    const std::string cookies = jar.headerFor(host);
    if (!cookies.empty()) http.addHeader("Cookie", cookies);
    if (isPost) http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    std::string page;
    const auto onData = [&page](const uint8_t* data, size_t len) {
      const size_t room = MAX_PAGE_BYTES - page.size();
      page.append(reinterpret_cast<const char*>(data), len < room ? len : room);
      return true;
    };
    const int status =
        isPost ? http.sendRequest("POST", reinterpret_cast<const uint8_t*>(postBody.data()), postBody.size(), onData)
               : http.GET(onData);
    if (status < 0) {
      LOG_ERR("OPDS", "Implicit auth: request failed at step %d", step);
      return false;
    }
    for (const auto& header : http.getHeaders()) {
      if (header.first == "set-cookie") jar.store(host, header.second);
    }
    isPost = false;
    postBody.clear();

    if (status >= 300 && status < 400) {
      const std::string location = http.getHeader("location");
      if (location.empty()) return false;
      if (location.rfind("opds://", 0) == 0) {
        outToken = extractOpdsAccessToken(location);
        if (outToken.empty()) LOG_ERR("OPDS", "Implicit auth: callback without access_token");
        return !outToken.empty();
      }
      if (!freeink::SecureHttpClient::resolveUrl(url, location, url)) {
        LOG_ERR("OPDS", "Implicit auth: bad redirect");
        return false;
      }
      continue;
    }

    if (status == 200) {
      if (formSubmitted) {
        // A second login page after posting credentials means they were
        // rejected.
        LOG_ERR("OPDS", "Implicit auth: credentials rejected");
        return false;
      }
      const OpdsLoginForm form = buildOpdsLoginForm(page.data(), page.size(), username, password);
      if (!form.found) {
        LOG_ERR("OPDS", "Implicit auth: no login form found");
        return false;
      }
      if (!form.action.empty() && !freeink::SecureHttpClient::resolveUrl(url, form.action, url)) {
        LOG_ERR("OPDS", "Implicit auth: bad form action");
        return false;
      }
      postBody = form.body;
      isPost = true;
      formSubmitted = true;
      continue;
    }

    LOG_ERR("OPDS", "Implicit auth: unexpected status %d", status);
    return false;
  }
  LOG_ERR("OPDS", "Implicit auth: too many steps");
  return false;
}

#else

bool opdsImplicitAuthenticate(const std::string&, const std::string&, const std::string&, std::string&) {
  LOG_ERR("OPDS", "Implicit OAuth requires the wolfSSL HTTP client");
  return false;
}

#endif
