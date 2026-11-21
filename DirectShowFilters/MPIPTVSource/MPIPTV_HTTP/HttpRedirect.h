// The count of redirects of the HTTP protocol of MPIPTVSource, in a header so that the tests can call it. The behaviour is that of the
// code before it was moved here (every redirect is followed); see SECURITY_AUDIT.md (HT-01).
#pragma once

const unsigned int HTTP_MAX_REDIRECTS = 10;

// 'redirects' counts the redirects since the last answer with data; returns whether this one is followed.
inline bool HttpRedirectAllowed(unsigned int& redirects)
{
  return ++redirects <= HTTP_MAX_REDIRECTS;
}
