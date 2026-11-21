// LiveMedia555 (wrappers around live555): the RTSP server of the stream server and the RTSP client of the IPTV source,
// talking to each other over the loopback interface in one process. No filter is involved.
#include <gtest/gtest.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <chrono>
#include <string>
#include "BasicUsageEnvironment.hh"
#include "MPRTSPClient.h"
#include "MPRTSPServer.h"
#include "MPTaskScheduler.h"
#include "../unit/AllocLimit.h"
#include "../../StreamingServer/source/EventThreadCalls.h"
#include "../../StreamingServer/source/RtspAuth.h"

// MPRTSPServer logs through the host program
void LogDebug(const wchar_t*, ...) {}

namespace
{
  // A port that is free now (bind to port 0, read it, close)
  unsigned short FreePort()
  {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));   // ::, MPRTSPServer.h has "using namespace std"
    int len = sizeof(a);
    ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    ::closesocket(s);
    return ntohs(a.sin_port);
  }

  struct Answer
  {
    bool received = false;
    int code = 0;
    std::string text;
  };

  void OnAnswer(RTSPClient* client, int code, char* text)
  {
    Answer* answer = static_cast<Answer*>(static_cast<MPRTSPClient*>(client)->Context());
    answer->received = true;
    answer->code = code;
    if (text)
      answer->text = text;
    delete[] text;
  }

  class LiveMediaRtsp : public testing::Test
  {
  protected:
    MPTaskScheduler* scheduler = nullptr;
    UsageEnvironment* env = nullptr;
    unsigned short port = 0;

    void SetUp() override
    {
      scheduler = MPTaskScheduler::createNew();
      env = BasicUsageEnvironment::createNew(*scheduler);
      port = FreePort();
    }

    void TearDown() override
    {
      env->reclaim();
      delete scheduler;
    }

    // Runs the event loop until the answer is there (5 s at the most)
    bool Wait(const Answer& answer)
    {
      const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!answer.received && std::chrono::steady_clock::now() < end)
        static_cast<TaskScheduler*>(scheduler)->doEventLoop();   // one step, waits up to 1 s for an event
      return answer.received;
    }

    std::string Url(const char* stream) const { return "rtsp://127.0.0.1:" + std::to_string(port) + "/" + stream; }
  };
}

TEST_F(LiveMediaRtsp, ServerStartsWithoutSessions)
{
  MPRTSPServer* server = MPRTSPServer::createNew(*env, Port(port));
  ASSERT_NE(server, nullptr);
  EXPECT_EQ(server->GetSessionCount(), 0);
  EXPECT_EQ(server->GetSessionByIndex(0), nullptr);
  EXPECT_FALSE(server->RemoveSessionById(12345));
  Medium::close(server);
}

TEST_F(LiveMediaRtsp, ServerAnswersOptions)
{
  MPRTSPServer* server = MPRTSPServer::createNew(*env, Port(port));
  ASSERT_NE(server, nullptr);

  Answer answer;
  const std::string url = Url("none");
  MPRTSPClient* client = MPRTSPClient::createNew(&answer, *env, url.c_str(), 0, "mp-test");
  ASSERT_NE(client, nullptr);
  EXPECT_EQ(client->Context(), &answer);
  client->sendOptionsCommand(OnAnswer);
  ASSERT_TRUE(Wait(answer));
  EXPECT_EQ(answer.code, 0) << answer.text;
  EXPECT_NE(answer.text.find("DESCRIBE"), std::string::npos) << answer.text;

  Medium::close(client);
  Medium::close(server);
}

TEST_F(LiveMediaRtsp, UnknownStreamIsNotFound)
{
  MPRTSPServer* server = MPRTSPServer::createNew(*env, Port(port));
  ASSERT_NE(server, nullptr);

  Answer answer;
  const std::string url = Url("no-such-stream");
  MPRTSPClient* client = MPRTSPClient::createNew(&answer, *env, url.c_str(), 0, "mp-test");
  ASSERT_NE(client, nullptr);
  client->sendDescribeCommand(OnAnswer);
  ASSERT_TRUE(Wait(answer));
  EXPECT_EQ(answer.code, 404) << answer.text;
  EXPECT_EQ(server->GetSessionCount(), 0) << "a refused request must not leave a session";

  Medium::close(client);
  Medium::close(server);
}

TEST_F(LiveMediaRtsp, ServerListensOnIPv4Only)
{
  // the TV Server binds the stream server to an IPv4 address; there must be no listener on ::1
  MPRTSPServer* server = MPRTSPServer::createNew(*env, Port(port));
  ASSERT_NE(server, nullptr);

  SOCKET v6 = socket(AF_INET6, SOCK_STREAM, 0);
  if (v6 == INVALID_SOCKET)
  {
    Medium::close(server);
    GTEST_SKIP() << "no IPv6 on this machine";
  }
  sockaddr_in6 a = {};
  a.sin6_family = AF_INET6;
  a.sin6_addr = in6addr_loopback;
  a.sin6_port = htons(port);
  const int result = connect(v6, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  closesocket(v6);
  EXPECT_NE(result, 0);
  Medium::close(server);
}

TEST_F(LiveMediaRtsp, PortInUseGivesNoServer)
{
  MPRTSPServer* first = MPRTSPServer::createNew(*env, Port(port));
  ASSERT_NE(first, nullptr);
  MPRTSPServer* second = MPRTSPServer::createNew(*env, Port(port));
  EXPECT_EQ(second, nullptr);
  if (second)
    Medium::close(second);
  Medium::close(first);
}

// ---------------------------------------------------------------------------
// SECURITY_AUDIT.md: RS-01. live555 wants every subclass of the server to call cleanup() in its own destructor, so the sessions that
// are still there are deleted while the members of the subclass exist. MPRTSPServer does not: its destructor only clears the map of
// the sessions, and the sessions are deleted later in the destructor of the base class, where ~MPRTSPClientSession() erases itself
// from the map of the server that is already destroyed.
// ---------------------------------------------------------------------------

namespace
{
  // creates the sessions the way live555 does when a client sets up a stream, without a client
  struct SessionServer : MPRTSPServer
  {
    static SessionServer* Create(UsageEnvironment& env, unsigned short port)
    {
      Port ourPort(port);
      const int socket = setUpOurSocket(env, ourPort, AF_INET);
      return socket < 0 ? nullptr : new SessionServer(env, socket, ourPort);
    }

    void AddSessions(int count)
    {
      for (int i = 0; i < count; i++)
        createNewClientSessionWithId();
    }

  private:
    SessionServer(UsageEnvironment& env, int socket, Port port) : MPRTSPServer(env, socket, port, NULL, 65) {}
  };
}

// The reference: a session can be found and removed through the server, and a server without sessions stops.
TEST_F(LiveMediaRtsp, SessionOfTheServerCanBeRemovedById)
{
  SessionServer* server = SessionServer::Create(*env, port);
  ASSERT_NE(server, nullptr);
  server->AddSessions(2);
  ASSERT_EQ(server->GetSessionCount(), 2);
  MPRTSPServer::MPRTSPClientSession* session = server->GetSessionByIndex(0);
  ASSERT_NE(session, nullptr);
  EXPECT_TRUE(server->RemoveSessionById(session->SessionId()));
  EXPECT_EQ(server->GetSessionCount(), 1);
  server->RemoveSessionById(server->GetSessionByIndex(0)->SessionId());
  EXPECT_EQ(server->GetSessionCount(), 0);
  Medium::close(server);
}

// RS-01: the server is stopped while clients are connected (TV Server stops with viewers). A death test: the destructor may crash.
// The blocks are freed to the system (PageHeap), so a use of the destroyed map is an access violation without AddressSanitizer.
TEST(LiveMediaRtspDeathTest, StoppingTheServerWithSessionsDoesNotUseTheDestroyedMap)
{
  EXPECT_EXIT(
    {
      PageHeap pageHeap;
      MPTaskScheduler* scheduler = MPTaskScheduler::createNew();
      UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);
      SessionServer* server = SessionServer::Create(*env, FreePort());
      if (!server)
        exit(3);
      fprintf(stderr, "[child] server created\n");
      server->AddSessions(3);
      fprintf(stderr, "[child] sessions added\n");
      Medium::close(server);
      fprintf(stderr, "[child] server closed\n");
      exit(0);
    },
    ::testing::ExitedWithCode(0), "");
}

// RS-01: calls from the API thread of TV Server are run by the thread that drives the live555 event loop
TEST(EventThreadCalls, RunsOnTheEventThreadAndTheCallerWaits)
{
  EventThreadCalls calls;
  std::atomic<bool> stop(false);
  std::thread::id loopId;
  std::thread loop([&]()
  {
    loopId = std::this_thread::get_id();
    calls.Enter();
    while (!stop) { calls.Drain(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    calls.Leave();
  });
  while (loopId == std::thread::id()) std::this_thread::yield();   // Enter() follows right after

  std::thread::id ranOn;
  bool done = false;
  for (int i = 0; i < 1000 && !done; ++i)   // until Enter() has happened
  {
    done = calls.Call([&]() { ranOn = std::this_thread::get_id(); }, std::chrono::milliseconds(5000));
    if (ranOn == loopId) break;
    std::this_thread::yield();
  }
  EXPECT_TRUE(done);
  EXPECT_EQ(loopId, ranOn);
  stop = true;
  loop.join();
}

TEST(EventThreadCalls, RunsDirectlyWhenNoLoopIsActive)
{
  EventThreadCalls calls;
  std::thread::id ranOn;
  EXPECT_TRUE(calls.Call([&]() { ranOn = std::this_thread::get_id(); }, std::chrono::milliseconds(10)));
  EXPECT_EQ(std::this_thread::get_id(), ranOn);
}

TEST(EventThreadCalls, TimeoutLeavesTheCallQueuedAndLeaveRunsIt)
{
  EventThreadCalls calls;
  calls.Enter();                    // this thread is the "event thread" ...
  std::shared_ptr<int> hits = std::make_shared<int>(0);
  bool done = true;
  std::thread api([&]()             // ... and never drains while the API thread waits
  {
    done = calls.Call([hits]() { ++*hits; }, std::chrono::milliseconds(50));
  });
  api.join();
  EXPECT_FALSE(done);
  EXPECT_EQ(0, *hits);
  calls.Leave();
  EXPECT_EQ(1, *hits);
}

// SECURITY_AUDIT.md: CD-02. With a user name the server asks for the password (digest); without one it stays open.
TEST(RtspAuth, NoUserNameMeansNoDatabase)
{
  EXPECT_EQ(MakeAuthDatabase(nullptr, "x"), nullptr);
  EXPECT_EQ(MakeAuthDatabase("", "x"), nullptr);
  UserAuthenticationDatabase* database = MakeAuthDatabase("user", nullptr);   // no password: an empty one
  EXPECT_NE(database, nullptr);
  delete database;
}

TEST_F(LiveMediaRtsp, WithAuthenticationTheRequestNeedsTheRightPassword)
{
  UserAuthenticationDatabase* database = MakeAuthDatabase("tv", "secret");
  ASSERT_NE(database, nullptr);
  MPRTSPServer* server = MPRTSPServer::createNew(*env, Port(port), database);
  ASSERT_NE(server, nullptr);
  const std::string url = Url("no-such-stream");

  struct Case { const char* user; const char* password; int expected; };
  const Case cases[] = {{nullptr, nullptr, 401}, {"tv", "wrong", 401}, {"other", "secret", 401}, {"tv", "secret", 404}};  // 404: past the login
  for (const Case& c : cases)
  {
    Answer answer;
    MPRTSPClient* client = MPRTSPClient::createNew(&answer, *env, url.c_str(), 0, "mp-test");
    ASSERT_NE(client, nullptr);
    Authenticator authenticator(c.user ? c.user : "", c.password ? c.password : "");
    client->sendDescribeCommand(OnAnswer, c.user ? &authenticator : nullptr);
    ASSERT_TRUE(Wait(answer));
    EXPECT_EQ(answer.code, c.expected) << (c.user ? c.user : "(none)") << " / " << (c.password ? c.password : "(none)") << ": " << answer.text;
    Medium::close(client);
  }

  Medium::close(server);
  delete database;
}
