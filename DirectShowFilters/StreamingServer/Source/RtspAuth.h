// Authentication of the RTSP server of the TV Server (SECURITY_AUDIT.md: CD-02).
// NULL or an empty user name: no database, the server stays open as before. The caller deletes the database, after the server.
#pragma once
#include "liveMedia.hh"

inline UserAuthenticationDatabase* MakeAuthDatabase(const char* userName, const char* password)
{
  if (userName == NULL || userName[0] == 0)
  {
    return NULL;
  }
  UserAuthenticationDatabase* database = new UserAuthenticationDatabase;
  database->addUserRecord(userName, password != NULL ? password : "");
  return database;
}
