#region Copyright (C) 2005-2011 Team MediaPortal

// Copyright (C) 2005-2011 Team MediaPortal
// http://www.team-mediaportal.com
// 
// MediaPortal is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
// 
// MediaPortal is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with MediaPortal. If not, see <http://www.gnu.org/licenses/>.

#endregion

using System;
using System.Diagnostics;
using System.ServiceProcess;
using Grpc.Core;
using MediaPortal.RemoteApi;
using WatchDogService.Interface;
using WatchDogService.Interface.Proto;

namespace WatchDogService
{
  public partial class WatchDogService : ServiceBase
  {
    EventLog _eventLog = new EventLog();
    Server _server;

    public WatchDogService()
    {
      InitializeComponent();
      _eventLog.Source = "WatchDogService";
    }

    protected override void OnStart(string[] args)
    {
      try
      {
        // TLS + API key, same key material as the TV server
        _server = SecureGrpcServer.Create(WatchDogEndpoint.Port, GrpcSecurity.LoadOrCreateServerMaterial(),
                                          message => _eventLog.WriteEntry(message, EventLogEntryType.Warning),
                                          WatchDog.BindService(new WatchDogGrpcService()));
        _server.Start();
      }
      catch (Exception ex)
      {
        _eventLog.WriteEntry("WatchDogService exception" + ex, EventLogEntryType.Error);
      }
    }

    protected override void OnStop()
    {
      try
      {
        if (_server != null)
          _server.ShutdownAsync().Wait(5000);
      }
      catch (Exception ex)
      {
        _eventLog.WriteEntry("WatchDogService exception" + ex, EventLogEntryType.Error);
      }
    }

  }
}
