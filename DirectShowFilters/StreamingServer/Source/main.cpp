#include "StdAfx.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <streams.h>
#include <shlobj.h>
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "MPTaskScheduler.h"
#include "TsMPEG2TransportFileServerMediaSubsession.h" 
#include "MPRTSPServer.h"
#include "EventThreadCalls.h"
#include "RtspAuth.h"
#include <sstream>
#include <iomanip>  // setfill(), setw()

void LogDebug(const wchar_t *fmt, ...)
{
	va_list ap;
	va_start(ap,fmt);

	va_start(ap,fmt);
	wchar_t logbufferw[2000];
	_vsnwprintf_s(logbufferw, _countof(logbufferw), _TRUNCATE, fmt, ap);
	va_end(ap); 

	wchar_t folder[MAX_PATH];
	wchar_t fileName[MAX_PATH];
	::SHGetSpecialFolderPathW(NULL, folder, CSIDL_COMMON_APPDATA, FALSE);
	swprintf_s(fileName, L"%s\\Team MediaPortal\\MediaPortal TV Server\\log\\streaming server.Log", folder);

	FILE* fp = _wfopen(fileName,L"a+, ccs=UTF-8");
	if (fp!=NULL)
	{
		SYSTEMTIME systemTime;
		GetLocalTime(&systemTime);
		fwprintf(fp,L"[%04.4d-%02.2d-%02.2d %02.2d:%02.2d:%02.2d,%03.3d] [%x] - %s\n",
      systemTime.wYear, systemTime.wMonth, systemTime.wDay,
      systemTime.wHour, systemTime.wMinute, systemTime.wSecond, systemTime.wMilliseconds,
			GetCurrentThreadId(),
			logbufferw);
		fclose(fp);
	}
};

void LogDebug(const char *fmt, ...)
{
	va_list ap;
	va_start(ap,fmt);

	va_start(ap,fmt);
	char logbuffer[2000];
	_vsnprintf_s(logbuffer, sizeof(logbuffer), _TRUNCATE, fmt, ap);
	va_end(ap);

	wchar_t logbufferw[2000];
	MultiByteToWideChar(CP_ACP, 0, logbuffer, -1, logbufferw, _countof(logbufferw));
	LogDebug(L"%s", logbufferw);
};

const char* STREAM_NAME = "testStream";
const char* STREAM_DESCRIPTION = "Session streamed by \"MediaPortal Tv Server v1.2 Beta 1\"";
const char* FILE_NAME = "C:\\temp\\testApp\\live.ts.tsbuffer";
const int	DEFAULT_RTSP_PORT = 554;

UsageEnvironment* m_env;
MPRTSPServer*			m_rtspServer;
// Digest authentication of the RTSP server (CD-02); live555 does not take over the database, it is deleted after the server
static UserAuthenticationDatabase* m_authDb = NULL;

// live555 is single-threaded: m_env, m_rtspServer and the server tables are touched only on the thread
// that runs StreamRun(); calls from other threads are queued to it (and run directly while it is idle).
static EventThreadCalls g_calls;
static const std::chrono::milliseconds CALL_TIMEOUT(10000);

void StreamSetup(char* ipAdress);
int  StreamSetupEx(char* ipAdress, int port);
int  StreamSetupAuth(char* ipAdress, int port, char* userName, char* password);
void StreamShutdown();
void StreamRun();
void announceStream(RTSPServer* rtspServer, ServerMediaSession* sms, char * streamName, wchar_t * inputFileName); // fwd
void StreamAddTimeShiftFile(char* streamName, wchar_t* fileName,bool isProgramStream,int channelType);
void StreamAddMpegFile(char* streamName, wchar_t* fileName, int channelType);
void StreamRemove(char* streamName);

extern ipv4AddressBits SendingInterfaceAddr ;
extern ipv4AddressBits ReceivingInterfaceAddr ;

#if _DEBUG

int main(int argc, char* argv[])
{
	StreamSetup("192.168.1.130");
	StreamAddTimeShiftFile("test1", L"C:\\1\\live5-0.ts.tsbuffer",false,0);
	//StreamAddMpegFile("test2", "C:\\media\\movies\\NED 1.mpg");
	//StreamAddMpegFile("test3", "C:\\media\\movies\\PREMIERE 420070201-1146.ts");
	while (true)
	{
		StreamRun();
	}
	StreamRemove("test");
	return 0;
}

#endif


//**************************************************************************************
void StreamGetClientCount(int* clients)
{
	*clients=0;
	std::shared_ptr<int> count = std::make_shared<int>(0);
	if (g_calls.Call([count]()
	{
		if (m_rtspServer != NULL) *count = m_rtspServer->GetSessionCount();
	}, CALL_TIMEOUT))
	{
		*clients = *count;
	}
}

//**************************************************************************************
void StreamGetClientDetail(unsigned short index, char** ipAddress, char** streamId, int* isActive, long* connectionTickCount)
{
  static char szIpAddress[50];
  static char szStreamId[260];
  *ipAddress = NULL;
  *streamId = NULL;
  *isActive = 0;
  *connectionTickCount = 0;

  struct Detail { bool found; int active; long ticks; };
  std::shared_ptr<Detail> detail = std::make_shared<Detail>();
  detail->found = false;
  detail->active = 0;
  detail->ticks = 0;
  bool done = g_calls.Call([index, detail]()
  {
    if (m_rtspServer == NULL)
    {
      return;
    }
    MPRTSPServer::MPRTSPClientSession* client = m_rtspServer->GetSessionByIndex(index);
    if (client == NULL)
    {
      return;
    }

    struct sockaddr_in clientAddress = client->ClientAddress();
    sprintf_s(szIpAddress, "%d.%d.%d.%d",
              clientAddress.sin_addr.S_un.S_un_b.s_b1,
              clientAddress.sin_addr.S_un.S_un_b.s_b2,
              clientAddress.sin_addr.S_un.S_un_b.s_b3,
              clientAddress.sin_addr.S_un.S_un_b.s_b4);

    const char* sid = client->StreamId();
    if (sid != NULL)
    {
      strncpy_s(szStreamId, sid, _TRUNCATE);
    }
    detail->active = client->IsPaused() ? 0 : 1;
    detail->ticks = (long)client->StartDateTime();
    detail->found = true;
  }, CALL_TIMEOUT);

  if (!done || !detail->found)
  {
    return;
  }
  *ipAddress = &szIpAddress[0];
  *streamId = &szStreamId[0];
  *isActive = detail->active;
  *connectionTickCount = detail->ticks;
}

//**************************************************************************************
void StreamSetup(char* ipAdress)
{
	if (StreamSetupEx(ipAdress, DEFAULT_RTSP_PORT) == 1)
	{
		LogDebug("Exiting process after error");
		exit(1);
	}
}
//**************************************************************************************
int StreamSetupEx(char* ipAdress, int port)
{
  return StreamSetupAuth(ipAdress, port, NULL, NULL);
}

// With a user name every request of a client must carry the user name and the password (RTSP digest); without one the server is open
// as before. NULL or an empty name means no authentication.
int StreamSetupAuth(char* ipAdress, int port, char* userName, char* password)
{
	wchar_t folder[MAX_PATH];
	wchar_t fileName[MAX_PATH];
	::SHGetSpecialFolderPathW(NULL,folder,CSIDL_COMMON_APPDATA,FALSE);
	swprintf(fileName, MAX_PATH, L"%s\\Team MediaPortal\\MediaPortal TV Server\\log\\streaming server.Log", folder);
	::DeleteFileW(fileName);

	LogDebug("-------------- v1.1.5 ---------------");
  StreamShutdown();
	if (port == DEFAULT_RTSP_PORT) {
		LogDebug("Stream server:Setup stream server for ip: %s", ipAdress);
	}
	else
	{
		LogDebug("Stream server:Setup stream server for ip: %s:%d", ipAdress, port);
	}

	ReceivingInterfaceAddr=inet_addr(ipAdress );
	SendingInterfaceAddr=inet_addr(ipAdress );

	TaskScheduler* scheduler = MPTaskScheduler::createNew();
	m_env = BasicUsageEnvironment::createNew(*scheduler);
	m_authDb = MakeAuthDatabase(userName, password);
	if (m_authDb != NULL)
	{
		LogDebug("Stream server:RTSP authentication is on");
	}
	m_rtspServer = MPRTSPServer::createNew(*m_env, port, m_authDb);
	if (m_rtspServer == NULL) 
	{
		LogDebug("Stream server:Failed to create RTSP server: %s",m_env->getResultMsg());
		delete m_authDb;
		m_authDb = NULL;
		return 1;
	}
	return 0; // ok
}
//**************************************************************************************
static void ShutdownOnEventThread()
{
  if (m_rtspServer != NULL)
  {
    LogDebug("Stream server:Shutting down RTSP server");
    MPRTSPServer *server = m_rtspServer;
    m_rtspServer = NULL;
    Medium::close(server);
  }
  delete m_authDb;   // after the server: it may still use the database while it closes
  m_authDb = NULL;

  if (m_env != NULL)
  {
    LogDebug("Stream server:Cleaning up environment");
    UsageEnvironment *env = m_env;
    m_env = NULL;
    TaskScheduler *scheduler = &env->taskScheduler();
    env->reclaim();
    delete scheduler;
  }
}

void StreamShutdown()
{
  g_calls.Call(ShutdownOnEventThread, CALL_TIMEOUT);
}
//**************************************************************************************
void StreamRun()
{
	g_calls.Enter();
	for (int i=0; i < 10;++i)
	{
		g_calls.Drain();
		if (m_env == NULL) break;
		m_env->taskScheduler().doEventLoop(); 
	}
	g_calls.Leave();
}

//**************************************************************************************
void StreamRemove( char* streamName)
{
	LogDebug("Stream server:Stream server: remove stream %s", streamName);
	std::string name(streamName);
	g_calls.Call([name]()
	{
		if (m_rtspServer != NULL) m_rtspServer->removeServerMediaSession(name.c_str());
	}, CALL_TIMEOUT);
}

//**************************************************************************************
// Copies the arguments (the caller's buffers are gone once a timed-out call returns) and adds the stream on the event thread
static void AddFileOnEventThread(const char* streamName, const wchar_t* fileName, bool timeShift, int channelType)
{
	std::string name(streamName);
	std::wstring file(fileName);
	g_calls.Call([name, file, timeShift, channelType]()
	{
		try
		{
			if (m_env == NULL || m_rtspServer == NULL) return;
			LogDebug(timeShift ? L"Stream server: add timeshift  mpeg-2 transport stream %S filename:%s"
			                   : L"Stream server: add mpeg-2 ts stream %S filename:%s", name.c_str(), file.c_str());
			ServerMediaSession* sms= ServerMediaSession::createNew(*m_env, name.c_str(), name.c_str(),STREAM_DESCRIPTION,false);
			sms->addSubsession(TsMPEG2TransportFileServerMediaSubsession::createNew(*m_env, file.c_str(), false,timeShift,channelType));
			m_rtspServer->addServerMediaSession(sms);

			announceStream(m_rtspServer, sms, (char*)name.c_str(), (wchar_t*)file.c_str());
		}
		catch(...)
		{
			LogDebug(L"Stream server: unable to add stream %S filename:%s", name.c_str(), file.c_str());
		}
	}, CALL_TIMEOUT);
}

//**************************************************************************************
void StreamAddTimeShiftFile(char* streamName, wchar_t* fileName,bool isProgramStream,int channelType)
{
	AddFileOnEventThread(streamName, fileName, true, channelType);
}

//**************************************************************************************
void StreamAddMpegFile(char* streamName, wchar_t* fileName, int channelType)
{
	AddFileOnEventThread(streamName, fileName, false, channelType);
}

//**************************************************************************************
void announceStream(RTSPServer* rtspServer, ServerMediaSession* sms, char * streamName, wchar_t * inputFileName) 
{
	char* url = rtspServer->rtspURL(sms);
	LogDebug("Stream server: url for stream is %s", url);
}
