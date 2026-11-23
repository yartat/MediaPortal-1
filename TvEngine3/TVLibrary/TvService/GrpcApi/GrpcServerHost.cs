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
using Grpc.Core;
using MediaPortal.RemoteApi;
using TvControl;
using TvLibrary.Log;
using P = TvControl.Grpc.Proto;

namespace TvService.GrpcApi
{
  /// <summary>
  /// Hosts the TvControl gRPC endpoint (TLS + API key) on the former remoting port.
  /// </summary>
  public sealed class GrpcServerHost
  {
    private Server _server;

    public void Start(IController controller)
    {
      Start(controller, GrpcSecurity.LoadOrCreateServerMaterial());
    }

    public void Start(IController controller, ServerKeyMaterial material)
    {
      _server = SecureGrpcServer.Create(RemoteControl.Port, material, message => Log.Error(message),
                                        P.TvControlService.BindService(new TvControlServiceImpl(controller)));
      _server.Start();
      Log.Info("gRPC: TvControl listening on port {0} (TLS, CA fingerprint {1})", RemoteControl.Port,
               material.CaFingerprint);
    }

    public void Stop()
    {
      Server server = _server;
      _server = null;
      if (server != null)
      {
        server.ShutdownAsync().Wait(5000);
        Log.Info("gRPC: TvControl stopped");
      }
    }
  }
}
