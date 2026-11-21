// The real CLogger writes a log file into the folder of TV Server and reads the configuration of MPIPTVSource;
// the tests of the RTP code do not need either. The test program defines MPIPTVSOURCE_EXPORTS and provides these members.
#include <windows.h>
#include "Logger.h"

CLogger::CLogger()
  : loggerInstance(GUID_NULL), mutex(NULL), maxLogSize(0), allowedLogVerbosity(0)
{
}

CLogger::~CLogger(void)
{
}

void CLogger::Log(unsigned int /*logLevel*/, const TCHAR* /*format*/, ...)
{
}
