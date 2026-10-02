#region Copyright (C) 2005-2026 Team MediaPortal

// Copyright (C) 2005-2026 Team MediaPortal
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
using System.IO;
using System.Windows.Forms;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using MediaPortal.Profile;
using MediaPortal.RemoteApi;
using WatchDogService.Interface;
using WatchDogService.Interface.Proto;

namespace WatchDog
{
  /// <summary>
  /// Client of the TV server's watchdog service (gRPC over TLS, authenticated with the TV server connection key).
  /// </summary>
  class TVServerManager
  {
    private const int ConnectTimeoutMs = 5000;
    private static readonly TimeSpan CallTimeout = TimeSpan.FromMinutes(5);
    private static readonly Empty E = new Empty();

    private readonly string _hostName;
    private readonly ConnectionKey _connectionKey;

    public TVServerManager()
    {
      try
      {
        string connectionKey;
        using (Settings xmlreader = new MPSettings())
        {
          _hostName = xmlreader.GetValueAsString("tvservice", "hostname", string.Empty);
          connectionKey = xmlreader.GetValueAsString("tvservice", "connectionkey", string.Empty);
        }
        // Without a configured key the key of a local TV server is used (WatchDog runs elevated).
        ConnectionKey.TryParse(connectionKey, out _connectionKey);

        if (string.IsNullOrEmpty(_hostName))
        {
          MessageBox.Show("The host name is empty.", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
      }
      catch (Exception)
      {
        MessageBox.Show("The host name is empty.", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
      }
    }

    private T Call<T>(Func<WatchDogService.Interface.Proto.WatchDog.WatchDogClient, CallOptions, T> call)
    {
      Channel channel = GrpcClientFactory.CreateChannel(_hostName, WatchDogEndpoint.Port, _connectionKey, ConnectTimeoutMs);
      try
      {
        return call(new WatchDogService.Interface.Proto.WatchDog.WatchDogClient(channel),
                    new CallOptions(deadline: DateTime.UtcNow.Add(CallTimeout)));
      }
      finally
      {
        channel.ShutdownAsync().Wait();
      }
    }

    private void CallAndShowResult(Func<WatchDogService.Interface.Proto.WatchDog.WatchDogClient, CallOptions, string> call)
    {
      string result;
      try
      {
        result = Call(call);
      }
      catch (Exception ex)
      {
        MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
        return;
      }
      MessageBox.Show(result, "Status", MessageBoxButtons.OK, MessageBoxIcon.Information);
    }

    public void TvServerRemoteStart()
    {
      CallAndShowResult((client, options) => client.StartTvService(E, options).Value);
    }

    public void TvServerRemoteStop()
    {
      CallAndShowResult((client, options) => client.StopTvService(E, options).Value);
    }

    public void TvServerRemoteLogRead(string ZipFile)
    {
      try
      {
        Call((client, options) =>
               {
                 using (AsyncServerStreamingCall<LogChunk> logs = client.ReadLog(E, options))
                 using (FileStream writer = new FileStream(ZipFile, FileMode.CreateNew, FileAccess.Write))
                 {
                   while (logs.ResponseStream.MoveNext().GetAwaiter().GetResult())
                     logs.ResponseStream.Current.Data.WriteTo(writer);
                 }
                 return true;
               });
      }
      catch (Exception ex)
      {
        MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
      }
    }

    public void RebootTvServer()
    {
      CallAndShowResult((client, options) =>
                          {
                            client.Reboot(E, options);
                            return "Done";
                          });
    }

    public void ShutdownTvServer()
    {
      CallAndShowResult((client, options) =>
                          {
                            client.Shutdown(E, options);
                            return "Done";
                          });
    }

    public void PowerOffTvServer()
    {
      CallAndShowResult((client, options) =>
                          {
                            client.PowerOff(E, options);
                            return "Done";
                          });
    }

    public void ClearTVserverLogs()
    {
      CallAndShowResult((client, options) => client.ClearTvServerLogs(E, options).Value);
    }

    public void ClearWindowsEventLogs()
    {
      CallAndShowResult((client, options) => client.ClearWindowsEventLogs(E, options).Value);
    }
  }
}
